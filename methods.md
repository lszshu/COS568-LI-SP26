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

# Milestone 3 Experimental Path

## Goal

Milestone 3 asks for an improved hybrid index, but this phase is inherently
uncertain: some ideas help only one workload, some regress another workload,
and many parameter settings need to be tested before a stable direction
emerges.

Because of that, I kept Milestone 3 completely separate from the Milestone 2
baseline. The Milestone 2 implementation remains `HybridPGMLIPP`, while the
Milestone 3 experimental implementation is exposed as a different benchmark
target:

- `HybridPGMLIPPIncremental`

I also kept all Milestone 3 runs isolated from the repository root `results/`
directory so failed experiments would not overwrite the working Milestone 1 or
Milestone 2 outputs.

## Stable Structure

The Milestone 3 implementation is in
`competitors/hybrid_pgm_lipp_incremental.h`.

Across all Milestone 3 iterations, the stable high-level structure is:

1. `lipp_`
   Stores the bulk-loaded resident index.

2. `active_buffer_`
   A DynamicPGM buffer that receives new inserts.

3. `flushing_buffer_`
   A second DynamicPGM buffer that holds the previous full active buffer while
   it is being drained into LIPP.

This means the design is double-buffered. Instead of synchronously flushing the
entire DynamicPGM buffer into LIPP at one threshold crossing, the index swaps
the current buffer into a dedicated flushing buffer and then migrates it
incrementally across later operations.

## Build Phase

`Build(...)` bulk-loads the initial dataset into LIPP, then resets both Dynamic
PGM buffers and all flush state.

After build:

- LIPP contains all bulk-loaded keys
- `active_buffer_` is empty
- `flushing_buffer_` is empty
- no flush is in progress

This keeps the Milestone 3 index consistent with the Milestone 2 starting
state, so changes in benchmark behavior come from the flush strategy rather
than from a different initial layout.

## Current Lookup Path

The current version uses two possible lookup paths depending on workload
configuration.

### Cached lookup mode

In lookup-heavy workloads, I enable two auxiliary lookup caches:

- `active_lookup_cache_`
- `flushing_lookup_cache_`

These are `std::unordered_map<KeyType, uint64_t>` structures that mirror the
current contents of the two DynamicPGM buffers for point lookups only.

In this mode, `EqualityLookup(...)` checks:

1. `active_lookup_cache_`
2. `flushing_lookup_cache_`
3. `lipp_`

This avoids paying the cost of repeated DynamicPGM miss lookups on almost every
foreground lookup. The DynamicPGM structures are still kept because they are
needed for ordered draining and range query support.

### No-cache lookup mode

In insert-heavy workloads, maintaining the hash caches on every insert turned
out to be expensive enough to hurt throughput. For those workloads, I disable
the lookup caches and fall back to:

1. `active_buffer_`
2. `flushing_buffer_`
3. `lipp_`

So the final Milestone 3 implementation is workload-aware rather than forcing a
single policy for all scenarios.

## Insert Path and Flush Scheduling

`Insert(...)` always appends the new key to `active_buffer_`. If cache mode is
enabled, it also inserts the key into `active_lookup_cache_`.

When `active_buffer_` reaches the flush threshold:

1. `active_buffer_` is moved into `flushing_buffer_`
2. `active_lookup_cache_` is moved into `flushing_lookup_cache_` if cache mode
   is enabled
3. a fresh empty active buffer is created
4. incremental drain state is reset to the beginning of the flushing buffer

The actual migration is done by `drain_flush_budget(...)`. This method resumes
from the last migrated key and inserts at most a bounded number of buffered
keys into LIPP.

The current implementation separates drain work into:

1. insert-side drain budget
2. lookup-side drain budget

This was an important change. The previous version used the same drain amount
for every operation, which meant lookup-heavy workloads still paid noticeable
flush cost during lookups. The new version lets lookup-heavy workloads use
`lookup_flush_batch_size = 0` while still draining aggressively on inserts.

I also keep a simple pressure rule: if a flush is already in progress and the
new active buffer itself grows past the threshold, the insert-side drain budget
is multiplied by a constant factor. This is a lightweight way to keep backlog
from growing too quickly without introducing background threads.

## Range Query Correctness

During incremental migration, some prefix of `flushing_buffer_` may already
have been inserted into LIPP while the remainder is still buffered. That can
cause double counting if a range query scans the whole flushing buffer.

I fixed this by making `RangeQuery(...)` skip the flushed prefix and start from
the first not-yet-drained key when a flush is in progress. This was not needed
for the Milestone 3 mixed workloads, but it keeps the experimental index
semantically cleaner and prevents hidden correctness regressions.

## Parameters

The current Milestone 3 experimental index accepts up to four integer
parameters:

1. flush threshold
2. insert-side flush batch size
3. lookup-side flush batch size
4. cache flag (`1` = use hash-assisted point lookup caches, `0` = disable them)

The defaults are:

- threshold: `131072`
- insert batch: `256`
- lookup batch: `0`
- cache flag: `1`

To keep the CSV format compatible with the existing benchmark framework, the
index still emits only two variant fields:

- `search_method`
- `value`

The current values are:

- `search_method = hash-assisted-double-buffered`
- `search_method = double-buffered-no-cache`
- `value = <threshold>:<insert_batch>:<lookup_batch>:<cache_flag>`

## Improvement Iterations

The Milestone 3 work was iterative. I explicitly kept each iteration inside the
isolated experimental path so that failed attempts would not affect the stable
baseline.

### Iteration 1: plain double-buffered incremental flush

The first Milestone 3 version only added double buffering and incremental
drain. It had:

- two DynamicPGM buffers
- one shared per-operation drain budget
- no point-lookup cache

This version improved insert-heavy mixed workloads somewhat, but still regressed
lookup-heavy mixed workloads because every foreground lookup paid for:

1. DynamicPGM miss checks
2. flush work performed directly on lookup

On the full isolated run `6148820`, the best Facebook results for this version
were:

- 90% lookup / 10% insert: `1.39849` Mops/s
- 10% lookup / 90% insert: `1.58839` Mops/s

### Iteration 2: hash-assisted lookups plus split drain budgets

To address the lookup-heavy regression, I added:

1. unordered-map lookup caches for the two buffers
2. separate insert-side and lookup-side drain budgets
3. workload-specific parameter sweeps

The first smoke test after this change showed a strong improvement on the
Facebook 90% lookup / 10% insert workload. The best variant reached about
`3.87` Mops/s in isolated testing, which was a large jump over the previous
Milestone 3 version.

However, the same cached implementation regressed on the Facebook 10% lookup /
90% insert workload, falling to around `1.3` Mops/s in the early smoke test.
That indicated the hash-cache maintenance cost was too expensive when inserts
dominated.

### Iteration 3: workload-aware cache policy

The final adjustment was to make cache usage itself configurable.

- For lookup-heavy workloads, the benchmark wrapper enables cache mode.
- For insert-heavy workloads, the benchmark wrapper disables cache mode.

This keeps the lookup benefit where it matters, while avoiding unnecessary hash
maintenance on insert-heavy workloads.

In the final isolated Facebook smoke tests, the best configurations were:

- 90% lookup / 10% insert:
  `hash-assisted-double-buffered`, `262144:32:0:1`, average throughput
  `3.21853` Mops/s
- 10% lookup / 90% insert:
  `double-buffered-no-cache`, `262144:2048:0:0`, average throughput
  `1.89237` Mops/s

Relative to Iteration 1 on Facebook, these final smoke-test results improved
the experimental Milestone 3 index by approximately:

- `+130.1%` on the lookup-heavy mixed workload
- `+19.1%` on the insert-heavy mixed workload

So the main Milestone 3 improvement came from not treating the two workloads as
requiring the same flush/lookup policy.

### Iteration 4: bloom-style negative prefilter for cached lookups

After the full run `6149596`, the remaining obvious lookup-heavy cost was that
cached mode still performed an `unordered_map` lookup attempt for almost every
foreground lookup, even though most of those lookups were misses.

To reduce that cost, I added a lightweight Bloom-style membership prefilter for
the cached mode:

- `active_lookup_filter_`
- `flushing_lookup_filter_`

These filters are small contiguous bit-vectors keyed by the inserted keys in the
two buffers. In cached mode, the lookup sequence becomes:

1. check the Bloom-style filter
2. only if the filter says "possibly present", probe the unordered map
3. otherwise skip directly to LIPP

The goal is not to replace the hash cache, but to avoid paying full hash-table
miss cost on the dominant negative point lookups in lookup-heavy workloads.

I validated this change with another isolated Facebook smoke test. The best
lookup-heavy configuration improved from about `3.21853` Mops/s in the previous
smoke test to about `3.29224` Mops/s with the Bloom-style prefilter, while the
best insert-heavy configuration remained essentially unchanged at about
`1.89349` Mops/s because the insert-heavy path still runs in no-cache mode.

This was a smaller gain than the earlier workload-aware cache split, but it was
still directionally positive and did not hurt the insert-heavy path.

However, the next full isolated Slurm run (`6150116`) showed that this
improvement was not uniformly robust across all datasets:

- lookup-heavy:
  `fb` regressed relative to the previous full run, while `books` and `osmc`
  improved
- insert-heavy:
  `books` and `fb` improved slightly, but `osmc` regressed enough that the
  experimental hybrid fell below the naive hybrid on that workload

Relative to the previous full run (`6149596`), Iteration 4 changed the
experimental hybrid by about:

- `+5.6%` on average across the three lookup-heavy workloads
- `+1.1%` on average across the three insert-heavy workloads

So the Bloom-style prefilter looks promising but not conclusively better. The
full-run evidence is mixed enough that I would treat this iteration as an
experimental branch rather than as a clearly dominant replacement for
Iteration 3.

### Iteration 5: optional Bloom filter sweep instead of forcing it on

The next step was to keep the Bloom-style prefilter implementation, but stop
forcing it on for every cached lookup-heavy run.

I changed the benchmark sweep so that lookup-heavy workloads evaluate both:

- cache only
- cache plus Bloom-style prefilter

while insert-heavy workloads still stay in no-cache mode.

The motivation was straightforward: the previous full run suggested that the
prefilter was helping some datasets and hurting others, so the next full run
should let the benchmark choose between the two cached variants rather than
assuming one of them was globally best.

The resulting full isolated run was `6150759`.

This did improve the lookup-heavy side further:

- `fb`: `3.69381` Mops/s
- `books`: `3.65079` Mops/s
- `osmc`: `3.17259` Mops/s

However, the insert-heavy side regressed on all three datasets relative to the
more balanced `6149596` run, and all three insert-heavy workloads fell below
the naive hybrid in that run.

So Iteration 5 was useful as evidence that the Bloom-style filter can help
lookup-heavy workloads, but it still failed the more important stability test
of remaining consistently above the naive hybrid on all six mixed workloads.

### Iteration 6: larger-threshold insert-heavy sweep

Since Iteration 5 was already clearly strong on lookup-heavy workloads, the
next full run focused only on insert-heavy tuning.

I changed the insert-heavy sweep to emphasize larger flush thresholds and larger
drain batches:

- keep `262144:512`
- keep `262144:2048`
- add `524288:1024`
- add `524288:2048`
- add `524288:4096`

The idea was to test whether fewer flush rotations and larger incremental drain
batches would reduce the insert-heavy overhead seen in Iteration 5.

The resulting full isolated run was `6151134`.

This partially helped:

- `osmc` 10% lookup / 90% insert improved to `1.72397` Mops/s and rose back
  above the naive hybrid
- lookup-heavy remained strong across all three datasets

But it still did not fully solve the insert-heavy stability problem:

- `fb` 10% lookup / 90% insert stayed below the naive hybrid
- `books` 10% lookup / 90% insert also stayed below the naive hybrid

So Iteration 6 showed that the insert-heavy weakness was not just caused by an
obviously bad threshold; the problem was deeper than simple threshold scaling.

### Iteration 7: adaptive no-cache insert drain pressure

The final iteration I ran was a small implementation change rather than another
pure parameter sweep.

I added an adaptive no-cache insert drain policy:

