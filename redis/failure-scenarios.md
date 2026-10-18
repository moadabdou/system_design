# Step 6: Test Failure Scenarios

Status: outline; analyze each scenario when the relevant component is designed.

For each failure, trace what happens before, during, and after recovery. Record
what clients observe, whether acknowledged writes survive, and whether retries
are safe.

| Scenario | Main question |
| --- | --- |
| Process crash | Which writes survive, and how does the server restart? |
| Slow or disconnected client | Can its buffers exhaust memory or delay other clients? |
| Memory exhaustion | Which operations fail, and does the server remain responsive? |
| Full disk or failed write | Does the server reject mutations, continue, or stop? |
| Slow or disconnected replica | How much data must be buffered, and how does it catch up? |
| Primary failure | Who chooses a replacement, and which acknowledged writes might be lost? |
| Network partition | Which nodes may accept writes, and how is divergence handled? |
| Interrupted shard migration | How do clients locate keys without losing writes? |

Deliverable: a failure matrix with expected behavior, recovery steps, and evidence
from selected experiments. Begin with the single-node failures; extend the matrix
as persistence, replication, and sharding are introduced.
