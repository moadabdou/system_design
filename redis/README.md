# Redis — System Design Case Study

The current implementation covers the minimal single-threaded TCP store and
key expiration: GET, SET (optionally PX), and DEL. Dataset memory limits,
eviction, persistence, replication, and sharding remain future work.

## Study Documents

Implementation: [C++ prototype and run instructions](prototype/README.md) ·
[Implementation and test report](implementation-report.md) ·
[Expiration implementation and results](expiration-report.md) ·
[Poll and epoll flags](epoll-flags.md)

1. [Scope](scope.md)
2. Steps 2–3: [Requirements and capacity estimates](requirements-and-capacity.md)
3. Step 4: [Minimal design](minimal-design.md)
4. Step 5: [Design trade-offs](design-tradeoffs.md) — outline
5. Step 6: [Failure scenarios](failure-scenarios.md) — outline
6. Step 7: [Comparison with Redis](implementation-comparison.md) — outline
7. Step 8: [Validation experiments](validation-experiments.md) — outline

## 1. Overview
- What is Redis? In-memory data store, single-threaded event loop.
- Why study it? Caching, session store, leaderboards, queues, pub/sub, rate limiting.

## 2. Architecture
- Single-threaded I/O multiplexing + background persistence threads.
- RESP protocol, event loop, commands dispatch.
- Memory model: dict + expires dict, eviction policies.

## 3. Core Data Structures
- String, List, Hash, Set, Sorted Set, Stream, Bitmap, HyperLogLog, Geo.
- When to use each + time complexity.

## 4. Persistence
- RDB (snapshotting) vs AOF (append-only log).
- Trade-offs: durability vs performance, rewrite / fsync policies.

## 5. Replication & HA
- Primary-replica async replication, Sentinel for failover.
- CAP trade-offs, split-brain risk.

## 6. Sharding / Cluster
- Hash slots (16384), gossip, client redirection (MOVED/ASK).
- Rebalancing, multi-key operation limits.

## 7. Common System Design Patterns
- Cache-aside, write-through, write-behind.
- TTL + jitter, stampede protection (singleflight, probabilistic early refresh).
- Distributed lock (SET NX PX + Redlock caveats), idempotency.
- Rate limiting (sliding window with ZSET), leaderboard (ZSET), feed / timeline.

## 8. Scalability & Ops
- Eviction: noeviction, allkeys-lru, volatile-lru, etc.
- Memory fragmentation, big-key / hot-key problems.
- Monitoring: INFO, SLOWLOG, latency, hit ratio.

## 9. Exercises
- [ ] Design a URL shortener cache layer with Redis.
- [ ] Design rate limiter for 10k rps.
- [ ] Design leaderboard for 1M players.
- [ ] Compare Redis vs Memcached vs Dragonfly.

## 10. References
- redis.io/docs
- Redis design notes, antirez blog.




5. Investigate one concern at a time. Add concurrency, expiration, memory limits, persistence, replication, and sharding. For each, ask: What problem does this solve, what alternatives exist, and what does it cost?
6. Test failure scenarios. Walk through a crash, a slow replica, a network partition, a full disk, and memory exhaustion. State what clients observe and whether data can be lost.
7. Compare with the actual implementation. Read official documentation and selected source code. Record where Redis makes different choices and why. Separate documented facts from your own hypotheses.
8. Validate with a small experiment. Benchmark a workload, simulate a failure, or implement a small component. Check whether your assumptions hold.