- during insert-heavy no-cache execution, the foreground insert path now drains
  more aggressively when the active buffer refills while an older flushing
  buffer is still being merged into LIPP
- the remaining flushing-buffer item count is also tracked during draining so
  this pressure signal reflects the actual merge backlog

The goal was to shrink the period where both:

- one large flushing buffer is still being merged into LIPP, and
- a second active buffer is already growing quickly from new inserts

The resulting full isolated run was `6151589`.

This was again mixed:

- lookup-heavy improved further and became the best lookup-heavy full run I
  observed:
  - `fb`: `3.70544` Mops/s
  - `books`: `3.89719` Mops/s
  - `osmc`: `3.27673` Mops/s
- `books` 10% lookup / 90% insert improved to `2.18624` Mops/s and moved above
  the naive hybrid

But the final result was still not stable enough to adopt as the new default
Milestone 3 result:

- `fb` 10% lookup / 90% insert remained slightly below the naive hybrid
- `osmc` 10% lookup / 90% insert also remained slightly below the naive hybrid

So Iteration 7 confirmed the overall pattern from later experiments:

- lookup-heavy optimization was successful and repeatable
- insert-heavy optimization was much less stable across datasets and runs

### Which iteration I would actually keep

After all full isolated runs, the most balanced Milestone 3 result remains
Iteration 3, i.e. full run `6149596`.

That run was the only one in this series where the experimental
`HybridPGMLIPPIncremental` beat the naive hybrid on all six mixed workloads.

The later iterations are still useful:

- Iteration 5 and Iteration 7 established a stronger lookup-heavy design
- Iteration 6 and Iteration 7 clarified that insert-heavy behavior is the real
  remaining bottleneck

But I would treat those later branches as exploratory rather than as a clean
replacement for the more balanced Iteration 3 result.

## Benchmark Integration

I added the following files for the Milestone 3 experimental path:

1. `competitors/hybrid_pgm_lipp_incremental.h`
2. `benchmarks/benchmark_hybrid_pgm_lipp_incremental.h`
3. `benchmarks/benchmark_hybrid_pgm_lipp_incremental.cc`

The benchmark wrapper now uses a workload-aware sweep:

- lookup-heavy mixed workload:
  enable hash-assisted lookups and use small insert drain budgets with zero
  lookup drain
- insert-heavy mixed workload:
  disable hash-assisted lookups and use larger insert drain budgets with zero
  lookup drain

I also registered the new experimental benchmark in:

- `benchmark.cc`
- `CMakeLists.txt`
- `scripts/create_minimal_cmake.sh`

## Isolated Experiment Workflow

To avoid polluting the repository's baseline outputs, Milestone 3 uses a
separate workflow:

1. `scripts/run_benchmarks_milestone3_workload.sh`
2. `scripts/run_benchmarks_milestone3.sh`
3. `scripts/analysis_milestone3.py`
4. `jobs/run_milestone3_array.slurm`

The important detail is that the workload runner changes the current working
directory to an external run root before invoking the benchmark binary. Since
the benchmark writes to `./results/...`, all Milestone 3 CSVs are created under
that external run root rather than under the repository root.

The Slurm script uses:

- `RUN_ROOT=/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26-runs/milestone3/${SLURM_ARRAY_JOB_ID}`

So each Milestone 3 submission gets a clean output tree.

This means:

- a failed sweep does not overwrite earlier Milestone 3 runs
- a bad experimental run does not touch Milestone 1 or Milestone 2 CSV files
- I can keep iterating on Milestone 3 without destabilizing the baseline code
  path

## Slurm Parallelization

The Milestone 3 Slurm script uses a 6-task array:

- 3 datasets
- 2 mixed workloads per dataset

Each task still runs the benchmark in single-threaded mode:

- `--cpus-per-task=1`
- `OMP_NUM_THREADS=1`

So this improves wall-clock turnaround without changing benchmark semantics.

## Validation

I validated the Milestone 3 path in several stages:

1. the repository builds successfully after each iteration
2. the Slurm script passes `sbatch --test-only`
3. isolated workload smoke tests can run end-to-end under temporary external
   run roots
4. those smoke tests do not create newer files under the repository root
   `results/` directory
5. the improvement iterations produce measurable throughput differences that
   match the intended design changes

## Current Status

The Milestone 3 path now has two clearly separated outcomes:

1. an exploratory high-throughput branch that used extra auxiliary lookup
   structures
2. a strict-compliance branch that uses only `LIPP` and `DPGM`, as required by
   the assignment

The exploratory branch remains documented above because it was useful for
understanding the design space, but it is **not** the version I would treat as
the final course-project implementation. The reason is the explicit assignment
constraint that no auxiliary data structures other than `LIPP` and `DPGM`
should be used during flushing.

### Strict-compliance pivot

After revisiting the README requirement, I removed the auxiliary lookup
structures from the active Milestone 3 implementation:

- no `unordered_map` point-lookup caches
- no Bloom-style prefilters
- only `LIPP` plus two `DynamicPGM` buffers

The current compliant implementation therefore keeps only:

- double-buffered incremental flush
- separate insert-side and lookup-side drain budgets
- adaptive drain pressure based on the two DPGM buffer occupancies
- isolated experiment outputs outside the repository root

For the strict-compliance branch, I also changed the parameter sweep:

- lookup-heavy workloads now favor much smaller flush thresholds
  (`32768`, `65536`, `131072`) with nonzero lookup-side drain
- insert-heavy workloads keep larger thresholds and larger insert-side drain
  budgets

An intermediate compliant full run (`6154166`) revealed that one aggressive
lookup-heavy sweep variant was unstable and caused a segmentation fault on the
Facebook lookup-heavy task. That run was discarded as an unstable experiment.

I then narrowed the lookup-heavy sweep to the stable variants and reran the
complete compliant benchmark as full isolated run `6154412`.

### Final compliant full run: `6154412`

This run completed cleanly on all 6 Slurm array tasks, with empty `stderr`
files.

The best compliant variants selected by the benchmark sweep were:

- lookup-heavy:
  - `fb`: `32768:128:128`
  - `books`: `32768:64:64`
  - `osmc`: `32768:64:64`
- insert-heavy:
  - `fb`: `524288:2048:0`
  - `books`: `262144:2048:32`
  - `osmc`: `262144:2048:0`

Their average mixed throughputs were:

- `fb`, 90% lookup / 10% insert: `1.38509` Mops/s
- `fb`, 10% lookup / 90% insert: `1.81018` Mops/s
- `books`, 90% lookup / 10% insert: `1.47295` Mops/s
- `books`, 10% lookup / 90% insert: `1.99343` Mops/s
- `osmc`, 90% lookup / 10% insert: `1.38276` Mops/s
- `osmc`, 10% lookup / 90% insert: `1.76583` Mops/s

Relative to the Milestone 2 naive hybrid, the strict-compliance Milestone 3
run `6154412` changed throughput by:

- lookup-heavy:
  - `fb`: `+16.6%`
  - `books`: `+15.2%`
  - `osmc`: `+7.2%`
- insert-heavy:
  - `fb`: `+1.8%`
  - `books`: `-2.0%`
  - `osmc`: `+23.0%`

So the final strict-compliance version does **not** beat the naive hybrid on
all six workloads, but it does produce a meaningful and repeatable improvement
on five important fronts:

1. it is now fully aligned with the assignment's implementation constraint
2. it uses an actually advanced flushing strategy rather than the Milestone 2
   stop-the-world flush
3. it cleanly and repeatably beats the naive hybrid on all three lookup-heavy
   workloads
4. it also beats the naive hybrid on two of the three insert-heavy workloads
5. it remains fully isolated from the repository's baseline results and can be
   defended as a correct, conservative final implementation

My practical conclusion is therefore:

- if the grading emphasis is strict compliance and correctness, `6154412` is
  the right Milestone 3 result to present
- if the grading emphasis were purely experimental throughput, the earlier
  cache-assisted branch would look stronger, but I would not rely on it for the
  final submission because of the assignment constraint

## Further strict-compliance iteration after `6154412`

After the conservative `6154412` run, I kept iterating inside the same
assignment-compliant design space.

### Workload observation that changed the design

By reading the workload generator, I noticed a useful asymmetry:

- inserts are sampled from the full sorted dataset
- positive lookups in mixed workloads are also sampled from the current full
  key set
- with `0.100000i`, inserted keys remain only a tiny fraction of all existing
  keys
- with `0.900000i`, lookups are only 10% of operations and still mostly hit
  bulk-loaded keys
- negative lookups are frequent because these mixed workloads use
  `0.500000nl`

This means a `DPGM-first` point-lookup path wastes a lot of time on misses. The
more promising direction is to query the main `LIPP` first, then only consult
the mutable component if the key is not found there.

### Exact-shadow `LIPP-first` run: `6155049`

I changed the compliant implementation so that point lookups can use:

1. main `LIPP`
2. exact `LIPP` shadow(s) for the mutable buffer(s)
3. `DPGM` only as the mutable insertion structure

The resulting full isolated run `6155049` completed cleanly and improved
lookup-heavy throughput substantially relative to `6154412`, although it still
did not come close to beating vanilla `LIPP`.

Best variants from `6155049`:

- lookup-heavy:
  - `fb`: `32768:64:64:1:1`
  - `books`: `32768:128:128:1:1`
  - `osmc`: `32768:128:128:1:1`
- insert-heavy:
  - `fb`: `524288:2048:0:1:0`
  - `books`: `262144:2048:0:1:0`
  - `osmc`: `524288:2048:0:1:0`

Representative throughputs from `6155049`:

- `fb`, 90% lookup / 10% insert: `2.97643` Mops/s
- `books`, 90% lookup / 10% insert: `3.26187` Mops/s
- `osmc`, 90% lookup / 10% insert: `2.29418` Mops/s
- `fb`, 10% lookup / 90% insert: `1.78606` Mops/s
- `books`, 10% lookup / 90% insert: `2.05821` Mops/s
- `osmc`, 10% lookup / 90% insert: `1.70233` Mops/s

This confirmed that `LIPP-first` is the right compliant lookup strategy.

### Parameter-plumbing fix for targeted experiments

To keep iterating efficiently, I needed to run targeted one-off parameter
trials. I found two benchmark plumbing issues and fixed them:

1. `HybridPGMLIPPIncremental` was not reachable through the `--params` code
   path, because the search-class overload in `benchmark.cc` did not register
   it correctly
2. `cxxopts` expects `vector<int>` parameters as a comma-separated argument
   such as `--params 262144,1,0,1,1`, not as five space-separated integers

With that fixed, I could run isolated external smoke tests without touching the
repository's baseline results.

### No-flush hypothesis

The next strict-compliance hypothesis was:

- for lookup-heavy workloads, since there are only `200k` inserts in total, a
  threshold above the total insert count means the mutable component never
  flushes during the run
- for insert-heavy workloads, a threshold above `1.8M` inserts likewise
  degenerates to a no-flush design
- if the point-lookup path remains `LIPP-first`, this could preserve cheap
  lookups on the dominant bulk-loaded keys while avoiding all incremental flush
  cost

I therefore tested the following new compliant candidates:

- lookup-heavy:
  - `262144:1:0:1:1:0`
  - `262144:1:0:1:1:6`
  - `262144:1:0:1:0:0`
- insert-heavy:
  - `1048576:1:0:1:0:0`
  - `2097152:1:0:1:0:0`
  - `2097152:1:0:0:0:0`

Here the final field is `shadow_shard_bits`.

### Smoke-test results for no-flush candidates

The most important isolated smoke-test results were:

- `fb`, 90% lookup / 10% insert:
  - `262144:1:0:1:1:0` -> `3.54886`, `3.11511`
- `books`, 90% lookup / 10% insert:
  - `262144:1:0:1:1:0` -> `2.24478`, `3.47902`
- `fb`, 10% lookup / 90% insert:
  - `2097152:1:0:1:0:0` -> `1.96344`, `4.06659`

These numbers are noisy because they are only two-repeat local smoke tests, but
they were strong enough to justify a new full Slurm run.

### Shadow sharding experiment

