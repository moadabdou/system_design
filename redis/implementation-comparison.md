# Step 7: Compare with Redis

Status: outline; perform the comparison after writing our own design predictions.

Choose and record a Redis release and source commit before investigating details.
Use official documentation and selected source paths to compare:

- Connection handling, event registration, parsing, and command execution.
- Key storage, expiration, memory accounting, and eviction.
- Persistence, write acknowledgment, and restart recovery.
- Replication, failover, partition behavior, and sharding.

For each topic, record our prediction, the observed Redis behavior, the evidence,
and the consequence for performance or correctness. Label explanations of design
intent as hypotheses unless a source supports them.

Deliverable: a concise comparison table with version-specific references, key
differences, and unresolved questions. Revisit our design where the evidence
changes an assumption.
