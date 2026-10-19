# Requirements and Capacity Estimates

This document combines steps 2 and 3 of the study process: define requirements,
then estimate the resources needed to meet them. See [scope.md](scope.md) for the
study boundaries.

The numbers below are initial study assumptions, not measured Redis performance
or guarantees. Revise them as experiments provide evidence.

These requirements cover the full study. The first prototype implements only
GET, SET, and DEL over a single-threaded TCP server. The current prototype also
supports TTLs through SET PX, a logical dataset budget, and rejection or LRU
eviction. Persistence, replication, and sharding are deferred to later phases.

## Functional Requirements

- `SET key value`: create or replace a string value, optionally with a TTL.
- `GET key`: return the current value or indicate that the key is absent.
- `DEL key`: remove a key; deleting an absent key is safe.
- Execute each command atomically on the node that owns the key.
- Treat expired keys as absent even if their memory has not yet been reclaimed.
- Enforce a memory limit with a defined eviction or write-rejection policy.
- Add persistence, replication, and sharding in the later study stages.

## Workload and Performance Targets

| Parameter | Initial assumption |
| --- | --- |
| Live keys | 10 million |
| Average key size | 32 bytes |
| Average value size | 1 KiB (1,024 bytes) |
| Maximum value size | 1 MiB; large values require separate testing |
| Steady request rate | 50,000 commands/second |
| Peak request rate | 100,000 commands/second |
| Command mix | 90% GET, 9% SET, 1% DEL |
| GET hit rate | 95% |
| Keys with TTL | 50% |
| Access distribution | Uniform baseline; skewed traffic tested separately |
| Latency target | p99 at or below 5 ms at peak load on a local network |

Measure latency at the client, including network time and server queueing. Start
with the average sizes above, then test large values, expiration bursts, and hot
keys separately.

## Durability and Availability Requirements

- **Initial in-memory stage:** a process crash may lose the entire dataset.
- **Persistence stage:** evaluate a target of at most one second of acknowledged
  write loss after a process crash; state the storage and failure assumptions
  required to meet it.
- **Replication stage:** target service recovery within 30 seconds of a primary
  failure in a three-node study deployment. Evaluate acknowledged-write loss
  separately; replication alone does not define a durability guarantee.
- **Partition behavior:** document which nodes may accept writes and what happens
  to conflicting or unreplicated writes. Choose the policy during replication
  design.
- **Recovery time:** measure restart time for the full dataset before choosing a
  recovery target.

## Memory Estimate

Use a provisional overhead allowance of 128 bytes per key for metadata, indexes,
and expiration bookkeeping. Actual overhead depends on the implementation and
must be measured.

The current prototype uses a separate [logical accounting model](memory-limits-design.md#what-we-count)
with explicit LRU/TTL key copies and 192/96-byte metadata allowances. The capacity
estimate below retains its original study assumptions; it has not been validated
against that implementation or measured at ten million keys.

```text
Bytes per entry = 32 + 1,024 + 128 = 1,184 bytes
Dataset memory = 10,000,000 × 1,184 = 11.84 GB ≈ 11.03 GiB
With 30% planning allowance = 11.84 × 1.30 = 15.39 GB ≈ 14.34 GiB
```

This allowance covers uncertainty and ordinary allocator overhead. Additional
memory is needed for the operating system, client buffers, replication buffers,
and persistence activity. Evaluate a 32 GiB machine as a starting candidate;
measure peak memory during snapshotting and log rewriting before accepting it.

## Network Estimate

At peak load, count key/value bytes before protocol headers and acknowledgments:

```text
GET commands = 100,000 × 90% = 90,000/second
Successful GETs = 90,000 × 95% = 85,500/second
SET commands = 100,000 × 9% = 9,000/second

Incoming key bytes = 100,000 × 32 = 3.20 MB/second
Incoming value bytes = 9,000 × 1,024 = 9.22 MB/second
Outgoing value bytes = 85,500 × 1,024 = 87.55 MB/second
```

Outgoing values alone consume about 700 Mb/second. Protocol overhead, replication,
and full synchronization add traffic, so evaluate a faster network link when
testing the peak workload and failure recovery.

## Persistence and Replication Estimate

```text
SET key/value bytes = 9,000 × (32 + 1,024) = 9.50 MB/second
DEL key bytes = 1,000 × 32 = 0.032 MB/second
Combined mutation payload ≈ 9.54 MB/second
One hour at sustained peak ≈ 34.3 GB of mutation payload
```

An append-only log adds command framing and expiration records; rewriting or
compaction changes disk usage. Replication sends mutation data to each replica
and must also support full dataset transfers. Size disks and replication buffers
after choosing retention, rewrite behavior, and tolerated replica downtime.

## Questions to Validate During Design

- Can one node meet the throughput and p99 latency targets at these value sizes?
- How much memory does an entry actually consume with and without a TTL?
- How do persistence and replication affect latency and peak memory?
- What happens when a hot key, slow client, or expiration burst dominates work?
- Which resource reaches its limit first: CPU, memory, network, or disk?
- At what measured limit should the design introduce sharding?