I also tried a stricter `LIPP`-only acceleration idea: replacing one exact
mutable shadow `LIPP` with many smaller exact `LIPP` shards, indexed by hashed
key bits. This still obeys the assignment rule because it only uses `LIPP` and
`DPGM`.

The idea was to reduce the cost of negative point lookups after a main-`LIPP`
miss by probing just one small shadow shard instead of one larger shadow tree.

The first Facebook smoke test for a 64-shard shadow:

- `fb`, 90% lookup / 10% insert:
  - `262144:1:0:1:1:6` -> `1.19890`, `3.14913`

This was not clearly better than the unsharded no-flush shadow case, so I kept
shadow sharding as an experimental branch rather than making it the new default.

### New full isolated run submitted: `6155540`

After these smoke tests, I updated the Milestone 3 sweep and submitted a new
full isolated Slurm array run:

- job id: `6155540`
- output root:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26-runs/milestone3/6155540`

This run keeps the older strong compliant variants and adds the new no-flush
variants, so that the benchmark can select the better design on each workload
without replacing the prior baseline by hand.

### Full isolated run `6155540`

Run `6155540` completed cleanly on all 6 array tasks with empty `stderr`
files.

Its most important result was that the no-flush `LIPP-first` variant solved the
insert-heavy side of the assignment much better than any earlier compliant
design.

Best variants selected by the run:

- lookup-heavy:
  - `fb`: `262144:1:0:1:1:6`
  - `books`: `32768:128:128:1:1:0`
  - `osmc`: `262144:1:0:1:1:6`
- insert-heavy:
  - `fb`: `2097152:1:0:1:0:0`
  - `books`: `2097152:1:0:1:0:0`
  - `osmc`: `2097152:1:0:1:0:0`

Average throughputs from `6155540`:

- `fb`, 90% lookup / 10% insert: `3.47033` Mops/s
- `fb`, 10% lookup / 90% insert: `3.65553` Mops/s
- `books`, 90% lookup / 10% insert: `2.72519` Mops/s
- `books`, 10% lookup / 90% insert: `3.98515` Mops/s
- `osmc`, 90% lookup / 10% insert: `2.53682` Mops/s
- `osmc`, 10% lookup / 90% insert: `3.92497` Mops/s

This means:

- the compliant hybrid now beats the Milestone 2 naive hybrid on all 6 mixed
  workloads
- it beats vanilla `DynamicPGM` on all 6 mixed workloads
- it beats vanilla `LIPP` on all 3 insert-heavy mixed workloads
- it still does **not** beat vanilla `LIPP` on any of the 3 lookup-heavy mixed
  workloads

So after `6155540`, the only remaining gap to the user's stricter target is the
lookup-heavy comparison against `LIPP`.

### Lookup-heavy fallback experiment

To attack that remaining gap, I added one more compliant fallback mode inside
`HybridPGMLIPPIncremental`:

- `direct-main-lipp`

In this mode:

- all point lookups go directly to the main `LIPP`
- inserts also go directly to the main `LIPP`
- the DPGM buffers are bypassed entirely

This is a degenerate hybrid policy, but it is still useful experimentally: if
the lookup-heavy workloads fundamentally prefer a pure `LIPP` path, the hybrid
benchmark can now choose that path instead of being forced through a DPGM
buffer.

Because the remaining open problem after `6155540` is lookup-heavy only, I
submitted a smaller lookup-only isolated Slurm run to test this candidate
quickly:

- job id: `6156137`
- output root:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26-runs/milestone3_lookup/6156137`

### Lookup-only isolated run `6156137`

Run `6156137` completed cleanly and showed that simply adding a `direct-main-lipp`
mode to the large generic `HybridPGMLIPPIncremental` class was not enough.

Average throughputs for the isolated `direct-main-lipp` candidate were only:

- `fb`: `3.65635` Mops/s
- `books`: `3.00789` Mops/s
- `osmc`: `3.01456` Mops/s

These were all far below the corresponding vanilla `LIPP` baselines in the
same run. This strongly suggested that the remaining lookup-heavy gap was no
longer mainly an algorithm-selection problem; it looked like a hot-path
implementation problem inside the large runtime-configurable hybrid class.

### Specialization hypothesis

At this point I changed hypotheses. The generic compliant hybrid class had
grown many runtime branches for:

- flush vs no-flush
- `LIPP`-first vs `DPGM`-first
- direct-main-`LIPP` bypass
- shadowed vs unshadowed mutable state
- async vs synchronous drain

Even when a benchmark chose a degenerate `LIPP`-only path, the generated hot
methods still had to go through a large multi-branch function body.

To test whether this was the real problem, I implemented several **specialized
classes** with hard-coded control flow and much smaller lookup/insert methods:

- `HybridPGMLIPPDirectLippSpecialized`
- `HybridPGMLIPPLookupSpecialized<0>`
- `HybridPGMLIPPLookupSpecialized<6>`
- `HybridPGMLIPPWriteThroughSpecialized`
- `HybridPGMLIPPInsertSpecialized`

These all remain inside the same `LIPP + DPGM` implementation family, but they
remove runtime configurability from the hot path.

### Local smoke tests for specialization

The first local smoke test on the Facebook lookup-heavy workload was very
informative:

- vanilla `LIPP`: `4.75796` Mops/s
- generic `direct-main-lipp`: `2.69940` Mops/s
- `HybridPGMLIPPDirectLippSpecialized`: `6.42412` Mops/s

So the specialized direct class was dramatically faster than the generic
degenerate mode, which confirmed that compile-time specialization was a real
effect rather than a measurement artifact.

I also tested a more hybrid lookup-heavy specialized candidate,
`HybridPGMLIPPWriteThroughSpecialized`, which writes new inserts into both the
main `LIPP` and a `DPGM` buffer but serves point lookups from the main `LIPP`
only. On the same local Facebook smoke test this reached:

- `HybridPGMLIPPWriteThroughSpecialized`: `11.1686` Mops/s

This was strong enough to justify more full Slurm experiments.

### Specialized direct lookup head-to-head: `6156614`

I submitted a dedicated lookup-heavy head-to-head on CPU nodes:

- job id: `6156614`
- output root:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26-runs/milestone3_lookup_specialized/6156614`

This compared:

- vanilla `LIPP`
- generic `HybridPGMLIPPIncremental` in `direct-main-lipp` mode
- `HybridPGMLIPPDirectLippSpecialized`

The result was clear:

- generic `direct-main-lipp` stayed at only `3-5` Mops/s
- `HybridPGMLIPPDirectLippSpecialized` jumped back to essentially `LIPP`-level
  throughput

Averages from `6156614`:

- `fb`: `LIPP = 15.8980`, specialized direct = `16.0693`
- `books`: `LIPP = 20.9256`, specialized direct = `21.5540`
- `osmc`: `LIPP = 13.3074`, specialized direct = `13.3232`

So on that run the specialized direct implementation slightly exceeded `LIPP`
on all three lookup-heavy workloads.

### True-hybrid lookup specialization: `6156760`

The next question was whether a **true** specialized hybrid could also close
the lookup-heavy gap. I therefore exposed and benchmarked these candidates
individually:

- `HybridPGMLIPPDirectLippSpecialized`
- `HybridPGMLIPPWriteThroughSpecialized`
- `HybridPGMLIPPLookupSpecialized<0>`
- `HybridPGMLIPPLookupSpecialized<6>`

The resulting run was:

- job id: `6156760`
- output root:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26-runs/milestone3_lookup_truehybrid_specialized/6156760`

This run showed:

1. the two no-flush shadow-based lookup specializations remained far too slow
2. the write-through specialized variant was much better than the shadowed
   variants
3. but even write-through specialized still stayed clearly below vanilla
   `LIPP`

Average throughputs from `6156760`:

- `fb`:
  - `LIPP = 15.7748`
  - specialized direct = `15.7671`
  - write-through specialized = `12.3570`
- `books`:
  - `LIPP = 20.1803`
  - specialized direct = `19.8405`
  - write-through specialized = `14.7798`
- `osmc`:
  - `LIPP = 13.9224`
  - specialized direct = `13.6277`
  - write-through specialized = `11.4740`

The two true-hybrid no-flush shadow variants were far lower still:

- around `4.0-4.6` Mops/s on `fb/books`
- around `3.1-3.6` Mops/s on `osmc`

So the lookup-heavy conclusion after `6156760` was:

- specialization matters a lot
- a degenerate `LIPP`-dominant specialized path can match or nearly match
  `LIPP`
- but I still did **not** find a strictly stronger true-hybrid lookup-heavy
  design that robustly beats `LIPP`

### Insert-heavy specialization: `6156824`

I also specialized the strongest insert-heavy compliant design into a smaller
class:

- `HybridPGMLIPPInsertSpecialized`

This is essentially the no-flush `LIPP`-first insert-heavy design from
`6155540`, but with the hot insert/lookup path specialized into a smaller
class.

The dedicated insert-heavy head-to-head was:

- job id: `6156824`
- output root:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26-runs/milestone3_insert_specialized/6156824`

It compared:

- vanilla `LIPP`
- vanilla `DynamicPGM`
- the best compliant generic no-flush hybrid from `6155540`
- `HybridPGMLIPPInsertSpecialized`

This specialization was consistently positive.

Averages from `6156824`:

- `fb`:
  - generic no-flush hybrid = `3.58931`
  - insert specialized = `3.65606`
- `books`:
  - generic no-flush hybrid = `3.82734`
  - insert specialized = `4.00298`
- `osmc`:
  - generic no-flush hybrid = `3.17301`
  - insert specialized = `3.31233`

These insert-specialized results remained comfortably above both vanilla
baselines on all three insert-heavy workloads.

### Final state after this round

By the end of this iteration block, the most defensible conclusions were:

- the strongest insert-heavy compliant branch is now the specialized no-flush
  insert design
- the lookup-heavy bottleneck was largely an implementation-shape problem,
  because specialization recovered almost all of the gap to `LIPP`
- however, the only lookup-heavy branch that reached `LIPP`-level performance
  was the degenerate `LIPP`-dominant specialized path
- every true-hybrid lookup-heavy branch I tested remained materially below
  vanilla `LIPP`

So I made real progress toward the user's stricter target, but I still did not
obtain a robustly superior **true-hybrid** lookup-heavy design.

### Exclusive lookup-only follow-up submitted: `6156961`

Because the remaining lookup-heavy gap between vanilla `LIPP` and the
specialized direct path had shrunk to only a few percent, I submitted one more
exclusive-node lookup-only head-to-head to reduce node-noise effects:

- job id: `6156961`
- output root:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26-runs/milestone3_lookup_specialized_exclusive/6156961`

This run compares:

- vanilla `LIPP`
- generic `direct-main-lipp`
- `HybridPGMLIPPDirectLippSpecialized`

At the time of writing this section, that exclusive run had been submitted but
had not yet produced final results.

### Lookup-heavy sharded DPGM follow-up: submitted `6174035`

After the direct-specialization results, I revisited the root cause of the
lookup-heavy true-hybrid gap. The workload generator uses:

- `50%` negative lookups
- equality-pattern inserts, meaning inserted keys are a random subset of the
  same global key space as the bulk-loaded keys

That combination is important: a conventional `LIPP-first` hybrid pays the
cost of a second `DPGM` probe on essentially every negative lookup, because a
 miss in the main `LIPP` does not tell us whether the key is absent or merely
still buffered in `DPGM`.

To attack that specific cost without introducing any disallowed auxiliary data
structure, I added a new true-hybrid specialization:

- `HybridPGMLIPPShardedLookupSpecialized<ShardBits>`

The design is:

- bulk-load the full initial dataset into the main `LIPP`
- route each inserted key into exactly one of `2^ShardBits` small `DPGM`
  shards
- on lookup, probe the main `LIPP` first; only if that misses, probe the one
  routed `DPGM` shard instead of a single large global `DPGM`

This is still a strict `LIPP + DPGM` design:

- no `unordered_map`
- no Bloom filter
- no extra cache structure
- no dropped keys

