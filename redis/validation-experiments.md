# Step 8: Validate Assumptions

Status: outline; small experiments can accompany every earlier step.

Completed observations: [minimal baseline](implementation-report.md#initial-performance-and-memory-observations)
and [expiration burst](expiration-report.md#expiration-burst-experiment), plus
[memory pressure](memory-limits-report.md).

Start with a single-threaded, nonblocking TCP prototype supporting GET, SET, and
DEL. Use it to check event-loop behavior before adding storage and distribution
features.

| Phase | Initial experiment |
| --- | --- |
| Minimal store | Connect several clients; split commands across writes, pipeline requests, and keep one client idle or slow |
| Capacity | Measure throughput, p99 latency, and memory per key for the assumed workload |
| Bounded memory | Expire many keys and fill the memory budget; observe correctness and latency |
| Persistence | Terminate the process during writes; compare acknowledged data with recovered data |
| Replication | Disconnect a replica or stop the primary; measure lag, recovery time, and lost writes |
| Sharding | Move keys under load; observe routing, errors, and missing or stale values |

Before each experiment, state a prediction and a pass/fail criterion. Record the
code revision, machine resources, configuration, workload, reproduction steps,
and results. Distinguish process-crash tests from machine or power-loss tests.

Deliverable: reproducible observations linked to the assumption or decision they
test. Compare measurements with [our targets](requirements-and-capacity.md), and
revise estimates when evidence warrants it.
