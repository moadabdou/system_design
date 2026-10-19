"""Integration tests for the step-4 TCP key/value prototype (stdlib only)."""
import concurrent.futures
from pathlib import Path
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from client import Client, StoreError, command, encode, read

BINARY = str(Path(sys.argv.pop(1)).resolve())


def available_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


class TestServer:
    def __init__(self, directory, options=()):
        self.port = available_port()
        self.address = f"127.0.0.1:{self.port}"
        self.log_path = Path(directory) / "server.txt"
        self.process = None
        self.output = None
        self.options = options
        self.start()

    def start(self):
        self.output = self.log_path.open("ab", buffering=0)
        self.process = subprocess.Popen(
            [BINARY, "--port", str(self.port), *self.options], stdout=self.output, stderr=self.output
        )
        deadline = time.monotonic() + 5
        try:
            while time.monotonic() < deadline:
                if self.process.poll() is not None:
                    raise AssertionError(self.log_path.read_text())
                try:
                    if self.cmd("PING") == b"PONG":
                        return
                except (OSError, EOFError):
                    time.sleep(0.01)
            raise AssertionError("server did not become ready")
        except Exception:
            self.stop()
            raise

    def cmd(self, *parts):
        return command(self.address, *parts)

    def stop(self, crash=False):
        if self.process and self.process.poll() is None:
            if crash:
                self.process.kill()
            else:
                self.process.terminate()
            self.process.wait(timeout=5)
        if self.output:
            self.output.close()