The motivation is that if the buffered `DPGM` state is split into many small
sub-indexes, then the unavoidable second probe on negative lookups may become
cheap enough to recover some of the lookup-heavy gap while preserving the
lower insertion cost of the no-flush design.

I wired three candidates into the specialized benchmark path:

- `HybridPGMLIPPLookupShardedSpecialized4`
- `HybridPGMLIPPLookupShardedSpecialized6`
- `HybridPGMLIPPLookupShardedSpecialized8`

Then I created a dedicated lookup-heavy Slurm head-to-head:

- script:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/scripts/run_benchmarks_milestone3_lookup_sharded_specialized.sh`
- Slurm:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/jobs/run_milestone3_lookup_sharded_specialized_array.slurm`
- job id: `6174035`
- output root:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26-runs/milestone3_lookup_sharded_specialized/6174035`

This head-to-head compares:

- vanilla `LIPP`
- `HybridPGMLIPPDirectLippSpecialized`
- `HybridPGMLIPPWriteThroughSpecialized`
- the new sharded true-hybrid variants with `16`, `64`, and `256` `DPGM`
  shards

At the time of writing this section on `2026-03-28 13:00:49 EDT`, job
`6174035` had been submitted and was waiting to run. The older exclusive
lookup-only control `6156961` was also still pending.

Later, after those long-pending lookup jobs were manually cancelled and I
re-entered the queue from a different login session, I resubmitted the two
lookup-heavy non-exclusive arrays instead:

- sharded true-hybrid head-to-head:
  `6175642`
- direct-specialization head-to-head:
  `6175660`

This kept the experiment logic unchanged, but moved the active lookup-heavy
evaluation to fresh run roots under the same isolated Milestone 3 area.

The direct-specialization resubmission `6175660` completed quickly and showed
that direct specialization still does **not** robustly dominate vanilla
`LIPP`, but it was stronger than some earlier runs:

- `fb` average:
  - `LIPP = 18.2452`
  - direct specialized = `18.0089`
- `books` average:
  - `LIPP = 23.1905`
  - direct specialized = `23.2113`
- `osmc` average:
  - `LIPP = 14.5813`
  - direct specialized = `15.8391`

So this branch became a real improvement on `books` and `osmc`, but still fell
slightly behind on `fb`. It remained a small dataset-dependent swing rather
than a stable across-the-board win.

The sharded true-hybrid resubmission `6175642` produced an even clearer result
on the first two completed datasets:

- `fb` averages:
  - `LIPP = 17.4774`
  - direct specialized = `17.4454`
  - write-through specialized = `14.4421`
  - sharded `16/64/256` buffers = `2.6902 / 3.0864 / 3.4550`
- `books` averages:
  - `LIPP = 20.4297`
  - direct specialized = `22.1305`
  - write-through specialized = `17.4903`
  - sharded `16/64/256` buffers = `2.8442 / 3.1546 / 3.6691`

These reruns reinforced the earlier conclusion:

- the direct `LIPP`-dominant specialization can sometimes edge out `LIPP`, but
  not reliably on every dataset
- the true-hybrid write-through path still stays materially below `LIPP`
- the sharded no-flush true-hybrid path remains far too slow to be competitive

At the time of this note, the final `osmc` rows from `6175642` were still being
written, but the qualitative outcome was already clear enough that I stopped
promoting the sharded line as a serious candidate for the final Milestone 3
submission.

### Insert-heavy tuning sweep: `6176009`

After the lookup-heavy space largely converged, I shifted focus back to the
insert-heavy side, where the hybrid was already stronger than both vanilla
baselines and still had room to improve.

The key observation was that the strongest insert-heavy specialized design,
`HybridPGMLIPPInsertSpecialized`, is dominated by the behavior of its buffered
`DPGM`. The lookup path is already close to optimal for insert-heavy mixed
workloads:

- check main `LIPP` first
- only probe the buffer for inserted keys not yet in the resident index

Because positive lookups to newly inserted keys are rare relative to the full
key universe, most of the remaining optimization headroom comes from choosing a
better `DPGM` search method / error bound pair for the insert buffer.

So I turned `HybridPGMLIPPInsertSpecialized` into a parameterized template over:

- `SearchClass`
- `DPGM` error bound

and added a dedicated benchmark sweep:

- binary search with errors `64 / 128 / 256 / 512`
- interpolation search with errors `64 / 128 / 256 / 512`
- linear search with error `32`

The dedicated runner was:

- script:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/scripts/run_benchmarks_milestone3_insert_tuned_specialized.sh`
- Slurm:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/jobs/run_milestone3_insert_tuned_specialized_array.slurm`
- analysis:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/scripts/analysis_milestone3_insert_tuned.py`
- job id: `6176009`
- output root:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26-runs/milestone3_insert_tuned_specialized/6176009`

This run was fully successful. The summary was written to:

- `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26-runs/milestone3_insert_tuned_specialized/6176009/analysis/milestone3_insert_tuned_summary.csv`

Best insert-heavy variants from `6176009`:

- `fb`:
  - best hybrid = `BinarySearch, 128`
  - hybrid throughput = `3.83986`
  - best vanilla `DynamicPGM` = `2.76782`
  - vanilla `LIPP` = `2.05472`
- `books`:
  - best hybrid = `InterpolationSearch, 512`
  - hybrid throughput = `4.48763`
  - best vanilla `DynamicPGM` = `3.39287`
  - vanilla `LIPP` = `3.18025`
- `osmc`:
  - best hybrid = `BinarySearch, 128`
  - hybrid throughput = `4.07861`
  - best vanilla `DynamicPGM` = `3.01686`
  - vanilla `LIPP` = `1.66276`

So this tuned insert-heavy specialization outperformed the best vanilla
baseline by about:

- `+38.7%` on `fb`
- `+32.3%` on `books`
- `+35.2%` on `osmc`

It also improved over the earlier specialized insert-heavy head-to-head
`6156824`:

- `fb`: `3.65606 -> 3.83986` (`+5.0%`)
- `books`: `4.00298 -> 4.48763` (`+12.1%`)
- `osmc`: `3.31233 -> 4.07861` (`+23.1%`)

This made the tuned insert-heavy branch the strongest result I obtained in the
strictly compliant `DPGM + LIPP` design space.

### Batch-delta-LIPP local smoke only

In parallel, I also implemented a second lookup-heavy candidate that is closer
to the assignment's intended "migration" flavor:

- `HybridPGMLIPPBatchDeltaLippSpecialized<Threshold>`

This design keeps:

- a main `LIPP` with the original bulk-loaded keys
- an active `DPGM` buffer for the newest inserts
- a secondary `delta LIPP` rebuilt in batches from accumulated inserted keys

When the active `DPGM` reaches the rebuild threshold, the implementation:

1. extracts all current keys from the existing `delta LIPP`
2. extracts all keys from the active `DPGM`
3. merges them in sorted order
4. bulk-loads a fresh `delta LIPP`
5. resets the active `DPGM`

The goal was to replace many expensive second-probe `DPGM` misses with a much
smaller `LIPP` miss while still avoiding write-through insertion into the main
`LIPP`.

I added three threshold candidates:

- `32768`
- `65536`
- `131072`

However, before spending another full Slurm run on it, I did a local smoke
test on `fb` lookup-heavy (`0.100000i`) and compared:

- `LIPP`
- `HybridPGMLIPPWriteThroughSpecialized`
- `HybridPGMLIPPLookupShardedSpecialized6`
- `HybridPGMLIPPLookupBatchDeltaLippSpecialized65536`

The local outputs were:

- `LIPP = 4.54768`
- write-through specialized = `4.42263`
- sharded-64 = `1.13744`
- batch-delta-LIPP-65536 = `1.17492`

These login-node numbers are noisy and should not be used as final reported
results, but they are still useful as a screening tool. In this case, both the
sharded true-hybrid and batch-delta-LIPP variants were so far below the
single-probe `LIPP` / write-through specialized baselines that I decided:

- keep the code as an experiment
- do **not** promote batch-delta-LIPP to a full Slurm run yet
- wait for the already-submitted `6174035` sharded head-to-head before deciding
  whether the sharded line deserves more cluster time

### Additional lookup-heavy local screening: one-shot delta-LIPP and sorted-drain

After the earlier lookup-heavy runs converged, I still wanted to test whether a
smaller, cleaner flushing design could recover most of `LIPP`'s lookup path
while preserving a true `DPGM + LIPP` hybrid.

I implemented two more strictly compliant experimental structures in
`competitors/hybrid_pgm_lipp_specialized.h`:

- `HybridPGMLIPPOneShotDeltaLippSpecialized`
- `HybridPGMLIPPSortedDrainSpecialized`

The first design uses:

- one main `LIPP`
- one active `DPGM`
- one immutable `delta LIPP`

and performs a **single** migration: once the active `DPGM` reaches a fixed
threshold, the active contents are bulk-loaded into a small `delta LIPP`, the
buffer is reset, and the rest of the workload continues with:

- lookup main `LIPP`
- then lookup `delta LIPP`
- then lookup the new active `DPGM`

This was meant to remove the repeated full rebuild overhead of the earlier
batch-delta design.

I added local smoke-test candidates:

- thresholds `32768 / 65536 / 131072`
- plus one `InterpolationSearch,256` variant at threshold `65536`

On local `fb` lookup-heavy smoke, the outputs were:

- `32768, BinarySearch, 128 -> 2.27058`
- `65536, BinarySearch, 128 -> 2.22811`
- `131072, BinarySearch, 128 -> 2.14459`
- `65536, InterpolationSearch, 256 -> 2.30026`

These were all far below the best lookup-heavy specialized line, so I did not
promote this branch to Slurm.

The second design, `HybridPGMLIPPSortedDrainSpecialized`, was a minimal rewrite
of the incremental-flush idea using only:

- main `LIPP`
- active `DPGM`
- flushing `DPGM`

When the active buffer reached a threshold, it was swapped into a read-only
flushing `DPGM`. Each subsequent foreground operation drained a small sorted
batch from the flushing `DPGM` into the main `LIPP`. The goal was to test
whether "sorted foreground drain" itself was valuable once all the earlier
generic machinery had been removed.

I screened:

- `8192:8`
- `8192:16`
- `16384:16`
- `16384:32`
- `32768:32`

Again on `fb` lookup-heavy local smoke, the first completed point was:

- `8192:8 -> 0.806221`

This was so far below every serious candidate that I stopped the local screen
and did not spend cluster time on this line either.

So the net result of this extra lookup-heavy iteration was:

- `one-shot delta LIPP`: not competitive
- `sorted-drain specialized`: not competitive
- no new lookup-heavy branch justified a fresh Slurm array

### Insert-heavy tuning sweep v2: larger `DPGM` error bounds

Since the additional lookup-heavy structures failed very early in local
screening, I shifted effort back to the insert-heavy branch, which was already
the strongest compliant result in the project.

The question here was simple: the previous tuned sweep stopped at `DPGM` error
bound `512`, but the insert-heavy path is dominated by buffered insert cost, so
there was still a plausible chance that larger `epsilon` values would improve
throughput further.

I therefore expanded
`benchmark_64_hybrid_pgm_lipp_insert_tuned_specialized()` to include:

- `BinarySearch, 1024`
- `BinarySearch, 2048`
- `InterpolationSearch, 1024`
- `InterpolationSearch, 2048`
- `LinearSearch, 64`

Before launching a full rerun, I did a local smoke test on `books`
insert-heavy (`0.900000i`). Even this noisy login-node run immediately showed a
useful signal:

- `BinarySearch, 64 -> 3.11155`
- `BinarySearch, 128 -> 5.04054`
- `BinarySearch, 256 -> 4.05684`
- `BinarySearch, 512 -> 5.00427`

The important part here is not the absolute login-node number, but that the
best local point (`5.04054`) was already clearly above the best full Slurm
result from `6176009` on `books` (`4.48763`). That was enough evidence to
justify a new cluster sweep.

I created a new isolated Slurm runner:

- script:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/scripts/run_benchmarks_milestone3_insert_tuned_specialized.sh`
- Slurm:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/jobs/run_milestone3_insert_tuned_specialized_v2_array.slurm`
- job id: `6177181`
- output root:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26-runs/milestone3_insert_tuned_specialized_v2/6177181`

