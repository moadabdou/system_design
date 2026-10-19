"""Small synchronous RESP client for the step-4 prototype (stdlib only)."""
import argparse
import socket


class StoreError(Exception):
    pass


def encode(*parts):
    parts = [p if isinstance(p, bytes) else str(p).encode() for p in parts]
    return b"*%d\r\n" % len(parts) + b"".join(
        b"$%d\r\n" % len(p) + p + b"\r\n" for p in parts
    )


def read(stream):
    line = stream.readline()
    if not line:
        raise EOFError("server closed connection")
    if not line.endswith(b"\r\n"):
        raise ValueError("invalid response")
    tag, value = line[:1], line[1:-2]
    if tag == b"-":
        raise StoreError(value.decode())
    if tag == b"+":
        return value
    if tag == b":":
        return int(value)
    if tag == b"$":
        n = int(value)
        if n == -1:
            return None
        payload = stream.read(n)
        if len(payload) != n or stream.read(2) != b"\r\n":
            raise EOFError("truncated bulk reply")
        return payload
    if tag == b"*":
        return [read(stream) for _ in range(int(value))]
    raise ValueError("unknown response type")


class Client:
    def __init__(self, address, timeout=3):
        host, port = address.rsplit(":", 1)
        self.socket = socket.create_connection((host, int(port)), timeout)
        self.stream = self.socket.makefile("rb")

    def command(self, *parts):
        self.socket.sendall(encode(*parts))
        return read(self.stream)

    def close(self):
        self.stream.close()
        self.socket.close()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


def command(address, *parts):
    with Client(address) as client:
        return client.command(*parts)



if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--address", default="127.0.0.1:6380")
    parser.add_argument("parts", nargs="+")
    args = parser.parse_args()
    print(command(args.address, *args.parts))
