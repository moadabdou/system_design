"""Small localhost baseline, not a capacity or peak-load benchmark."""
import argparse
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import platform
import socket
import subprocess
import tempfile
import time

from client import Client, encode, read


def rss_bytes(pid):
    for line in Path(f"/proc/{pid}/status").read_text().splitlines():
        if line.startswith("VmRSS:"):
            return int(line.split()[1]) * 1024
    raise RuntimeError("VmRSS unavailable")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--keys", type=int, default=10000)
    parser.add_argument("--requests", type=int, default=5000)
    parser.add_argument("--value-bytes", type=int, default=1024)
    args = parser.parse_args()
    if min(args.keys, args.requests, args.value_bytes) <= 0:
        parser.error("workload sizes must be positive")
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
    address = f"127.0.0.1:{port}"
    value = b"x" * args.value_bytes
    # Fixed 32-byte keys match the study's average-key assumption.
    def key(index):
        return f"{index:032d}"

    with tempfile.TemporaryFile() as log:
        process = subprocess.Popen(
            [str(args.binary.resolve()), "--port", str(port)],
            stdout=log, stderr=log,
        )
        try:
            deadline = time.monotonic() + 5
            while True:
                try:
                    with Client(address) as client:
                        if client.command("PING") != b"PONG":
                            raise RuntimeError("unexpected readiness reply")
                    break
                except (OSError, EOFError):
                    if process.poll() is not None or time.monotonic() >= deadline:
                        raise RuntimeError("server failed to become ready")
                    time.sleep(0.01)
            with Client(address) as client:
                client.command("PING")
                empty_rss = rss_bytes(process.pid)
                start = time.perf_counter()
                for offset in range(0, args.keys, 32):
                    batch = range(offset, min(offset + 32, args.keys))
                    client.socket.sendall(b"".join(
                        encode("SET", key(i), value) for i in batch
                    ))
                    for _ in batch:
                        if read(client.stream) != b"OK":
                            raise RuntimeError("unexpected SET reply")
                load_seconds = time.perf_counter() - start
                loaded_rss = rss_bytes(process.pid)
                for i in range(min(args.keys, 100)):
                    if client.command("GET", key(i)) != value:
                        raise RuntimeError("warmup GET mismatch")
                latencies = []
                start = time.perf_counter()
                for i in range(args.requests):
                    request_start = time.perf_counter()
                    if client.command("GET", key(i % args.keys)) != value:
                        raise RuntimeError("measured GET mismatch")
                    latencies.append((time.perf_counter() - request_start) * 1000)
                get_seconds = time.perf_counter() - start
            latencies.sort()
            cpu = next((line.split(":", 1)[1].strip() for line in
                        Path("/proc/cpuinfo").read_text().splitlines()
                        if line.startswith("model name")), "unknown")
            print(json.dumps({
                "recorded_at": datetime.now(timezone.utc).isoformat(),
                "server_source_revision": subprocess.check_output(
                    ["git", "rev-parse", "HEAD"],
                    cwd=Path(__file__).resolve().parents[3], text=True,
                ).strip(),
                "platform": platform.platform(),
                "cpu": cpu,
                "logical_cpus": os.cpu_count(),
                "mem_total": Path("/proc/meminfo").read_text().splitlines()[0],
                "binary": str(args.binary.resolve()),
                "transport": "localhost TCP; one persistent synchronous client",
                "keys": args.keys, "key_bytes": 32,
                "value_bytes": args.value_bytes, "set_pipeline": 32,
                "set_commands_per_second": args.keys / load_seconds,
                "get_requests": args.requests, "get_pipeline": 1,
                "get_hit_rate": 1.0,
                "get_commands_per_second": args.requests / get_seconds,
                "get_p50_ms": latencies[math.ceil(len(latencies) * 0.50) - 1],
                "get_p99_ms": latencies[math.ceil(len(latencies) * 0.99) - 1],
                "empty_rss_bytes": empty_rss,
                "loaded_rss_bytes": loaded_rss,
                "rss_growth_bytes_per_key": (loaded_rss - empty_rss) / args.keys,
                "validation": "All SET replies and warmup/measured GET values matched",
            }, indent=2))
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            if process.returncode != 0:
                log.seek(0)
                raise RuntimeError(log.read().decode(errors="replace"))


if __name__ == "__main__":
    main()