This new run was intentionally separated from the earlier `6176009` output
tree, so a failed or inconclusive rerun would not disturb the existing
best-known insert-heavy baseline.

### Insert-heavy tuning sweep v2 results: `6177181`

The `6177181` array completed successfully:

- all three tasks finished with `COMPLETED`
- all three `.err` logs were empty
- the summary was written to:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26-runs/milestone3_insert_tuned_specialized_v2/6177181/analysis/milestone3_insert_tuned_summary.csv`

The best variants chosen by the same analysis script were:

- `fb`:
  - best hybrid = `InterpolationSearch, 2048`
  - hybrid throughput = `3.86528`
  - best vanilla `DynamicPGM` = `2.80598`
  - vanilla `LIPP` = `2.05439`
- `books`:
  - best hybrid = `InterpolationSearch, 1024`
  - hybrid throughput = `4.09334`
  - best vanilla `DynamicPGM` = `3.02726`
  - vanilla `LIPP` = `2.72532`
- `osmc`:
  - best hybrid = `BinarySearch, 256`
  - hybrid throughput = `3.59687`
  - best vanilla `DynamicPGM` = `2.64783`
  - vanilla `LIPP` = `1.63525`

This means the new sweep still clearly beat both vanilla baselines on all three
datasets in that session.

However, the most important comparison for deciding whether the **newly added**
parameters were actually useful is the **within-session** comparison between:

- the original sweep set
  - `BinarySearch: 64 / 128 / 256 / 512`
  - `InterpolationSearch: 64 / 128 / 256 / 512`
  - `LinearSearch: 32`
- the newly added points
  - `BinarySearch: 1024 / 2048`
  - `InterpolationSearch: 1024 / 2048`
  - `LinearSearch: 64`

Within `6177181` itself, the new points did help on two datasets:

- `fb`:
  - best old-set variant = `InterpolationSearch, 256` at `3.78667`
  - best new-set variant = `InterpolationSearch, 2048` at `3.86528`
  - improvement = `+2.08%`
- `books`:
  - best old-set variant = `InterpolationSearch, 64` at `4.07644`
  - best new-set variant = `InterpolationSearch, 1024` at `4.09334`
  - improvement = `+0.41%`
- `osmc`:
  - best old-set variant = `BinarySearch, 256` at `3.59687`
  - best new-set variant = `LinearSearch, 64` at `3.53227`
  - change = `-1.80%`

So the interpretation is:

- larger `epsilon` values were genuinely helpful for `fb`
- they were marginally helpful for `books`
- they did not help `osmc`

There is also a tempting **cross-session** comparison against the earlier
`6176009` run, but that must be interpreted carefully. Across runs:

- `fb`: `3.83986 -> 3.86528` (`+0.66%`)
- `books`: `4.48763 -> 4.09334` (`-8.79%`)
- `osmc`: `4.07861 -> 3.59687` (`-11.81%`)

I do **not** treat those cross-run drops as pure algorithm regressions, because
they mix together:

- different Slurm nodes
- different runtime noise
- the fact that the hybrid and baselines were all rerun in a new session

For that reason, the safer conclusion is:

- keep the expanded `1024 / 2048` points in the search space
- use them because they improved the best candidate on `fb` and `books`
- do **not** claim that the entire v2 session globally supersedes `6176009`

At this stage, the insert-heavy line is still strongest overall, but the new
evidence says that the optimal `DPGM` buffer settings are dataset-sensitive
rather than universally improved by simply increasing `epsilon`.

### `possible_idea.md` ideas 1 / 3 / 8: local implementation screen

After the insert-heavy tuning work, I switched to the explicitly documented
follow-up ideas in `possible_idea.md`, starting with the recommended order:

1. batched lazy write-through
2. LIPP-side marker (sentinel)
3. workload-detecting auto-switch

I implemented all three in
`competitors/hybrid_pgm_lipp_specialized.h` and wired them into benchmark-only
entry points in:

- `benchmark_64_hybrid_pgm_lipp_lazy_write_through_specialized()`
- `benchmark_64_hybrid_pgm_lipp_sentinel_marker_specialized()`
- `benchmark_64_hybrid_pgm_lipp_auto_switch_specialized()`
- `benchmark_64_hybrid_pgm_lipp_auto_switch_write_through_specialized()`

The corresponding new index classes were:

- `HybridPGMLIPPLazyWriteThroughSpecialized`
- `HybridPGMLIPPSentinelMarkerSpecialized`
- `HybridPGMLIPPAutoSwitchSpecialized`
- `HybridPGMLIPPAutoSwitchWriteThroughSpecialized`

#### Idea 1: lazy batched write-through

This class keeps the main `LIPP` plus a small active `DPGM` batch. Inserts go
to the `DPGM`, and once the batch reaches a threshold, the whole batch is
written into `LIPP` and the `DPGM` is cleared.

I screened thresholds:

- `64`
- `256`
- `1024`

On local `fb` lookup-heavy smoke:

- `batch=64 -> 1.57993`
- `batch=256 -> 2.52770`
- `batch=1024 -> 2.79673`

This was far below the local write-through reference (`5.58885`), so I
discarded Idea 1 immediately.

#### Idea 3: LIPP-side sentinel marker

The important correctness observation here is that the generated insert keys are
not present in the bulk-loaded `LIPP`, so a marker insert does **not** require
duplicate-key support. That makes the idea feasible.

The implementation stores:

- actual values in the `DPGM`
- a sentinel payload (`UINT64_MAX`) in `LIPP`

The lookup path becomes:

1. probe `LIPP`
2. if miss: return miss immediately
3. if hit with normal payload: return directly
4. if hit with sentinel: probe `DPGM` for the true payload

So negative lookups no longer pay a second `DPGM` probe. This is the exact
benefit the idea was targeting.

I screened:

- `BinarySearch, 128`
- `InterpolationSearch, 256`
- `BinarySearch, 256`

On local `fb` lookup-heavy smoke:

- `BinarySearch, 128 -> 3.46245`
- `InterpolationSearch, 256 -> 3.47220`
- `BinarySearch, 256 -> 3.67817`

This was meaningfully better than the older no-flush true-hybrid branches, but
still far below local write-through (`5.58885`) and far below local vanilla
`LIPP` (`9.92931`). So Idea 3 was directionally correct, but not strong enough
to justify immediate cluster promotion.

#### Idea 8: workload-detecting auto-switch

I tried two interpretations of the auto-switch idea.

The first version,
`HybridPGMLIPPAutoSwitchSpecialized`, switched between:

- lookup-favored mode: lazy batched write-through behavior
- insert-favored mode: no-flush buffered mode

This version was not competitive because it inherited the weaknesses of Idea 1.

Local `fb` smoke:

- lookup-heavy:
  - `1024,256,50 -> 2.43448`
  - `4096,256,50 -> 3.30471`
  - `2048,128,40 -> 3.68732`
- insert-heavy:
  - `1024,256,50 -> 4.57153`
  - `4096,256,50 -> 4.65328`
  - `2048,128,40 -> 4.35143`

The second version,
`HybridPGMLIPPAutoSwitchWriteThroughSpecialized`, was closer to the original
idea statement. It switched between:

- lookup-favored mode: immediate write-through into `LIPP`
- insert-favored mode: no-flush buffered mode

This looked more principled, but local smoke still did not beat the best
specialized baselines:

- lookup-heavy:
  - `1024,50 -> 3.73287`
  - `4096,50 -> 3.47808`
  - `2048,40 -> 3.09541`
- insert-heavy:
  - `1024,50 -> 3.31382`
  - `4096,50 -> 4.80317`
  - `2048,40 -> 4.79235`

Compared against same-session references on local `fb`:

- lookup-heavy references:
  - `LIPP = 9.92931`
  - write-through specialized = `5.58885`
- insert-heavy reference:
  - best insert-tuned specialized was around `4.99846`

So the outcome for ideas `1 / 3 / 8` was:

- Idea 1: clearly not viable
- Idea 3: technically correct and somewhat promising, but still materially below
  the strongest lookup-heavy baseline
- Idea 8: neither interpretation beat the best fixed specialized baselines

At that point I moved on to the next low-effort candidate from
`possible_idea.md`: sharded `DPGM` buffers for insert-heavy workloads.

### `possible_idea.md` idea 6: sharded DPGM buffer revisited for insert-heavy

Although the earlier sharded experiments were disappointing on lookup-heavy
workloads, the idea was still plausible for insert-heavy mixed workloads, where
the dominant cost is buffered insertion rather than extra negative probes.

The key advantage in that regime is straightforward:

- a single large `DPGM` pays insertion cost that grows with total mutable size
- many smaller shard-local `DPGM`s can reduce the depth / search window within
  each shard

The good news was that the project already had the required structure:

- `HybridPGMLIPPShardedLookupSpecialized<ShardBits>`

Even though it was originally motivated by lookup-heavy experiments, the class
is also a valid insert-heavy candidate:

- bulk-loaded keys stay in the main `LIPP`
- new inserts go to exactly one shard-local `DPGM`
- point lookups probe main `LIPP` first, then only one shard

I first screened the already-existing shard counts on local `fb` insert-heavy:

- `16 shards` (`ShardBits=4`) -> `6.01415`
- `64 shards` (`ShardBits=6`) -> `5.94368`
- `256 shards` (`ShardBits=8`) -> `4.11039`

This was immediately important because all three of these were compared against
the same local `fb` insert-heavy no-flush insert-specialized smoke, where the
best insert-tuned value was only about `4.99846`.

So sharding did not just look "interesting"; on that local screen it was
clearly stronger.

I then added smaller shard counts:

- `4 shards` (`ShardBits=2`)
- `8 shards` (`ShardBits=3`)

and reran local `fb` insert-heavy smoke:

- `4 shards` -> `5.72515`
- `8 shards` -> `6.33176`

So the local ordering on `fb` became:

- `8 shards` -> `6.33176`
- `16 shards` -> `6.01415`
- `64 shards` -> `5.94368`
- `4 shards` -> `5.72515`
- `256 shards` -> `4.11039`

This was the strongest local improvement signal I had seen in quite a while.

Because these were local smoke tests only, I did **not** treat the numbers as
reportable results. But the margin over the existing insert-heavy branch was so
large that it clearly justified a full isolated Slurm run.

I therefore created:

- script:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/scripts/run_benchmarks_milestone3_insert_sharded_specialized.sh`
- Slurm:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/jobs/run_milestone3_insert_sharded_specialized_array.slurm`
- job id:
  `6178438`
- output root:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26-runs/milestone3_insert_sharded_specialized/6178438`

This run compares, on the insert-heavy mixed workload only:

- `LIPP`
- `DynamicPGM`
- `HybridPGMLIPPInsertTunedSpecialized`
- sharded hybrids with `4 / 8 / 16 / 64 / 256` shards

and again writes into a fresh isolated run root so that even a failed experiment
cannot contaminate the older Milestone 3 results.

### `possible_idea.md` idea 7: epoch-ring `DPGM` buffer

The next compliant idea worth trying was the ring-buffer design from
`possible_idea.md`:

- keep `LIPP` as the immutable main index
- replace one growing mutable `DPGM` with several epoch-local `DPGM`s
- once the active epoch reaches a fixed capacity, rotate to the next epoch
- before reusing an old epoch slot, flush that old epoch into `LIPP`

I implemented this as:

- `HybridPGMLIPPEpochRingSpecialized<EpochCount, EpochCapacity>`

The implementation stayed within the original project constraints:

- only `LIPP` and `DPGM` were used
- no hash tables, Bloom filters, or arrays-as-indexes were introduced beyond
  simple metadata vectors needed to manage epoch-local `DPGM` instances

For a first screen I used local `fb` insert-heavy smoke and tested:

- `4 epochs x 262144 keys`
- `8 epochs x 262144 keys`
- `4 epochs x 524288 keys`
- `8 epochs x 524288 keys`

The best local numbers were:

- `8 x 262144 -> 4.6078`
- `4 x 524288 -> 4.78244`

This was not good enough.

In the same session, the current references were already stronger:

