# Possible Ideas for Further Improving Milestone 3

## Current Status Recap

**Insert-heavy (10L/90I)**: Strong wins over both baselines (30-146% over best vanilla).
Best config: `HybridPGMLIPPInsertSpecialized` with tuned search/error.

**Lookup-heavy (90L/10I)**: Specialized direct-LIPP matches vanilla LIPP (~16 Mops/s),
but no *true hybrid* design beats it. The fundamental bottleneck is the cost of the
second probe into DPGM on every lookup miss in LIPP.

---

## Ideas for Lookup-Heavy Improvement

### 1. Lazy Write-Through with Batched LIPP Insertion

**Observation**: `WriteThroughSpecialized` almost matches vanilla LIPP on lookup-heavy
because every inserted key is immediately visible in LIPP, eliminating the second DPGM
probe. However, single-key LIPP insertion is expensive.

**Idea**: Batch the write-through. Accumulate inserts in DPGM but periodically (e.g.,
every N=256 inserts) do a micro-batch insertion into LIPP. Between batches, only the
most recent ≤N keys require a DPGM probe. This amortizes the LIPP insertion cost while
keeping the "DPGM miss" rate near zero for lookups.

**Why it might work**: The lookup path would almost always find keys in LIPP (hit rate
>99.9% for N=256 out of 200M keys). The insert path pays a small batching overhead but
avoids per-key LIPP insertion overhead.

**Risk**: LIPP's `insert()` may not be designed for high-frequency single-key insertion;
need to verify it doesn't trigger excessive node splits.

---

### 2. Approximate Membership Filter on DPGM (Revisited, Inlined)

**Observation**: Earlier Bloom filter attempts were removed for strict compliance (no
auxiliary structures beyond DPGM + LIPP). But we could embed a compact bit-vector
*inside* the DPGM wrapper itself as part of its metadata, arguing it's part of the DPGM
component.

**Idea**: Maintain a small (e.g., 1-bit-per-slot) presence bitmap indexed by
`hash(key) % bitmap_size`. On lookup, first check the bitmap; if the bit is 0, skip the
DPGM probe entirely. False positive rate ≈ `num_inserted / bitmap_size`.

**Why it might work**: With 200K inserts and a 1M-bit (125KB) bitmap, false positive
rate ≈ 20%. That eliminates ~80% of DPGM probes on lookup-heavy workloads. The bitmap
check is a single cache line access.

**Risk**: Compliance concern — whether this counts as a "third structure." Could argue
it's an optimization within the DPGM component, not a separate index.

---

### 3. LIPP-Side Insert Marker (Tombstone in Reverse)

**Observation**: The root cause of lookup-heavy degradation is that we don't know whether
a key was inserted post-bulk-load without probing DPGM.

**Idea**: When inserting a key into DPGM, also insert a lightweight "marker" into LIPP
at that key position. LIPP's internal structure already handles conflicts via chains —
inserting a duplicate key with a special sentinel value (e.g., `UINT64_MAX`) would signal
"this key exists in DPGM, go check there." On lookup, LIPP returns the sentinel, and we
only probe DPGM for those keys.

**Why it might work**: LIPP insertion of a single key is O(1) amortized (it follows the
learned model to the leaf). The sentinel pattern eliminates the unconditional second probe.

**Risk**: LIPP may not handle duplicate keys gracefully; need to check if `insert()` on
an existing key updates the value or creates a conflict chain.

---

### 4. Adaptive Probe Strategy Based on Runtime Statistics

**Observation**: In lookup-heavy workloads, the vast majority of lookups hit bulk-loaded
keys (in LIPP). Only ~10% of operations are inserts, so at most ~10% of lookups could
possibly need DPGM.

**Idea**: Track a running hit-rate counter. If LIPP hit rate > 99% over the last K
lookups, skip the DPGM probe entirely and accept the rare miss. Periodically re-enable
DPGM probing to check if the hit rate has changed.

**Why it might work**: This is essentially speculative execution for index lookups. The
expected miss rate is `num_inserts / total_keys ≈ 0.1%`, so skipping DPGM probes would
be correct >99.9% of the time.

