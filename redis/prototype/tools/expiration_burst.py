"""Measure PING latency and server RSS while untouched TTL keys expire."""
import argparse
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import platform
import socket
import subprocess
import tempfile
import time

from baseline import rss_bytes
from client import Client, encode, read


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--keys", type=int, default=10000)
    parser.add_argument("--value-bytes", type=int, default=4096)
    parser.add_argument("--ttl-ms", type=int, default=2000)
    args = parser.parse_args()
    if min(args.keys, args.value_bytes, args.ttl_ms) <= 0:
        parser.error("workload sizes must be positive")
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
    address = f"127.0.0.1:{port}"
    value = b"x" * args.value_bytes
    with tempfile.TemporaryFile() as log:
        process = subprocess.Popen([str(args.binary.resolve()), "--port", str(port)],
                                   stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + 5
            while True:
                try:
                    with Client(address) as client:
                        assert client.command("PING") == b"PONG"
                    break
                except (OSError, EOFError):
                    if process.poll() is not None or time.monotonic() >= deadline:
                        raise RuntimeError("server did not become ready")
                    time.sleep(0.01)
            with Client(address) as client:
                empty_rss = rss_bytes(process.pid)
                load_start = time.monotonic()
                for offset in range(0, args.keys, 32):
                    batch = range(offset, min(offset + 32, args.keys))
                    client.socket.sendall(b"".join(
                        encode("SET", f"{i:032d}", value, "PX", args.ttl_ms) for i in batch
                    ))
                    for _ in batch:
                        if read(client.stream) != b"OK":
                            raise RuntimeError("SET failed")
                load_seconds = time.monotonic() - load_start
                if load_seconds >= args.ttl_ms / 1000:
                    raise RuntimeError("load overlaps expiration; increase --ttl-ms")
                loaded_rss = rss_bytes(process.pid)
                samples, latencies = [], []
                # Avoid touching TTL keys until the final RSS sample. Only PING
                # traffic runs during the expiration and cleanup window.
                finish = time.monotonic() + args.ttl_ms / 1000 + 2
                while time.monotonic() < finish:
                    started = time.perf_counter()
                    if client.command("PING") != b"PONG":
                        raise RuntimeError("PING failed")
                    latencies.append((time.perf_counter() - started) * 1000)
                    samples.append({"seconds_since_load_start": time.monotonic() - load_start,
                                    "rss_bytes": rss_bytes(process.pid)})
                    time.sleep(0.01)
                after_rss = rss_bytes(process.pid)
                for i in range(args.keys):
                    if client.command("GET", f"{i:032d}") is not None:
                        raise RuntimeError("expired key remained visible")
                latencies.sort()
                result = {
                    "recorded_at": datetime.now(timezone.utc).isoformat(),
                    "base_revision": subprocess.check_output(
                        ["git", "rev-parse", "HEAD"],
                        cwd=Path(__file__).resolve().parents[3], text=True).strip(),
                    "working_tree_changes": subprocess.check_output(
                        ["git", "status", "--short"],
                        cwd=Path(__file__).resolve().parents[3], text=True).splitlines(),
                    "platform": platform.platform(), "binary": str(args.binary.resolve()),
                    "keys": args.keys, "key_bytes": 32, "value_bytes": args.value_bytes,
                    "ttl_ms": args.ttl_ms, "load_seconds": load_seconds,
                    "cleanup_entries_per_iteration": 128,
                    "ping_samples": len(latencies),
                    "ping_p50_ms": latencies[math.ceil(len(latencies) * 0.50) - 1],
                    "ping_p99_ms": latencies[math.ceil(len(latencies) * 0.99) - 1],
                    "ping_max_ms": max(latencies),
                    "empty_rss_bytes": empty_rss, "loaded_rss_bytes": loaded_rss,
                    "post_cleanup_window_rss_bytes": after_rss,
                    "rss_decrease_bytes": loaded_rss - after_rss,
                    "expired_keys_verified_absent": args.keys,
                    "samples": samples,
                }
                print(json.dumps(result, indent=2))
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