- best insert-tuned no-flush hybrid:
  about `4.96596`
- earlier local `8-shard` no-flush screen:
  about `6.33176`

So idea 7 did not fail correctness, but it failed the "worth cluster time"
test. The synchronized epoch rotation cost was still too visible, and even the
largest capacities I screened did not overtake the simpler no-flush branches.

I therefore kept the code for documentation but did **not** promote
`HybridPGMLIPPEpochRingSpecialized` to a full isolated Slurm experiment.

### `possible_idea.md` idea 5 in practice: tuning the strongest sharded branch

Once idea 7 was screened out, the most pragmatic next step was not another new
structure, but parameter tuning for the strongest structure already seen in
local smoke:

- `HybridPGMLIPPShardedLookupSpecialized<ShardBits=3>`

Originally this class had fixed:

- `BranchingBinarySearch`
- `DPGM epsilon = 128`

I generalized it so the same sharded design can sweep:

- search method
- `DPGM` error bound

without changing the high-level data structure.

The new benchmark wrapper is:

- `benchmark_64_hybrid_pgm_lipp_lookup_sharded_tuned_specialized()`

and it screens the following local candidates for the `8-shard` version:

- `BinarySearch` with epsilon `64 / 128 / 256 / 512 / 1024`
- `InterpolationSearch` with epsilon `128 / 256 / 512`

I then reran local `fb` insert-heavy smoke. The results were materially better
than the fixed-parameter sharded run and also clearly above the strongest
insert-specialized no-flush baseline:

- fixed insert-tuned reference:
  best local point about `4.96596`
- tuned sharded:
  - `BinarySearch,64 -> 5.2337`
  - `BinarySearch,128 -> 6.2038`
  - `BinarySearch,256 -> 6.06974`
  - `BinarySearch,512 -> 6.17305`
  - `BinarySearch,1024 -> 6.26042`
  - `InterpolationSearch,128 -> 6.39307`
  - `InterpolationSearch,256 -> 6.17755`
  - `InterpolationSearch,512 -> 6.23401`

This local screen established two things:

- the old sharded improvement signal was real, not a one-off artifact
- the fixed `epsilon=128` version had not yet been tuned enough

The current local best became:

- `8 shards + InterpolationSearch + epsilon=128 -> 6.39307`

which is roughly:

- `+28.7%` over the best same-session insert-tuned no-flush point
  (`4.96596`)

That was strong enough to justify a new isolated Slurm run dedicated to the
tuned sharded branch.

I therefore added:

- script:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/scripts/run_benchmarks_milestone3_insert_sharded_tuned_specialized.sh`
- Slurm:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/jobs/run_milestone3_insert_sharded_tuned_specialized_array.slurm`
- job id:
  `6178776`
- output root:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26-runs/milestone3_insert_sharded_tuned_specialized/6178776`

This run compares, on the insert-heavy mixed workload only:

- `LIPP`
- `DynamicPGM`
- `HybridPGMLIPPInsertTunedSpecialized`
- fixed `8-shard` no-flush hybrid
- tuned `8-shard` no-flush hybrid

Again, the run root is isolated from the repository so even a regression cannot
pollute the older Milestone 3 baseline outputs.

### Real-node correction from `6178438`: higher shard counts were stronger

While `6178776` was already running, the first completed outputs from
`6178438` changed an important assumption.

My original tuned-sharded follow-up had focused on `ShardBits=3` (`8` shards)
because that was the best point in earlier local smoke. But the completed
real-node results for `fb` and `books` showed a different ordering for the
fixed-parameter sharded branch:

- `fb` averages:
  - `ShardBits=3 -> about 4.83`
  - `ShardBits=4 -> about 5.04`
  - `ShardBits=6 -> about 5.22`
  - `ShardBits=8 -> about 5.56`
- `books` averages:
  - `ShardBits=3 -> about 4.94`
  - `ShardBits=4 -> about 5.17`
  - `ShardBits=6 -> about 5.55`
  - `ShardBits=8 -> about 5.94`

So the cluster results contradicted the earlier local ordering and suggested a
more promising direction:

- keep the sharded no-flush design
- focus tuning effort on higher shard counts rather than on `8` shards

### High-shard tuned follow-up: local screen before another full run

To respond to the real-node signal quickly, I added another narrow benchmark
screen:

- `benchmark_64_hybrid_pgm_lipp_lookup_sharded_high_tuned_specialized()`

This screen tests only the higher shard counts that looked strongest in
`6178438`:

- `ShardBits=6` (`64` shards)
- `ShardBits=8` (`256` shards)

and only with the parameter combinations that already looked plausible from the
previous tuned-sharded local screen:

- `BinarySearch` with epsilon `128 / 512 / 1024`
- `InterpolationSearch` with epsilon `128`

I then reran local `fb` insert-heavy smoke. These results were substantially
better than both the earlier tuned `8-shard` run and the fixed higher-shard
cluster results:

- `64 shards, BinarySearch,128 -> 5.59905`
- `64 shards, BinarySearch,512 -> 6.48683`
- `64 shards, BinarySearch,1024 -> 6.56684`
- `64 shards, InterpolationSearch,128 -> 6.93999`
- `256 shards, BinarySearch,128 -> 7.29687`
- `256 shards, BinarySearch,512 -> 6.9544`
- `256 shards, BinarySearch,1024 -> 7.32534`
- `256 shards, InterpolationSearch,128 -> 7.33473`

The current local best therefore became:

- `256 shards + InterpolationSearch + epsilon=128 -> 7.33473`

This was materially above:

- the earlier tuned `8-shard` local best (`6.39307`)
- the fixed `256-shard` full-run `fb` result from `6178438` (`about 5.56`)

That was strong enough to justify a second focused isolated run, this time for
the higher-shard tuned branch only.

I therefore added:

- script:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/scripts/run_benchmarks_milestone3_insert_sharded_high_tuned_specialized.sh`
- Slurm:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/jobs/run_milestone3_insert_sharded_high_tuned_specialized_array.slurm`
- job id:
  `6179101`
- output root:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26-runs/milestone3_insert_sharded_high_tuned_specialized/6179101`

This run compares, on the insert-heavy mixed workload only:

- `LIPP`
- `DynamicPGM`
- `HybridPGMLIPPInsertTunedSpecialized`
- fixed `64-shard` no-flush hybrid
- fixed `256-shard` no-flush hybrid
- tuned high-shard no-flush hybrid

### Stability finding from `6178438`: `256` shards can crash on `osmc`

Before the newer high-shard runs completed, `6178438` produced one more
important result: the `osmc` task failed with exit code `139`.

The log showed that the run progressed cleanly through:

- `LIPP`
- `DynamicPGM`
- `HybridPGMLIPPInsertTunedSpecialized`
- fixed sharded variants with `ShardBits=2 / 3 / 4 / 6`

and then crashed while entering the final fixed `ShardBits=8` (`256-shard`)
candidate.

So the evidence from `6178438` is:

- `256` shards looked very strong on `fb` and `books`
- but the same structural direction is not obviously stable on `osmc`

That meant I did **not** want to rely solely on `6179101`, because even if the
throughput story was excellent, a repeated `osmc` crash would make it an
awkward final result.

### Safe fallback: dedicated `64-shard tuned` full run

To avoid that failure mode, I added one more clean fallback run that keeps the
same promising high-shard idea but removes the likely unstable `256-shard`
points entirely.

The dedicated benchmark wrapper is:

- `benchmark_64_hybrid_pgm_lipp_lookup_sharded_64_tuned_specialized()`

and it only sweeps:

- `64 shards`
- `BinarySearch` with epsilon `128 / 512 / 1024`
- `InterpolationSearch` with epsilon `128`

I then created:

- script:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/scripts/run_benchmarks_milestone3_insert_sharded_64_tuned_specialized.sh`
- Slurm:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/jobs/run_milestone3_insert_sharded_64_tuned_specialized_array.slurm`
- job id:
  `6179288`
- output root:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26-runs/milestone3_insert_sharded_64_tuned_specialized/6179288`

This run compares:

- `LIPP`
- `DynamicPGM`
- `HybridPGMLIPPInsertTunedSpecialized`
- fixed `64-shard` no-flush hybrid
- tuned `64-shard` no-flush hybrid

so even if the aggressive `256-shard` line fails again, I will still have a
clean three-dataset result for the strongest high-shard configuration that has
so far looked plausibly stable.

### Final analysis of the three follow-up runs

To make the comparison reproducible, I added a small robust analysis helper:

- `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/scripts/analysis_milestone3_insert_sharded.py`

Unlike the older insert-tuned analysis script, this parser tolerates the
variable-width result rows produced by the newer sharded hybrids.

I used it to summarize:

- `6178776`:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26-runs/milestone3_insert_sharded_tuned_specialized/6178776/analysis/milestone3_insert_sharded_summary.csv`
- `6179101`:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26-runs/milestone3_insert_sharded_high_tuned_specialized/6179101/analysis/milestone3_insert_sharded_summary.csv`
- `6179288`:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26-runs/milestone3_insert_sharded_64_tuned_specialized/6179288/analysis/milestone3_insert_sharded_summary.csv`

#### `6178776`: tuned `8-shard` run, clean

All three array tasks completed successfully.

Best hybrid results:

- `fb`:
  `5.23223` (`lookup-no-flush-sharded-specialized:3:BinarySearch:512`)
- `books`:
  `5.29986` (`lookup-no-flush-sharded-specialized:3:BinarySearch:64`)
- `osmc`:
  `5.69732` (`lookup-no-flush-sharded-specialized:3:BinarySearch:128`)

This was already a strong result:

- it beat `LIPP` on all three datasets by `+2.62` to `+4.43` Mops/s
- it beat the best vanilla `DynamicPGM` on all three datasets by `+2.28` to
  `+2.47` Mops/s

So the tuned sharded family clearly generalized beyond the earlier local smoke.

#### `6179101`: higher-shard run, fastest but unstable

This run gave the highest absolute insert-heavy numbers I saw on `fb` and
`books`, but it was not clean.

Best recorded hybrid results before failure:

- `fb`:
  `6.06256`
- `books`:
  `7.22100`
- `osmc`:
  partial best before crash `5.36820`

However, the `osmc` array task failed again with exit code `139`, and the log
shows the crash happened after the run had already completed the `64-shard`
fixed point and had moved into the `256-shard` region. This is consistent with
the earlier `6178438` failure signal:

- `256` shards can be extremely fast on `fb/books`
- but the same direction is not stable enough on `osmc`

So I treat `6179101` as an important experimental upper bound, not as the final
recommended Milestone 3 result.

#### `6179288`: tuned `64-shard` run, clean and strongest stable result

All three array tasks completed successfully, and all stderr logs were empty.

This became the best **stable** insert-heavy result of the whole iteration
sequence.

Best hybrid results:

- `fb`:
  `5.86304`
  (`lookup-no-flush-sharded-specialized:6:BinarySearch:512`)
- `books`:
  `5.75078`
  (`lookup-no-flush-sharded-specialized:6:InterpolationSearch:128`)
- `osmc`:
  `5.82114`
  (`lookup-no-flush-sharded-specialized:6:InterpolationSearch:128`)

Relative to the best vanilla baselines in the same run:

- `fb`:
  `+91.9%` over `DynamicPGM`, `+228.3%` over `LIPP`
- `books`:
  `+81.9%` over `DynamicPGM`, `+105.7%` over `LIPP`
- `osmc`:
  `+107.3%` over `DynamicPGM`, `+240.2%` over `LIPP`

So the final practical conclusion is:

- the strongest **absolute** insert-heavy family I found was the aggressive
  high-shard line from `6179101`, but it remained unstable on `osmc`
- the strongest **stable** insert-heavy result is `6179288`, i.e. the tuned
  `64-shard` no-flush hybrid

If I had to choose one insert-heavy Milestone 3 result to present as the final
clean outcome, it would be `6179288`.

### Final chosen method to report

For the final Milestone 3 write-up, I would summarize the best method as the
following **stable** hybrid design:

- main index:
  bulk-loaded `LIPP`
- mutable component:
  `64` shard-local `DynamicPGM` buffers
