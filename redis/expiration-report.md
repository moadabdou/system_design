# Key Expiration: Implementation and Results

Issue #2 extends the single-node prototype with TTLs. The subsequent
[memory extension](memory-limits-report.md) implements issue #3. The [decision record](design-tradeoffs.md#key-expiration-decision-issue-2)
defines alternatives, costs, and semantics before the implementation.

## Client Behavior

```sh
python3 tools/client.py SET session hello PX 1000
python3 tools/client.py GET session
```

Run these from `redis/prototype/` with the server running. The key becomes absent
1,000 ms after SET executes. TTLs are positive integer milliseconds, at most
31,536,000,000 ms (365 days). Only uppercase PX is accepted. Invalid TTLs or SET
options leave the previous value and deadline unchanged.

- SET with PX replaces both value and deadline.
- SET without PX replaces the value and removes an earlier deadline.
- GET at or after the deadline returns null.
- DEL at or after the deadline returns zero.
- Expired values are reclaimed on access and by periodic cleanup even without
  subsequent access to those keys.

Deadlines use `steady_clock`, so wall-clock adjustments do not affect expiration.
The whole store is still lost on restart. Persistence of deadlines is deferred.

## Implementation

[Store](prototype/src/store.hpp) owns the value hash table and a sorted deadline
index with exactly one timer per expiring key. Each value holds an optional
iterator into the deadline index. Replacing or deleting a key removes its old
timer. A replacement is prepared before changing an existing entry, and a failed
new insertion rolls back its new timer.

GET and DEL check expiration directly, independent of periodic cleanup progress.
[The event loop](prototype/src/server.cpp) invokes `expire_due(128)` once after
the central command-processing pass. Cleanup removes only the earliest due
entries, yielding after 128. It never scans the entire dataset. Many simultaneous
deadlines can therefore take multiple iterations to reclaim, while expired keys
are already logically absent. The budget limits count, not elapsed time.

## Verification

The deterministic C++ store test supplies explicit time points rather than
sleeping. It verifies:

- Visibility one nanosecond before a deadline and absence exactly at it.
- GET and DEL reclaim both the value and timer; expired DEL returns zero.
- Replacing TTLs, clearing TTLs, adding TTLs, and preserving invalid replacements.
- The maximum TTL, clock overflow rejection, and 10,000 updates retaining one timer.
- 1,000 untouched keys expiring together: a 128-entry pass removes exactly 128;
  eight passes remove all 1,000 values and timers, preserving future/permanent keys.

The nine socket integration scenarios include TTL validation/overflow, plain SET
clearing TTL, shorter replacement TTL, expired GET/DEL, and pipelined TTL commands
followed by a half-close, alongside all previous protocol/network tests.

| Build | Result | CTest time |
| --- | --- | --- |
| Release | Store test and nine integration scenarios pass | 0.72 s |
| AddressSanitizer + UBSan | Same checks pass; no sanitizer diagnostics | 0.89 s |

Recorded output: [Release](prototype/results/ttl-release-tests.txt) and
[sanitizers](prototype/results/ttl-sanitizer-tests.txt).

## Expiration Burst Experiment

Prediction: bounded periodic cleanup should let unrelated PING requests complete
while untouched keys expire. Functional criteria: all SETs succeed, every PING
returns PONG without timeout, and every key is absent afterward. Latency and RSS
are observations without a performance threshold; store tests independently
verify physical entry/index reclamation and the cleanup budget.

The [experiment](prototype/tools/expiration_burst.py) loads 10,000 distinct 32-byte
keys with 4 KiB values and a 2,000 ms TTL, using SET pipelines of 32. Loading must
finish before the first deadline. It then sends only PINGs roughly every 10 ms
for four seconds, sampling RSS, before finally checking all keys for absence.
The TTL keys are untouched throughout the measured cleanup window.

Recorded 2026-10-19, Release build, on the same i5-1135G7/eight-logical-CPU,
approximately 7.48 GiB Linux host as the [baseline](implementation-report.md#initial-performance-and-memory-observations).
This uses the expiration working tree on top of `b15b6bb`, not that baseline
commit alone. [Raw JSON](prototype/results/ttl-expiration-burst.json) records the
revision, changed paths, configuration, timings, and RSS time series.

| Observation | Result |
| --- | --- |
| Time to load 10,000 keys | 0.743 s |
| PING samples during expiration window | 387 |
| PING p50 / p99 / maximum | 0.144 / 0.454 / 0.485 ms |
| Initial server RSS | 4,206,592 bytes |
| Loaded / post-cleanup-window RSS | 48,443,392 / 48,443,392 bytes |
| Keys verified absent afterward | 10,000 / 10,000 |

RSS did not decrease in this run. Freeing values and index nodes returns memory
to the allocator, which may retain it for reuse instead of returning pages to the
OS. RSS therefore does not establish the number of resident store entries; the
deterministic tests verify that count and the deadline index directly. The final
GET checks establish absence, and do not alone prove periodic reclamation.

This is one short localhost run with deadlines spread over the 0.743-second load
interval. PING latency includes the Python client and scheduler; it is not the
full mixed-workload target, a concurrent load test, or a worst-case simultaneous
expiration latency guarantee.

## Reproduce

From `redis/prototype/`:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build -V
python3 tools/expiration_burst.py build/study-store

cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DSANITIZE=ON
cmake --build build-asan -j2
ctest --test-dir build-asan -V
```

Both scripts and tests launch temporary localhost servers and clean them up.
