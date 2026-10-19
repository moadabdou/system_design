# Memory Limits: Implementation and Results

The [memory design](memory-limits-design.md) is implemented. Startup defaults to
a 64 MiB logical dataset budget and `noeviction`; `--memory BYTES` changes the
budget and `--policy allkeys-lru` enables eviction of live keys.

## Implementation

[Store](prototype/src/store.cpp) keeps an entry charge, total accounted bytes,
and a doubly linked LRU list. GET hits and successful SETs refresh recency.
Expiration, deletion, and eviction share one removal path for the value, timer,
LRU node, and charge.

Admission subtracts the target's old charge before checking its new size.
Expired entries are reclaimed first. If necessary, LRU victims are selected with
a combined limit of 128 entries reclaimed/selected per SET. Oversized entries
and writes that cannot fit within that work limit are rejected without live
eviction. Expired entries already reclaimed remain removed.

The new value, list node, timer, and map insertion are prepared before removing
live victims. Failed preparation rolls back the new metadata. Stable list
iterators identify victims across a possible hash-table rehash. The final commit
does not allocate; it restores the budget before the next command executes.

Capacity errors return `-OOM`, keep the connection open, and preserve the old
live value and TTL. Actual allocator exhaustion remains a process-resource
failure; it does not have the same server availability guarantee.

## Tests

| Build | Result | CTest time |
| --- | --- | --- |
| Release | All four CTest targets pass | 0.75 s |
| AddressSanitizer + UBSan | All four pass, no sanitizer diagnostics | 1.12 s |

The targets cover expiration, memory behavior, allocation failures, and 11 TCP
integration scenarios. Checks include exact-fit writes, shrinking replacements,
rejected replacements preserving TTLs, empty/binary data, arithmetic overflow,
LRU order, target exclusion, multiple victims, timer cleanup, and repeated updates.

Budget tests verify both exactly 128 victims and rejection when more are needed.
Another case shares the budget between 10 expired entries and 118 selected live
victims: the SET fails without live eviction, then a subsequent SET succeeds
because it starts with a fresh budget. Allocation fault injection checks each
preparation step for both new writes and replacements.

Recorded output: [Release](prototype/results/memory-release-tests.txt) and
[sanitizers](prototype/results/memory-sanitizer-tests.txt).

## Pressure Experiment

The [harness](prototype/tools/memory_pressure.cpp) runs a fresh store process for
each policy/workload pair. Each uses a 256 KiB budget, 2,000 possible 32-byte keys,
and 50,000 commands: approximately 90% GET, 9% SET, 1% DEL, with a fixed random
seed. Normal values are 1 KiB. Setup attempts to insert all 2,000 keys, so the
store starts under pressure. Counts below exclude setup.

- **Uniform:** uniform access; half the keys have TTLs beyond the measured window.
- **Skewed:** 85% of accesses choose from 64 hot keys, the rest from all keys.
- **Burst:** initial keys share a deadline at simulated 10 ms; later TTL SETs also
  expire. Cleanup runs every 32 commands, removing at most 128 due entries.
- **Large:** SETs use 64 or 192 KiB values, exercising multi-victim admission.

Expiration time advances by 1 ms per 1,000 commands for repeatability. Operation
latency uses the real monotonic clock. Every operation checks the accounted-byte,
metadata-count, and work-budget invariants.

| Policy | Workload | SET accepted / rejected | Evictions | GET hit rate | Operation p99 |
| --- | --- | --- | --- | --- | --- |
| noeviction | Uniform | 500 / 4,109 | 0 | 10.1% | 1.740 us |
| LRU | Uniform | 4,609 / 0 | 4,075 | 9.7% | 0.354 us |
| noeviction | Skewed | 1,730 / 2,711 | 0 | 35.6% | 1.761 us |
| LRU | Skewed | 4,441 / 0 | 662 | 76.2% | 0.273 us |
| noeviction | Burst | 782 / 3,827 | 0 | 9.8% | 1.737 us |
| LRU | Burst | 4,609 / 0 | 4,067 | 9.7% | 0.395 us |
| noeviction | Large | 1 / 4,608 | 0 | 8.9% | 1.744 us |
| LRU | Large | 4,604 / 5 | 4,795 | 0.09% | 28.381 us |

All accounted peaks were at or below 262,144 bytes. Large-value LRU writes reached
the 128-entry work cap and returned five work-limit errors. LRU retained hot keys
more effectively in this skewed run, but large values left room for very few keys.
Policies also start with different admitted keys after setup, so these results
describe the whole workload rather than an isolated policy cost comparison.

Peak harness RSS ranged from about 5.42 to 5.83 MiB, including the store, runtime,
keys, test payloads, and measurement storage. Empty harness RSS was about 4.7 MiB.
RSS therefore differs substantially from the logical budget. Neither the fixed
metadata allowances nor the ten-million-key capacity estimate are validated by
this small experiment.

Recorded on 2026-10-19, Linux x86-64, i5-1135G7, eight logical CPUs, approximately
7.48 GiB RAM, GCC 16.2.1 Release. [Raw JSON](prototype/results/memory-pressure.json)
records the base revision, working-tree changes, source/binary hashes, configuration,
all counters, and RSS. It measures the memory implementation on top of `0b6e897`,
not that commit alone. This is a direct store experiment with simulated expiry
time, not TCP latency or validation of the full workload targets.

## Reproduce

From `redis/prototype/`:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build -V
./build/memory-pressure noeviction uniform
./build/memory-pressure allkeys-lru uniform
```

Repeat with `skewed`, `burst`, and `large` for both policies. Each invocation emits
JSON. For sanitizers, configure `build-asan` with `-DCMAKE_BUILD_TYPE=Debug
-DSANITIZE=ON`, build it, and run `ctest --test-dir build-asan -V`.