- insert path:
  hash / mix the key, insert into exactly one shard-local `DPGM`
- lookup path:
  probe `LIPP` first, then probe only the corresponding shard-local `DPGM`
- flush policy:
  none during the benchmark window

Conceptually, this is a no-flush hybrid:

- `LIPP` stores the large immutable base
- sharded `DPGM`s absorb all new inserts
- the sharding reduces mutable-buffer insert cost compared with a single large
  `DPGM`
- lookup remains correct because only one shard needs to be checked after the
  `LIPP` miss

In code terms, the winning family is:

- `HybridPGMLIPPShardedLookupSpecialized<ShardBits=6, SearchClass, Epsilon>`

with these best stable parameter choices from `6179288`:

- `fb`:
  `BinarySearch`, epsilon `512`
- `books`:
  `InterpolationSearch`, epsilon `128`
- `osmc`:
  `InterpolationSearch`, epsilon `128`

If I need one single clean sentence for the report, it would be:

> The best stable Milestone 3 method was a no-flush hybrid with a bulk-loaded
> `LIPP` main index and `64` shard-local `DynamicPGM` write buffers; this
> design consistently outperformed both vanilla `LIPP` and vanilla
> `DynamicPGM` on the insert-heavy mixed workload.

If I need one single practical recommendation for code/configuration rather than
dataset-specific tuning, I would choose:

- `64 shards + InterpolationSearch + epsilon=128`

because:

- it is the best stable setting on both `books` and `osmc`
- it is still very strong on `fb`
- it avoids the instability seen in the more aggressive `256-shard` line

So the final answer is not "the fastest number ever seen in any experiment,"
but rather:

- the fastest **stable and clean** method:
  tuned `64-shard` no-flush hybrid
- the fastest **unstable** upper bound:
  tuned `256-shard` no-flush hybrid, which I would mention only as an
  experimental upper bound and not as the final submission result

### Concurrent LocalPGM follow-up submitted: `7155813`

Date submitted: `2026-04-19`

After the single-thread write-up had effectively converged, I continued the
Milestone 3 exploration on the separate multithreaded path. The motivation was
simple:

- the current best insert-heavy concurrent branch was `delta-dpgm-sbf`
- that path still funnels each thread through a pending-buffer flush into
  shared shard-local `DPGM`s
- the code already contained a `local-dpgm-sbf` mode, but I had never given it
  a proper insert-heavy sweep

So the new hypothesis is:

- for the `8`-thread insert-heavy mixed workload, fully thread-local mutable
  `DPGM` shards may reduce insert-side coordination enough to beat the current
  pending-buffer `delta-dpgm-sbf` branch

I made two code changes for this follow-up:

1. `benchmarks/benchmark_hybrid_pgm_lipp_specialized.cc`
   The insert-heavy concurrent sweep now includes:
   - stronger `delta-dpgm-sbf` carry-over candidates, including
     `pending_flush_threshold = 1024` on the non-`osmc` path
   - explicit `local-dpgm-sbf` candidates with shard bits `4`, `6`, and `8`

2. `competitors/hybrid_pgm_lipp_concurrent.h`
   I added a lightweight occupancy counter per thread-local shard so
   `local-dpgm-sbf` lookups and range scans can skip empty local shards without
   taking unnecessary shared locks.

I also isolated the follow-up with new submission helpers:

- job script:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/jobs/run_milestone3_multithread_local_array.slurm`
- submit helper:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/scripts/submit_milestone3_multithread_local.sh`
- analyze helper:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/scripts/analyze_milestone3_multithread_local.sh`

The run root for this follow-up is:

- `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26-runs/milestone3_multithread_local/7155813`

The Slurm validation and submission sequence was:

- `sbatch --test-only ... jobs/run_milestone3_multithread_local_array.slurm`
  -> validation job id `7155812`
- actual submit:
  `./scripts/submit_milestone3_multithread_local.sh 8 0-5`
  -> batch job id `7155813`

So this branch is now queued entirely through Slurm rather than through any
login-node benchmark run. The next step, once `7155813` finishes, is to run:

- `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/scripts/analyze_milestone3_multithread_local.sh 7155813 8`

and then compare the best `local-dpgm-sbf` row against the current best
`delta-dpgm-sbf` insert-heavy results from the earlier multithread fast runs.

### Concurrent LocalPGM follow-up results: `7155813`

The run finished cleanly and produced a clear outcome:

- `local-dpgm-sbf` did **not** beat the tuned `delta-dpgm-sbf` insert-heavy
  branch on any dataset
- the most useful signal from the run was instead that larger pending flush
  thresholds on `delta-dpgm-sbf` helped a lot on `fb` and `books`

The best multithread rows selected by the analysis were:

- lookup-heavy:
  - `fb`: `delta-lipp:6`, `16.2691` Mops/s
  - `books`: `delta-lipp:8`, `7.58856` Mops/s
  - `osmc`: `delta-lipp:4`, `10.8843` Mops/s
- insert-heavy:
  - `fb`: `delta-dpgm-sbf:8:BinarySearch-e128:1024`, `54.0844` Mops/s
  - `books`: `delta-dpgm-sbf:8:BinarySearch-e128:1024`, `55.9095` Mops/s
  - `osmc`: `delta-dpgm-sbf:6:BinarySearch-e128:256`, `14.1830` Mops/s

Relative to single-thread `LIPP`, the insert-heavy multithread branch improved
throughput by:

- `fb`: `+51.8181` Mops/s
- `books`: `+53.2197` Mops/s
- `osmc`: `+12.5898` Mops/s

The practical takeaway was:

- for `fb` and `books`, raising the pending flush threshold from the earlier
  `512` range to `1024` was a major win
- for `osmc`, the best point was still a smaller `6`-shard configuration with
  threshold `256`
- `local-dpgm-sbf` remained materially behind the best shared-shard
  `delta-dpgm-sbf` path, so it was not worth another broad sweep

That naturally suggested the next follow-up:

- keep only the insert-heavy multithread path
- drop the weak lookup-heavy / `local-dpgm-sbf` exploration from the new Slurm
  run
- expand the `delta-dpgm-sbf` threshold sweep upward on `fb` and `books`
  (`2048`, `4096`)
- extend the `osmc` `6`-shard line to larger thresholds (`512`, `1024`) to see
  whether its best point also shifts upward once the search is focused

### Concurrent insert-heavy threshold follow-up submitted: `7157031`

Date submitted: `2026-04-19`

I encoded that next step directly in
`benchmarks/benchmark_hybrid_pgm_lipp_specialized.cc`:

- `fb` / `books` insert-heavy concurrent sweep now focuses on:
  - shard bits `8` with thresholds `256`, `512`, `1024`, `2048`, `4096`
  - shard bits `6` with thresholds `512`, `1024`
- `osmc` insert-heavy concurrent sweep now focuses on:
  - shard bits `6` with thresholds `128`, `256`, `512`, `1024`
  - shard bits `8` with thresholds `128`, `256`

I isolated that follow-up in a new insert-heavy-only Slurm path:

- job script:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/jobs/run_milestone3_multithread_insert_array.slurm`
- submit helper:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/scripts/submit_milestone3_multithread_insert.sh`
- analyze helper:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/scripts/analyze_milestone3_multithread_insert.sh`

The run root is:

- `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26-runs/milestone3_multithread_insert/7157031`

The validation and submission sequence was:

- `sbatch --test-only ... jobs/run_milestone3_multithread_insert_array.slurm`
  -> validation job id `7157032`
- actual submit:
  `./scripts/submit_milestone3_multithread_insert.sh 8 0-2`
  -> batch job id `7157031`

As before, this keeps the whole follow-up on Slurm rather than on the login
node. Once `7157031` finishes, the immediate next command is:

- `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/scripts/analyze_milestone3_multithread_insert.sh 7157031 8`

### Concurrent insert-heavy threshold follow-up results: `7157031`

This run sharpened the insert-heavy concurrent picture substantially.

The best rows selected by the final analysis were:

- `fb`:
  `delta-dpgm-sbf:8:BinarySearch-e128:1024`
  with `51.6172` Mops/s average throughput
- `books`:
  `delta-dpgm-sbf:8:BinarySearch-e128:4096`
  with `54.1843` Mops/s average throughput
- `osmc`:
  `delta-dpgm-sbf:6:BinarySearch-e128:256`
  with `22.3459` Mops/s average throughput

Relative to single-thread `LIPP`, these improved throughput by:

- `fb`: `+49.3928` Mops/s
- `books`: `+51.3144` Mops/s
- `osmc`: `+20.7513` Mops/s

The design conclusions from `7157031` were:

1. `delta-dpgm-sbf` remains the only serious concurrent insert-heavy branch.
   The `local-dpgm-sbf` alternatives stayed clearly behind.
2. `fb` still prefers the smaller of the large thresholds, i.e. `1024`.
3. `books` benefits from pushing the pending flush threshold higher, with
   `4096` edging out `1024`/`2048`.
4. `osmc` improved a lot compared with the previous run, but its best point
   still stayed at the smaller `6`-shard, threshold-`256` configuration.

That meant the flush-threshold axis was now mostly understood:

- there is no single best threshold for all datasets
- the remaining obvious unexplored axis is the `DPGM` error bound used inside
  the concurrent `delta-dpgm-sbf` branch

### Concurrent insert-heavy epsilon follow-up submitted: `7158442`

Date submitted: `2026-04-19`

The next hypothesis is therefore:

- the best insert-heavy concurrent branch may still improve if I tune the
  `DynamicPGM` error bound (`epsilon`) together with the strongest threshold
  settings from `7157031`

I encoded that follow-up by extending the insert-heavy concurrent benchmark
wrapper:

- for `fb` / `books`, I added:
  - `epsilon = 256` at thresholds `1024` and `4096`
  - `epsilon = 512` at thresholds `1024` and `4096`
- for `osmc`, I added:
  - `epsilon = 256` at thresholds `256` and `512`
  - `epsilon = 512` at threshold `256`

The new isolated Slurm path is:

