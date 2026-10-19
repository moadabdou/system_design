# Redis Case Study Scope

## Objective

Design a Redis-like in-memory key-value store, starting with one machine and
extending it with persistence, replication, and sharding. Explain the trade-offs
behind each decision before comparing the design with Redis itself.

This is an architecture study, not an attempt to implement all of Redis.

The first implementation covered step 4 of the study process: a minimal event-loop
prototype with GET, SET, and DEL. The current bounded-memory stage adds key
expiration; dataset memory limits and eviction are next. Persistence, replication,
and sharding stay in documentation until their individual study phases begin.

## In Scope

- Basic key-value operations: `GET`, `SET`, and `DEL`, initially with string values.
- Key expiration: TTL behavior and reclaiming expired data.
- Request handling: client connections, command parsing, and concurrency.
- Memory management: capacity estimates, memory limits, and eviction policies.
- Persistence: snapshots, append-only logs, recovery, and durability trade-offs.
- Replication: primary-replica design, replication lag, and failover.
- Sharding: key distribution, request routing, and moving data between nodes.
- Failure behavior: process crashes, memory exhaustion, disk failures, and network
  partitions.

## Out of Scope

- Full Redis command and data-type coverage.
- Modules, scripting, transactions, streams, and pub/sub.
- Production deployment automation and managed-service comparisons.
- Implementing a production-ready Redis replacement.
- Application designs such as rate limiters and leaderboards; these can become
  separate follow-up studies.

## Study Stages

1. **Single-node store:** define operations and semantics, estimate capacity, and
   draw the read and write paths.
2. **Bounded memory:** add expiration and eviction; explain what clients observe
   when memory is exhausted.
3. **Persistent store:** compare snapshots and logs; define recovery behavior and
   acceptable data loss.
4. **Replicated store:** add replicas and failover; examine stale reads and lost
   writes during failures.
5. **Sharded store:** distribute keys; examine routing, hot keys, rebalancing, and
   cross-shard operations.

## Working Method

For each stage:

1. Write down requirements, workload assumptions, and success criteria.
2. Propose a design before reading the corresponding Redis implementation.
3. Compare alternatives and explain the chosen trade-offs.
4. Walk through normal requests and failure scenarios.
5. Compare with official Redis documentation and selected source code, recording
   the Redis version studied and distinguishing facts from hypotheses.
6. Validate a key assumption with a small experiment or prototype where useful.

Latency, throughput, dataset size, value size, and durability targets are recorded
alongside the estimates in [Requirements and Capacity Estimates](requirements-and-capacity.md).
They are study assumptions, not Redis guarantees.

## Deliverables and Completion Criteria

- Requirements and capacity estimates with explicit assumptions.
- Architecture diagrams and read/write request flows.
- A decision log covering alternatives and trade-offs.
- A failure matrix describing client-visible behavior and possible data loss.
- Small experiments with reproducible steps and observations.
- A comparison with the selected Redis version, supported by references.

The study is complete when each in-scope concern has a reasoned design, its main
failure behavior is explained, and the comparison with Redis identifies the
important differences and remaining questions.
