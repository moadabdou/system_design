# C++ Key-Value Store

This implementation covers one single-threaded, in-memory TCP server with
GET, SET, DEL, and key expiration. PING is a small readiness command used by the
client and tests. Eviction, persistence, replication, failover, and sharding are
deferred study phases documented in [the study outlines](../README.md#study-documents).

## Build and Run

Requires Linux, a C++20 compiler, CMake, and Python 3 for the client and tests.
There are no third-party dependencies. From this directory:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
./build/study-store --port 6380
```

In another terminal:

```sh
python3 tools/client.py SET greeting hello
python3 tools/client.py GET greeting
python3 tools/client.py DEL greeting
python3 tools/client.py SET temporary hello PX 1000
```

`--port` is the only configuration option. The server binds to `127.0.0.1` and
starts with an empty hash table. Stopping the process loses all stored data.

## Read the Code

Start with [main.cpp](src/main.cpp), then [Server::run()](src/server.cpp):

```cpp
while (running) {
    poll_network();
    drain_buffered_commands();
    store_.expire_due(EXPIRATIONS_PER_PASS);
}
```

The same OS thread accepts clients, reads requests, executes complete commands,
and writes replies. `drain_buffered_commands()` is the only command-processing
point: each connection executes at most `COMMANDS_PER_PASS` (32) commands per
event-loop iteration, even if no more bytes arrive on the socket. Socket handlers
read requests and write pending replies; replies queued by the drain pass are
sent on a later writable event.

See [Poll and epoll flags](../epoll-flags.md) for readiness meanings, interest
updates, partial I/O, and connection shutdown behavior.

| Files | Responsibility |
| --- | --- |
| [config.cpp](src/config.cpp) | Parse and validate the port before constructing the server |
| [server.hpp](src/server.hpp), [server.cpp](src/server.cpp) | Own connections and storage; run the event loop |
| [connection.hpp](src/connection.hpp), [server_network.cpp](src/server_network.cpp) | Per-client buffers, epoll registration, acceptance, partial reads/writes, and disconnects |
| [protocol.cpp](src/protocol.cpp) | Parse RESP arrays of bulk strings and encode replies |
| [server_commands.cpp](src/server_commands.cpp) | Validate and execute PING, GET, SET, and DEL |
| [store.hpp](src/store.hpp), [store.cpp](src/store.cpp) | Values, monotonic deadlines, access-time expiration, and bounded periodic cleanup |
| [common.hpp](src/common.hpp), [common.cpp](src/common.cpp) | Size limits, argument type, and integer parsing |

## Supported Behavior

- Commands are uppercase RESP arrays of bulk strings, with binary-safe keys and values.
- GET returns a value or a null bulk reply. SET creates/replaces a value. DEL returns 1 or 0.
- `SET key value PX milliseconds` sets a TTL from 1 through 31,536,000,000 ms
  (365 days). Only uppercase `PX` is supported. Invalid TTLs/options preserve the
  existing value and deadline. SET without PX clears an existing TTL.
- At the deadline, GET returns null and DEL returns 0. Expiration uses a monotonic
  clock; each loop iteration also reclaims at most 128 untouched expired keys.
- Multiple clients and pipelined commands are supported; each connection preserves command order.
- TCP reads may contain partial commands or multiple commands. Replies may require several writes.
- Values are limited to 1 MiB, keys to 4 KiB, each input/output buffer to 8 MiB,
  and accepted connections to 256. These are request/connection bounds, not a dataset memory policy.
- Reads, writes, accepts, and command execution have per-pass budgets for fairness.
- Malformed framing returns an error and closes the connection. Invalid commands
  return an error without changing data, and the connection remains usable.

The dataset grows until keys are deleted, expire, or process resources run out.
A reply confirms an in-memory operation only. There is no dataset memory limit
or administrative storage/distribution protocol in this implementation.

## Verify

```sh
ctest --test-dir build -V

cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DSANITIZE=ON
cmake --build build-asan -j2
ctest --test-dir build-asan -V
```

Tests cover deterministic expiration boundaries, bounded cleanup, TTL replacement
and validation, the commands, binary values, single OS-thread execution, fragmented
requests, pipelines, half-close, concurrent/slow clients, large replies, invalid
input, abrupt disconnects, and data loss on restart. They also check that deferred
features are unavailable. Each test uses temporary localhost servers and cleans
them up. See [the report](../implementation-report.md) for recorded results.

For initial throughput, GET latency, and process-memory observations:

```sh
python3 tools/baseline.py build/study-store --keys 10000 --requests 5000 --value-bytes 1024
```

This starts a temporary server and verifies replies. See the
[baseline results and limitations](../implementation-report.md#initial-performance-and-memory-observations);
it does not validate the full study's capacity or peak-load targets.

For the expiration-burst experiment:

```sh
python3 tools/expiration_burst.py build/study-store
```

See [the expiration report](../expiration-report.md) for the design, reclamation
checks, latency observations, and RSS limitations.

Formatting is defined in [.clang-format](.clang-format):

```sh
clang-format -i src/*.hpp src/*.cpp
```