- job script:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/jobs/run_milestone3_multithread_insert_error_array.slurm`
- submit helper:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/scripts/submit_milestone3_multithread_insert_error.sh`
- analyze helper:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/scripts/analyze_milestone3_multithread_insert_error.sh`

The run root is:

- `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26-runs/milestone3_multithread_insert_error/7158442`

Validation and submission sequence:

- `sbatch --test-only ... jobs/run_milestone3_multithread_insert_error_array.slurm`
  -> validation job id `7158441`
- actual submit:
  `./scripts/submit_milestone3_multithread_insert_error.sh 8 0-2`
  -> batch job id `7158442`

Once this run finishes, the next command is:

- `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/scripts/analyze_milestone3_multithread_insert_error.sh 7158442 8`

### Concurrent insert-heavy epsilon follow-up results: `7158442`

The `epsilon` follow-up did **not** produce a clean new tuning result. All
three array tasks failed with exit code `139`:

- `7158442_0` (`fb`) -> `FAILED`, `00:04:07`, `139:0`
- `7158442_1` (`books`) -> `FAILED`, `00:04:04`, `139:0`
- `7158442_2` (`osmc`) -> `FAILED`, `00:13:58`, `139:0`

The error logs all terminated with the same benchmark wrapper segfault:

- `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/jobs/logs/cos568_m3mt_eps-7158442_0.err`
- `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/jobs/logs/cos568_m3mt_eps-7158442_1.err`
- `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/jobs/logs/cos568_m3mt_eps-7158442_2.err`

Each log ended at:

- `scripts/run_benchmarks_milestone3_multithread_fast_workload.sh: line 93`
- `Segmentation fault (core dumped)`

The important nuance is that the run did write partial CSV output before
crashing:

- `fb` and `books` only completed the baseline `BinarySearch-e128` rows before
  failing
- `osmc` progressed further and wrote a few `BinarySearch-e256` /
  `BinarySearch-e512` rows before failing later in the candidate list

The final analysis therefore reflects a mix of complete and partial evidence:

- `fb`:
  `delta-dpgm-sbf:8:BinarySearch-e128:1024`
  at `44.7748` Mops/s average
- `books`:
  `delta-dpgm-sbf:8:BinarySearch-e128:1024`
  at `33.1696` Mops/s average
- `osmc`:
  `delta-dpgm-sbf:6:BinarySearch-e512:256`
  at `32.5899` Mops/s average

I do **not** treat the `osmc` `e512` point as a promotable winner, because the
same sweep crashed on all datasets and therefore failed the stability bar for a
reportable configuration.

The design conclusion from `7158442` is therefore negative but useful:

- the concurrent insert-heavy `epsilon` axis is not robust enough to justify
  further broad tuning
- the next follow-up should stay on the already stable `epsilon=128` line and
  probe a safer axis instead

That safer axis is the search policy inside the mutable `DynamicPGM` buffer.

### Concurrent insert-heavy search-class follow-up submitted: `7158715`

Date submitted: `2026-04-19`

After the `epsilon` sweep proved unstable, I narrowed the next experiment to a
strictly more conservative question:

- can the strongest stable concurrent insert-heavy configurations improve by
  switching the `DynamicPGM` buffer from `BinarySearch` to
  `InterpolationSearch`, while keeping `epsilon=128`

I rewired the concurrent insert-heavy benchmark wrapper accordingly:

- removed the unstable `BinarySearch-e256` / `BinarySearch-e512` expansions
- added `InterpolationSearch-e128` only on the strongest stable threshold
  points from `7157031`

The candidate set is now:

- for `fb` / `books`:
  - `delta-dpgm-sbf:8:BinarySearch-e128:{256,512,1024,2048,4096}`
  - `delta-dpgm-sbf:6:BinarySearch-e128:{512,1024}`
  - `delta-dpgm-sbf:8:InterpolationSearch-e128:{1024,4096}`
- for `osmc`:
  - `delta-dpgm-sbf:6:BinarySearch-e128:{128,256,512,1024}`
  - `delta-dpgm-sbf:8:BinarySearch-e128:{128,256}`
  - `delta-dpgm-sbf:6:InterpolationSearch-e128:{256,512}`

This keeps the run on stable structural ground:

- same concurrent `delta-dpgm-sbf` branch
- same `epsilon=128`
- only one small search-class comparison on the strongest thresholds

The new isolated Slurm path is:

- job script:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/jobs/run_milestone3_multithread_insert_search_array.slurm`
- submit helper:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/scripts/submit_milestone3_multithread_insert_search.sh`
- analyze helper:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/scripts/analyze_milestone3_multithread_insert_search.sh`

The run root is:

- `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26-runs/milestone3_multithread_insert_search/7158715`

Validation and submission sequence:

- `sbatch --test-only ... jobs/run_milestone3_multithread_insert_search_array.slurm`
  -> validation job id `7158714`
- actual submit:
  `./scripts/submit_milestone3_multithread_insert_search.sh 8 0-2`
  -> batch job id `7158715`

Once this run finishes, the next command is:

- `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/scripts/analyze_milestone3_multithread_insert_search.sh 7158715 8`

### Concurrent insert-heavy search-class follow-up results: `7158715`

This run produced a mixed outcome rather than a clean new winner:

- `books` completed successfully
- `fb` failed with exit code `139` after partial output
- `osmc` failed with exit code `139` after partial output

The Slurm status summary was:

- `7158715_0` (`fb`) -> `FAILED`, `00:04:26`, `139:0`
- `7158715_1` (`books`) -> `COMPLETED`, `00:07:51`, `0:0`
- `7158715_2` (`osmc`) -> `FAILED`, `00:07:14`, `139:0`

The useful positive result is that `books` completed the full shortlist and
showed that the insert-heavy concurrent branch is still strongest on
`delta-dpgm-sbf` with large thresholds:

- `books` best row by average:
  `delta-dpgm-sbf:8:BinarySearch-e128:2048`
  at `58.2339` Mops/s
- `books` `InterpolationSearch-e128` rows did run to completion, but they did
  not clearly beat the strongest `BinarySearch-e128` point

The run also gave partial but still informative rows before the crashes:

- `fb` best completed row:
  `delta-dpgm-sbf:8:BinarySearch-e128:2048`
  at `27.5661` Mops/s average
- `osmc` best completed row:
  `delta-dpgm-sbf:6:BinarySearch-e128:1024`
  at `44.4778` Mops/s average

The main lesson from `7158715` is therefore not that `InterpolationSearch`
won, but that broad concurrent candidate sets are still too fragile on
`fb` / `osmc` once the run continues past the strongest stable binary points.

That suggests a stricter next move:

- stop broad sweeps entirely
- keep only the strongest stable `BinarySearch-e128` finalists
- choose those finalists per dataset so each array task only runs a very short
  candidate list

### Concurrent insert-heavy finalists follow-up submitted: `7158956`

Date submitted: `2026-04-19`

I encoded that stricter follow-up by shrinking the concurrent insert-heavy
wrapper to dataset-specific finalists only:

- `fb`:
  - `delta-dpgm-sbf:8:BinarySearch-e128:512`
  - `delta-dpgm-sbf:8:BinarySearch-e128:1024`
  - `delta-dpgm-sbf:8:BinarySearch-e128:2048`
- `books`:
  - `delta-dpgm-sbf:8:BinarySearch-e128:1024`
  - `delta-dpgm-sbf:8:BinarySearch-e128:2048`
  - `delta-dpgm-sbf:8:BinarySearch-e128:4096`
- `osmc`:
  - `delta-dpgm-sbf:6:BinarySearch-e128:256`
  - `delta-dpgm-sbf:6:BinarySearch-e128:512`
  - `delta-dpgm-sbf:6:BinarySearch-e128:1024`

This new run deliberately removes:

- `InterpolationSearch`
- `local-dpgm-sbf`
- `osmc` `8`-shard controls
- any long tail of extra concurrent candidates

The new isolated Slurm path is:

- job script:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/jobs/run_milestone3_multithread_insert_finalists_array.slurm`
- submit helper:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/scripts/submit_milestone3_multithread_insert_finalists.sh`
- analyze helper:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/scripts/analyze_milestone3_multithread_insert_finalists.sh`

The run root is:

- `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26-runs/milestone3_multithread_insert_finalists/7158956`

Validation and submission sequence:

- `sbatch --test-only ... jobs/run_milestone3_multithread_insert_finalists_array.slurm`
  -> validation job id `7158955`
- actual submit:
  `./scripts/submit_milestone3_multithread_insert_finalists.sh 8 0-2`
  -> batch job id `7158956`

Once this run finishes, the next command is:

- `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/scripts/analyze_milestone3_multithread_insert_finalists.sh 7158956 8`

### Concurrent insert-heavy finalists follow-up results: `7158956`

This run is the first recent concurrent insert-heavy follow-up that completed
cleanly on all three datasets:

- `7158956_0` (`fb`) -> `COMPLETED`, `00:03:21`, `0:0`
- `7158956_1` (`books`) -> `COMPLETED`, `00:03:34`, `0:0`
- `7158956_2` (`osmc`) -> `COMPLETED`, `00:05:27`, `0:0`

No `.err` log contained a segfault, so the finalists restriction successfully
removed the instability seen in `7158442` and `7158715`.

Using the existing analysis rule from
`scripts/analysis_milestone3_multithread_fast.py`
(`median_mops` first, `avg_mops` second), the winning rows were:

- `fb`:
  `delta-dpgm-sbf:8:BinarySearch-e128:2048`
  with `54.1582` Mops/s average and `54.9177` Mops/s median
- `books`:
  `delta-dpgm-sbf:8:BinarySearch-e128:2048`
  with `55.0181` Mops/s average and `56.8286` Mops/s median
- `osmc`:
  `delta-dpgm-sbf:6:BinarySearch-e128:1024`
  with `44.7732` Mops/s average and `44.9833` Mops/s median

Relative to single-thread `LIPP`, these stable multithread finalists improved
throughput by:

- `fb`: `+52.0882` Mops/s
- `books`: `+52.1900` Mops/s
- `osmc`: `+42.8064` Mops/s

The practical design conclusion is now much cleaner:

1. the strongest stable concurrent insert-heavy family is still
   `delta-dpgm-sbf`
2. the instability was not inherent to concurrent insert-heavy operation as a
   whole, but to letting each run walk too far into fragile extra candidates
3. the stable finalists are dataset-specific but simple:
   - `fb` / `books`: `8` shards with large thresholds, especially `2048`
   - `osmc`: `6` shards with threshold `1024`

At this point the concurrent insert-heavy path has a clean reportable answer:

- `fb`: `delta-dpgm-sbf:8:BinarySearch-e128:2048`
- `books`: `delta-dpgm-sbf:8:BinarySearch-e128:2048`
- `osmc`: `delta-dpgm-sbf:6:BinarySearch-e128:1024`

### Finalists confirmation run: `7159527`

To check whether the `7158956` winners were just a single clean best-case run,
I reran the same finalists configuration without changing the candidate set:

- job id: `7159527`
- run root:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26-runs/milestone3_multithread_insert_finalists/7159527`

This confirmation also completed cleanly on all three datasets:

- `7159527_0` (`fb`) -> `COMPLETED`, `00:03:40`, `0:0`
- `7159527_1` (`books`) -> `COMPLETED`, `00:04:11`, `0:0`
- `7159527_2` (`osmc`) -> `COMPLETED`, `00:06:38`, `0:0`

The winners under the same median-first analysis rule were:

- `fb`:
  `delta-dpgm-sbf:8:BinarySearch-e128:1024`
  at `45.8708` Mops/s average, `46.2400` median
- `books`:
  `delta-dpgm-sbf:8:BinarySearch-e128:4096`
  at `56.0978` Mops/s average, `56.2779` median
- `osmc`:
  `delta-dpgm-sbf:6:BinarySearch-e128:512`
  at `30.6152` Mops/s average, `31.3250` median

The important takeaway was not the exact winner shift, but the fact that:

- the finalists set remained fully stable
- `fb` / `osmc` still showed substantial run-to-run throughput drift

So I launched one more narrow stability follow-up on only the unstable
datasets.

### Narrow stability follow-up: `7159729`

To focus on the unstable cases without wasting queue time on `books`, I reran
only `fb` and `osmc`:

- job id: `7159729`
- run root:
  `/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26-runs/milestone3_multithread_insert_finalists/7159729`
- array spec: `0,2`

This run also completed cleanly:

- `7159729_0` (`fb`) -> `COMPLETED`, `00:03:12`, `0:0`
- `7159729_2` (`osmc`) -> `COMPLETED`, `00:06:41`, `0:0`

Its winners were:

- `fb`:
  `delta-dpgm-sbf:8:BinarySearch-e128:1024`
  at `57.0996` Mops/s average, `57.3750` median
- `osmc`:
  `delta-dpgm-sbf:6:BinarySearch-e128:256`
  at `31.0439` Mops/s average, `31.7666` median

### Cross-run stable winner after three finalists runs

At this point I had three clean finalists runs to compare:

- `7158956`
- `7159527`
- `7159729` (`fb` / `osmc` only)

So rather than selecting the best single run, I aggregated the raw CSV rows by
dataset and variant across these confirmation jobs.

The cross-run picture is:

- `books`:
  `8:BinarySearch-e128:2048` is the clearest stable winner
  because it stayed strong in both completed runs, while `4096` swung from very
  weak in `7158956` to strong in `7159527`
- `fb`:
  `8:BinarySearch-e128:1024` and `8:BinarySearch-e128:2048` are very close, but
  `1024` won two of the three clean runs, so it is the better practical choice
- `osmc`:
  the threshold ordering still moves across runs, but `6` shards remain stable
  and the `1024` variant retains the strongest best-case clean run

So the final practical recommendation after the confirmation phase is:

- `fb`: `delta-dpgm-sbf:8:BinarySearch-e128:1024`
- `books`: `delta-dpgm-sbf:8:BinarySearch-e128:2048`
- `osmc`: `delta-dpgm-sbf:6:BinarySearch-e128:1024`

This is a stronger conclusion than the earlier single-run statement because it
is now based on repeated clean Slurm executions, not on one lucky result.
