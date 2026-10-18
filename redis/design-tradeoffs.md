# Step 5: Investigate Design Trade-offs

Status: outline to expand after the [minimal design](minimal-design.md) prototype.

Extend the design one concern at a time. For each concern, describe the problem,
compare alternatives, choose an approach, and record its costs and guarantees.

| Concern | Questions to explore |
| --- | --- |
| Concurrency | What work can block the event loop? When would additional threads help? |
| Expiration | How do we detect expired keys and reclaim untouched entries without long pauses? |
| Memory limits | How do we account for memory? When should we evict keys or reject writes? |
| Persistence | Snapshots, logs, or both? When do we acknowledge writes, and what can a crash lose? |
| Replication | How do replicas synchronize and catch up? What happens to reads and writes during failover? |
| Sharding | How do we assign and move keys, route requests, and handle hot keys? |

Follow the progression in [scope.md](scope.md): bounded memory, persistence,
replication, then sharding. Revisit concurrency as each addition creates new work.

Deliverable: a short decision record for each concern, with an updated diagram
where the architecture changes. Decisions remain open until that phase is studied.
