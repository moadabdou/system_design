# Memory Limits and Eviction

Implemented for issue #3. See [test and pressure results](memory-limits-report.md).

## Configuration

| Option | Default | Purpose |
| --- | --- | --- |
| `--memory BYTES` | 64 MiB | Dataset budget in positive decimal bytes |
| `--policy POLICY` | `noeviction` | Reject writes, or use `allkeys-lru` |

## What We Count

For a key of K bytes and a value of V bytes:

```text
entry bytes = 2*K + V + 192
with TTL: add K + 96
used bytes = sum of all stored entries
```

This includes key copies and estimated metadata for the hash table, LRU list,
and TTL index. Both policies use the same accounting. Expired entries count
until removed. The fixed overheads are estimates to check with measurements.

Client buffers, temporary copies, and allocator overhead need extra memory.
**The dataset budget is not a process RAM limit.**

## When a SET Fits

```text
projected bytes = used bytes - old entry bytes + new entry bytes
```

1. Validate the command and TTL before changing live data.
2. If the new entry alone exceeds the budget, reject it.
3. If the projected size fits, write it. Smaller or equal-sized replacements
   can succeed even when the store is full.
4. Otherwise, reclaim expired entries first.
5. If space is still short, reject under `noeviction`, or select LRU victims
   under `allkeys-lru`.

Reclaim or select at most **128 entries per SET**. If that is not enough, reject
with a work-limit error. This is separate from periodic expiration cleanup.

Prepare the new value and metadata before removing live victims. A rejected
SET keeps the old value, TTL, and recency, and evicts no live keys. Expired keys
removed while checking space stay removed. After every command, counted bytes
must stay within the budget.

## Eviction Choice: Least Recently Used (LRU)

Use a linked list: most recently used at the front, least recent at the back.
Successful GET hits and SETs move a key to the front. Misses and failed writes
do not. Evict from the back, skipping the key being replaced.

### Keeping the Order

Each hash-table entry stores an iterator to its node in a doubly linked list.
After finding a key, we can move its existing node to the front in O(1), without
allocating a new node or searching the list.

```text
Most recent                 Least recent
    C <-> B <-> A

GET A succeeds:
    A <-> C <-> B

SET D needs room for one victim:
    D <-> A <-> C      (B was evicted)
```

Recency follows the order commands execute on the server. A successful SET
counts as use even if the client never reads that key afterward.

### Making Room for a Write

Only SET needs eviction, and only when its projected size exceeds the budget.

1. Calculate the shortage: `projected bytes - memory limit`.
2. Reclaim due TTL entries first, within the shared 128-entry work budget.
3. If space is still short, walk backward from the LRU tail. Skip the key being
   replaced and collect candidate keys until their charges cover the shortage.
4. If the remaining work budget cannot cover the shortage, reject the SET.
   Selected live keys remain untouched.
5. Prepare the new value and all metadata. If preparation fails, discard that
   preparation and keep the live entries.
6. Complete the write and remove the selected victims without further allocation.
   Each removal deletes the value, LRU node, and TTL timer, then subtracts its
   charge. Finish within the dataset budget before processing another command.

For example, a 3 KiB shortage may require two 2 KiB victims. We free whole
entries, so freeing slightly more than needed is normal. Their counted sizes
include metadata and key copies, not just value bytes.

The 128-entry cap covers expired entries reclaimed plus candidates selected
for that SET. It limits work by entry count; it does not guarantee a fixed
execution time. A large write may fail even when enough space could be freed
by evicting more than 128 entries.

Both TTL and permanent keys can be evicted. A TTL does not protect a key from
eviction. The client receives no eviction notification; a later GET returns
null. Removing entries frees allocator memory for reuse, but process RSS may
stay unchanged.

We choose exact LRU for predictable behavior and straightforward tests. It costs
extra list memory and updates on access. FIFO ignores recent use; random eviction
can remove hot keys. `noeviction` remains the default to avoid automatic data loss.

## Client Behavior

| Situation | Result |
| --- | --- |
| SET fits | `+OK` |
| Entry exceeds the entire budget | `-OOM entry exceeds memory limit` |
| Full under `noeviction` | `-OOM memory limit reached` |
| Cleanup/eviction work limit reached | `-OOM eviction work limit reached` |
| GET or DEL at capacity | Works normally |
| GET for an evicted key | Null, as for a missing key |

Capacity errors keep the connection open.

## Checks Before Completion

- Verify accounting after SET, replacement, DEL, expiration, and eviction.
- Verify failed replacements preserve the value and TTL.
- Test LRU order, exact-fit writes, and the 128-entry work limit.
- Test both policies through TCP; run Release and sanitizer tests.
- Measure rejected writes, evictions, hit rate, p99 latency, and process RSS
  under memory pressure. Compare RSS with counted bytes.