class Integration(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="redis-step4-test-")
        self.servers = []

    def tearDown(self):
        errors = []
        for server in self.servers:
            server.stop()
            if server.process.returncode not in (0, -signal.SIGKILL):
                errors.append(server.log_path.read_text())
        self.temp.cleanup()
        self.assertFalse(errors, "\n".join(errors))

    def server(self, *options):
        directory = Path(self.temp.name) / str(len(self.servers))
        directory.mkdir()
        server = TestServer(directory, options)
        self.servers.append(server)
        return server

    def test_01_string_commands_and_single_thread(self):
        server = self.server()
        status = Path(f"/proc/{server.process.pid}/status").read_text()
        self.assertIn("Threads:\t1\n", status)
        self.assertIsNone(server.cmd("GET", "missing"))
        self.assertEqual(server.cmd("SET", b"a\0", b"v\0\r\n"), b"OK")
        self.assertEqual(server.cmd("GET", b"a\0"), b"v\0\r\n")
        self.assertEqual(server.cmd("SET", b"a\0", b"replacement"), b"OK")
        self.assertEqual(server.cmd("GET", b"a\0"), b"replacement")
        self.assertEqual(server.cmd("DEL", b"a\0"), 1)
        self.assertEqual(server.cmd("DEL", b"a\0"), 0)
        self.assertEqual(server.cmd("SET", b"", b""), b"OK")
        self.assertEqual(server.cmd("GET", b""), b"")

    def test_02_fragmentation_pipeline_order_and_half_close(self):
        server = self.server()
        with Client(server.address) as client:
            payload = encode("SET", "key", "fragmented")
            for byte in payload:
                client.socket.sendall(bytes([byte]))
            self.assertEqual(read(client.stream), b"OK")
            self.assertEqual(client.command("GET", "key"), b"fragmented")

            # More than one processing budget: buffered requests must progress
            # without new writes, and SET/GET ordering must remain intact.
            pipeline = b"".join(
                encode("SET", "key", i) + encode("GET", "key") for i in range(100)
            )
            client.socket.sendall(pipeline)
            client.socket.shutdown(socket.SHUT_WR)
            for i in range(100):
                self.assertEqual(read(client.stream), b"OK")
                self.assertEqual(read(client.stream), str(i).encode())
            self.assertEqual(client.stream.read(1), b"")

    def test_03_malformed_and_truncated_frames(self):
        server = self.server()
        for payload in [b"BAD\r\n", b"*1\r\n$999999999\r\n", b"*x\r\n",
                        b"*1\r\n$1\r\nxXX", b"*1\r\n$-1\r\n"]:
            with self.subTest(payload=payload), Client(server.address) as client:
                client.socket.sendall(payload)
                with self.assertRaises(StoreError):
                    read(client.stream)
                self.assertEqual(client.stream.read(1), b"")
        with Client(server.address) as client:
            client.socket.sendall(b"*2\r\n$3\r\nGET\r\n$10\r\nabc")
            client.socket.shutdown(socket.SHUT_WR)
            self.assertEqual(client.stream.read(1), b"")
        self.assertEqual(server.cmd("PING"), b"PONG")

    def test_04_concurrent_idle_and_slow_clients(self):
        server = self.server()
        large_value = b"x" * (1024 * 1024)
        server.cmd("SET", "large", large_value)
        with Client(server.address) as idle, Client(server.address) as slow:
            slow.socket.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
            slow.socket.sendall(encode("GET", "large") * 40)

            def worker(index):
                with Client(server.address) as client:
                    for i in range(20):
                        key = f"{index}:{i}"
                        self.assertEqual(client.command("SET", key, key), b"OK")
                        self.assertEqual(client.command("GET", key), key.encode())

            with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
                list(pool.map(worker, range(8)))
            self.assertEqual(server.cmd("GET", "large"), large_value)
            self.assertEqual(idle.command("PING"), b"PONG")

    def test_05_command_validation_preserves_existing_value(self):
        server = self.server()
        with Client(server.address) as client:
            client.command("SET", "keep", "old")
            invalid = [
                ("SET", "keep"), ("GET",), ("DEL", "keep", "extra"), ("BOGUS",),
                ("SET", "keep", b"x" * (1024 * 1024 + 1)),
                ("SET", "k" * 4097, "v"),
                ("SET", "keep", "new", "EX", "1000"),
            ]
            for parts in invalid:
                with self.subTest(command=parts[0]), self.assertRaises(StoreError):
                    client.command(*parts)
                self.assertEqual(client.command("GET", "keep"), b"old")

    def test_expiration_and_invalid_ttl_preserve_values(self):
        server = self.server()
        with Client(server.address) as client:
            self.assertEqual(client.command("SET", "expiring", "v", "PX", 30), b"OK")
            time.sleep(0.08)
            self.assertIsNone(client.command("GET", "expiring"))
            self.assertEqual(client.command("DEL", "expiring"), 0)
            client.command("SET", "deleted", "v", "PX", 30)
            time.sleep(0.08)
            self.assertEqual(client.command("DEL", "deleted"), 0)
            client.command("SET", "persistent", "old", "PX", 30)
            client.command("SET", "persistent", "new")
            time.sleep(0.08)
            self.assertEqual(client.command("GET", "persistent"), b"new")
            client.command("SET", "keep", "original", "PX", 5000)
            invalid = ["0", "-1", "1.5", "invalid", "31536000001",
                       "9223372036854775807", "9223372036854775808", ""]
            for ttl in invalid:
                with self.subTest(ttl=ttl), self.assertRaises(StoreError):
                    client.command("SET", "keep", "bad", "PX", ttl)
                self.assertEqual(client.command("GET", "keep"), b"original")
            for parts in [("SET", "keep", "bad", "PX"),
                          ("SET", "keep", "bad", "px", 10),
                          ("SET", "keep", "bad", "PX", 10, "extra")]:
                with self.subTest(parts=parts), self.assertRaises(StoreError):
                    client.command(*parts)
                self.assertEqual(client.command("GET", "keep"), b"original")
            client.command("SET", "keep", "short", "PX", 30)
            time.sleep(0.08)
            self.assertIsNone(client.command("GET", "keep"))
            client.socket.sendall(encode("SET", "pipeline", "v", "PX", 5000) +
                                  encode("GET", "pipeline") + encode("DEL", "pipeline"))
            client.socket.shutdown(socket.SHUT_WR)
            self.assertEqual(read(client.stream), b"OK")
            self.assertEqual(read(client.stream), b"v")
            self.assertEqual(read(client.stream), 1)
            self.assertEqual(client.stream.read(1), b"")

    def test_06_abrupt_disconnect_and_reused_descriptors(self):
        server = self.server()
        server.cmd("SET", "large", b"x" * (1024 * 1024))
        for _ in range(40):
            sock = socket.create_connection(("127.0.0.1", server.port), timeout=3)
            sock.sendall(encode("GET", "large"))
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
            sock.close()
            self.assertEqual(server.cmd("PING"), b"PONG")

    def test_memory_rejection_preserves_connection_and_replacement(self):
        server = self.server("--memory", "600")
        with Client(server.address) as client:
            client.command("SET", "a", "a", "PX", 5000)  # 292 bytes
            client.command("SET", "b", "b" * 114)  # 308 bytes
            with self.assertRaisesRegex(StoreError, "^OOM memory limit reached$"):
                client.command("SET", "a", "aa", "PX", 10000)
            self.assertEqual(client.command("GET", "a"), b"a")
            with self.assertRaisesRegex(StoreError, "^OOM entry exceeds memory limit$"):
                client.command("SET", "a", "x" * 601)
            self.assertEqual(client.command("GET", "a"), b"a")
            self.assertEqual(client.command("SET", "b", "c" * 114), b"OK")
            self.assertEqual(client.command("SET", "b", "c"), b"OK")
            self.assertEqual(client.command("DEL", "a"), 1)
            self.assertEqual(client.command("SET", "c", "c"), b"OK")
            self.assertEqual(client.command("PING"), b"PONG")

    def test_lru_eviction_and_work_limit(self):
        server = self.server("--memory", "585", "--policy", "allkeys-lru")
        with Client(server.address) as client:
            for key in ("a", "b", "c"):
                client.command("SET", key, key)
            self.assertEqual(client.command("GET", "a"), b"a")
            client.command("SET", "d", "d")
            self.assertIsNone(client.command("GET", "b"))
            self.assertEqual(client.command("DEL", "b"), 0)
            self.assertEqual(client.command("GET", "a"), b"a")
        server = self.server("--memory", "40000", "--policy", "allkeys-lru")
        with Client(server.address) as client:
            client.socket.sendall(b"".join(encode("SET", f"{i:04d}", "") for i in range(200)))
            for _ in range(200):
                self.assertEqual(read(client.stream), b"OK")
            with self.assertRaisesRegex(StoreError, "^OOM eviction work limit reached$"):
                client.command("SET", "0000", "x" * 25800)
            self.assertEqual(client.command("GET", "0000"), b"")
            self.assertEqual(client.command("GET", "0001"), b"")
            self.assertEqual(client.command("PING"), b"PONG")

    def test_07_restart_starts_with_empty_memory(self):
        server = self.server()
        server.cmd("SET", "key", "acknowledged")
        server.stop(crash=True)
        server.start()
        self.assertIsNone(server.cmd("GET", "key"))

    def test_08_cli_and_later_phase_features_are_unavailable(self):
        cases = [(["--port", "0"], "invalid port"),
                 (["--port", "65536"], "invalid port"),
                 (["--port", "not-a-number"], "invalid integer"),
                 (["--port"], "options require values")]
        for value in ["0", "-1", "1.5", "abc", "18446744073709551616", ""]:
            cases.append((["--memory", value], "invalid memory limit"))
        cases.extend([(["--memory"], "options require values"),
                      (["--policy", "random"], "invalid memory policy"),
                      (["--policy"], "options require values")])
        for option in ["--replica", "--dir", "--sync", "--node", "--peers"]:
            cases.append(([option, "unused"], "unknown option"))
        for args, message in cases:
            with self.subTest(args=args):
                result = subprocess.run([BINARY, *args], capture_output=True, timeout=3)
                self.assertEqual(result.returncode, 1)
                self.assertIn(message.encode(), result.stderr)
        server = self.server()
        for parts in [("SYNC",), ("SAVE",), ("INFO",), ("PROMOTE",),
                      ("FOLLOW", "127.0.0.1:6381"), ("SLOT", "key")]:
            with self.subTest(command=parts[0]), self.assertRaises(StoreError):
                server.cmd(*parts)


if __name__ == "__main__":
    unittest.main(verbosity=2)
