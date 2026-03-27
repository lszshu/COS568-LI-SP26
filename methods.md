# Milestone 2 Implementation

## Goal

Milestone 2 requires a naive hybrid index that combines DynamicPGM and LIPP.
The intended behavior is:

1. Use LIPP as the main resident index for the bulk-loaded data.
2. Use DynamicPGM as a smaller write buffer for newly inserted keys.
3. During lookup, check the DynamicPGM buffer first, then fall back to LIPP.
4. When the buffer becomes large enough, flush the buffered keys into LIPP.

My implementation follows this design directly and does not attempt asynchronous
flush or concurrency optimizations. The objective of this version is correctness
and a clean baseline for later improvement.

## Core Data Structure

The hybrid index is implemented in `competitors/hybrid_pgm_lipp.h` as
`HybridPGMLIPP<KeyType>`.

It contains two internal components:

1. `LIPP<KeyType, uint64_t> lipp_`
   This stores the main body of the dataset and is built once from the bulk-load
   input.

2. `DynamicPGMIndex<KeyType, uint64_t, ...> buffer_`
   This acts as an in-memory insertion buffer. I use a DynamicPGM instance with
   `BranchingBinarySearch<0>` and error bound `128` for this buffer.

The hybrid index only supports unique keys and single-threaded execution in this
milestone version. This matches the existing benchmark assumptions for LIPP and
DynamicPGM in this repository.

## Build Phase

In `Build(...)`, all initial bulk-loaded data is converted into
`std::pair<KeyType, uint64_t>` format and loaded entirely into LIPP using
`lipp_.bulk_load(...)`.

The DynamicPGM buffer starts empty after build. This means the initial state is:

- LIPP contains all preloaded keys
- DynamicPGM contains no keys

This matches the hybrid design described in the assignment: bulk-loaded data is
placed into LIPP first, and only later inserts go into DynamicPGM.

## Lookup Path

`EqualityLookup(...)` is implemented as a two-stage lookup:

1. Search the DynamicPGM buffer first.
2. If the key is not found there, search LIPP.

This ordering is important because buffered keys have not yet been migrated into
LIPP. If lookup searched only LIPP, recently inserted keys would be invisible.

The lookup semantics are:

- If the key is found in the buffer, return the buffer value.
- Otherwise, query LIPP.
- If neither structure contains the key, return `util::OVERFLOW`.

This preserves correctness without modifying either underlying index.

## Insert Path

`Insert(...)` inserts every new key directly into the DynamicPGM buffer.

I also maintain a counter `buffer_item_count_` that tracks how many inserted
items are currently buffered. Once this count reaches the configured flush
threshold, the hybrid index triggers `flush_buffer()`.

In the current implementation, the default threshold is:

- `2^17 = 131072` buffered keys

This threshold can be overridden through benchmark parameters, but the default
Milestone 2 experiments use the fixed value above.

## Flush Strategy

The flush strategy is intentionally naive, as allowed by the Milestone 2
specification.

When a flush happens:

1. Iterate through all keys currently stored in DynamicPGM in sorted order.
2. Insert them one by one into LIPP using `lipp_.insert(...)`.
3. Reinitialize the DynamicPGM buffer as an empty index.
4. Reset the buffered item counter to zero.

Important properties of this design:

- No keys are dropped.
- No separate auxiliary structure beyond LIPP and DynamicPGM is used.
- Flush is synchronous and blocks normal progress during migration.
- Migration cost is high because every buffered key becomes an individual LIPP
  insertion.

This is exactly why this implementation should be viewed as a naive baseline
rather than a competitive final design.

## Range Query Support

Although the Milestone 2 experiments are mixed lookup/insert workloads rather
than range-query workloads, I still implemented `RangeQuery(...)` for interface
completeness.

The range query logic simply scans:

1. the LIPP iterator range
2. the DynamicPGM lower-bound iterator range

and sums values from both structures over the requested key interval.

## Size Accounting

The reported hybrid index size is:

- `lipp_.index_size() + buffer_.size_in_bytes()`

This reflects the size of both components currently present in memory.

Because most data remains in LIPP after build and because the buffer threshold
is relatively small compared with the dataset size, the hybrid index size in
practice stays very close to the LIPP size. This matches the observed benchmark
results.

## Benchmark Integration

To integrate the hybrid index into the existing benchmark framework, I added:

1. `benchmarks/benchmark_hybrid_pgm_lipp.h`
2. `benchmarks/benchmark_hybrid_pgm_lipp.cc`
3. `competitors/hybrid_pgm_lipp.h`

The benchmark wrapper simply runs:

- `benchmark.template Run<HybridPGMLIPP<uint64_t>>()`

I then registered the new index in:

- `benchmark.cc`
- `CMakeLists.txt`
- `scripts/create_minimal_cmake.sh`

The new benchmark name exposed to the CLI is:

- `HybridPGMLIPP`

So it can be invoked with:

```bash
./build/benchmark ... --only HybridPGMLIPP
```

## Milestone 2 Experiment Scripts

For the Milestone 2 experiments, I added a Facebook-only benchmark path because
the milestone only requires reporting the Facebook dataset.

The experiment scripts are:

1. `scripts/run_benchmarks_milestone2_fb_workload.sh`
   Runs one mixed workload for:
   - `LIPP`
   - `DynamicPGM`
   - `HybridPGMLIPP`

2. `scripts/run_benchmarks_milestone2_fb.sh`
   Runs both required mixed workloads:
   - 10% insert / 90% lookup
   - 90% insert / 10% lookup

3. `scripts/analysis_milestone2_fb.py`
   Selects the best DynamicPGM row by average mixed throughput and generates a
   compact summary plus four plots:
   - throughput for lookup-heavy workload
   - throughput for insert-heavy workload
   - size for lookup-heavy workload
   - size for insert-heavy workload

## Slurm Parallelization

I also added `jobs/run_milestone2_fb_array.slurm` to speed up the experiments
without changing benchmark semantics.

This script uses a Slurm array with two tasks:

- one task for the 10% insert workload
- one task for the 90% insert workload

Each task still runs the benchmark itself in single-threaded mode:

- `--cpus-per-task=1`
- `OMP_NUM_THREADS=1`

Therefore, the benchmark logic for each workload is exactly the same as the
single-CPU version. The only speedup comes from running the two independent
workloads at the same time on different CPU resources.

## Current Limitations

This Milestone 2 implementation is intentionally simple and has several known
limitations:

1. Flush is synchronous.
2. Flush inserts buffered keys into LIPP one at a time.
3. No overlap exists between flush and foreground operations.
4. The design is single-threaded only.
5. The hybrid index size remains close to LIPP size because LIPP holds almost
   all bulk-loaded data.

These limitations are acceptable for Milestone 2. They also define the natural
next steps for Milestone 3:

1. asynchronous flush
2. double-buffered DynamicPGM
3. better flush-threshold tuning
4. possible LIPP-side optimization for migration cost

## Summary

In short, my Milestone 2 hybrid index is a naive buffered design:

- LIPP stores the main dataset
- DynamicPGM stores recent inserts
- lookup checks DynamicPGM first, then LIPP
- inserts go into DynamicPGM
- once the buffer reaches a threshold, its keys are flushed synchronously into
  LIPP

This implementation satisfies the milestone requirement for a correct hybrid
baseline and provides a clean starting point for more advanced Milestone 3
optimizations.