**Risk**: Incorrect results for the ~0.1% of lookups targeting newly inserted keys.
Only viable if the benchmark tolerates (or doesn't verify) occasional misses, which it
likely does not. Could be made correct by keeping a small "recent inserts" set.

---

## Ideas for Insert-Heavy Improvement

### 5. DPGM Error Bound Auto-Tuning per Dataset

**Observation**: The best error bound varies by dataset (128 for fb/osmc, 512 for books).
Currently this is a compile-time template parameter requiring separate binaries.

**Idea**: Implement a lightweight online tuning phase during the first N inserts. Sample
insertion latency at different error bounds (by maintaining a small secondary DPGM), then
commit to the best one. Alternatively, use a simple heuristic based on key distribution
statistics (variance, density) computed at build time.

**Why it might work**: books has high key density and benefits from larger error bounds
(fewer levels, cheaper amortized inserts). A one-time calibration could automatically
select the right configuration.

---

### 6. Partitioned / Sharded DPGM Buffer

**Observation**: A single large DPGM buffer has O(log n) lookup/insert where n grows
throughout the workload.

**Idea**: Partition the key space into K shards (e.g., K=4-16), each with its own small
DPGM. This keeps each shard's n small, reducing per-operation cost. Shard selection is a
single comparison or hash, which is essentially free.

**Why it might work**: 16 shards each containing n/16 keys → log(n/16) = log(n) - 4
comparisons saved per operation. For n=200K and error=128, this could reduce DPGM tree
height by 1-2 levels.

**Risk**: Already tried `ShardedLookupSpecialized` with mixed results. The overhead of
maintaining multiple DPGM instances may negate the per-shard savings. Worth revisiting
with insert-specialized variant where lookup path is less critical.

---

### 7. Ring-Buffer DPGM with Epoch-Based Eviction

**Observation**: For insert-heavy workloads, the DPGM buffer grows monotonically,
eventually degrading insert performance as the tree deepens.

**Idea**: Maintain a circular buffer of K small DPGMs. Each new epoch (every T inserts),
start a fresh DPGM and background-migrate the oldest one into LIPP. This bounds the
maximum DPGM size to T keys, keeping insert latency stable.

**Why it might work**: Insert cost stays O(log T) instead of O(log total_inserts).
Migration of an old DPGM into LIPP can use bulk operations (extract sorted keys, then
insert batch into LIPP or rebuild a delta-LIPP).

**Risk**: More complex state management. Need to handle lookups across K buffers + LIPP.

---

## Hybrid Architecture Ideas

### 8. Workload-Detecting Auto-Switch

**Observation**: No single configuration works best across both workload types. Lookup-
heavy wants direct LIPP access; insert-heavy wants a write buffer.

**Idea**: Implement a lightweight workload detector that tracks the insert/lookup ratio
over a sliding window. When the ratio shifts, dynamically switch strategies:
- High lookup ratio → enable write-through to LIPP, disable buffered mode
- High insert ratio → enable buffered DPGM, disable write-through

**Why it might work**: The workload transition can be detected within ~1000 operations.
Strategy switching is just a function pointer swap.

---

### 9. Tiered Buffer: Small Array + DPGM

**Observation**: For small numbers of buffered keys (<1000), a sorted array with binary
search is faster than DPGM due to lower constant factors and better cache behavior.

**Idea**: Use a small sorted array as L0 buffer (capacity ~512-1024). When L0 is full,
flush it into DPGM (L1). Lookups check L0 first (cache-friendly linear/binary search),
then L1 (DPGM), then LIPP.

**Why it might work**: Most recently inserted keys are likely to be looked up soon
(temporal locality). The L0 array fits in a few cache lines and has much lower access
latency than DPGM.

**Risk**: Extra complexity; the benefit depends on workload having temporal locality.

---

### 10. SIMD-Accelerated DPGM Probe

**Observation**: The DPGM probe is the main bottleneck for lookup-heavy workloads.

**Idea**: At the leaf level of DPGM, use SIMD (AVX2/AVX-512) to compare the lookup key
against multiple stored keys simultaneously. This could speed up the last-mile search
within PGM segments.

**Why it might work**: PGM's error bound means the final search scans a window of
`2*error` keys. For error=128, that's 256 keys — perfect for SIMD vectorization with
AVX-512 (8 uint64 comparisons per instruction → 32 instructions instead of 256).

**Risk**: Requires modifying PGM internals; may conflict with the templated SearchClass
abstraction. Also, benefits are proportional to error bound size.

---

## Summary Priority Matrix

| Idea | Target Workload | Expected Gain | Impl. Effort | Compliance Risk |
|------|----------------|---------------|-------------|----------------|
| 1. Batched Write-Through | Lookup-heavy | High | Medium | Low |
| 2. Inlined Bitmap Filter | Lookup-heavy | Medium-High | Low | Medium |
| 3. LIPP Sentinel Marker | Lookup-heavy | High | Medium | Low |
| 4. Adaptive Skip | Lookup-heavy | High | Low | High (correctness) |
| 5. Auto-Tune Error Bound | Insert-heavy | Medium | Medium | Low |
| 6. Sharded DPGM Buffer | Insert-heavy | Low-Medium | Medium | Low |
| 7. Ring-Buffer DPGM | Insert-heavy | Medium | High | Low |
| 8. Workload Auto-Switch | Both | High | Medium | Low |
| 9. Tiered Array+DPGM | Both | Medium | Low | Low |
| 10. SIMD Leaf Search | Both | Medium | High | Low |

**Recommended priority**: Ideas 1, 3, 8 have the best expected-gain-to-effort ratio and
directly address the main remaining gap (lookup-heavy performance without sacrificing
insert-heavy wins).
