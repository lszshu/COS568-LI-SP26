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
`LIPP`:

- `fb` average:
  - `LIPP = 18.2452`
  - direct specialized = `18.0089`
- `books` average:
  - `LIPP = 23.1905`
  - direct specialized = `23.2113`

So this branch remained effectively a tie with small dataset-dependent swings,
not a stable across-the-board win.

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
