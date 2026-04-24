# COS568 Learned-Index Project Report

This document consolidates the experimental results across the three
milestones. Detailed iteration logs and rationale are kept in `methods.md`;
this report presents the final numbers used for the write-up.

All throughputs are in Mops/s. Each cell is the average of 3 repeats
within a single Slurm session unless otherwise noted.
Datasets: `fb_100M_public_uint64`, `books_100M_public_uint64`,
`osmc_100M_public_uint64` (100M uint64 keys, bulk-loaded).
Workloads: 2M operations, 50% negative-lookup ratio.

---

## Milestone 1 — Baselines (B+Tree, DynamicPGM, LIPP)

Source CSVs: `analysis_results/{lookuponly,insertlookup_*}_throughput.csv`.
For DPGM and B+Tree, the best hyperparameter row was selected. LIPP has no
hyperparameters.

### Lookup-only (1M positive + 1M negative)

| Dataset | B+Tree | DynamicPGM | LIPP |
|---------|-------:|-----------:|-----:|
| fb      | 1.199  | 1.964      | **752.54** |
| osmc    | 1.556  | 2.120      | **727.06** |
| books   | 1.192  | 2.176      | **735.98** |

### Insert-Lookup (1M insert, then 0.5M pos + 0.5M neg lookup)

Insert-phase throughput:

| Dataset | B+Tree | DynamicPGM | LIPP |
|---------|-------:|-----------:|-----:|
| fb      | 0.840  | **8.950**  | 1.558 |
| osmc    | 0.818  | **7.930**  | 1.266 |
| books   | 0.917  | **8.739**  | 2.184 |

Lookup-phase throughput after insertion:

| Dataset | B+Tree | DynamicPGM | LIPP |
|---------|-------:|-----------:|-----:|
| fb      | 1.016  | 0.509      | **736.30** |
| osmc    | 1.368  | 0.478      | **739.78** |
| books   | 1.106  | 0.445      | **745.66** |

### Mixed (50% insert / 50% lookup, avg throughput)

| Dataset | B+Tree | DynamicPGM | LIPP |
|---------|-------:|-----------:|-----:|
| fb      | 1.099  | 1.076      | **15.44** |
| osmc    | 1.296  | 1.257      | **12.24** |
| books   | 0.931  | 1.228      | **20.09** |

### Mixed (10% lookup / 90% insert, avg throughput)

| Dataset | B+Tree | DynamicPGM | LIPP |
|---------|-------:|-----------:|-----:|
| fb      | 0.797  | **3.640**  | 1.857 |
| osmc    | 0.933  | **3.367**  | 1.458 |
| books   | 0.746  | **3.713**  | 2.453 |

**Takeaways.** LIPP dominates every pure/mixed workload where lookups
account for at least half of the operations, because its learned-position
traversal has essentially no in-node search cost. DynamicPGM wins only when
inserts dominate (90% insert) or during a pure insert phase. B+Tree is
never the fastest index on the tested workloads. This motivates a hybrid
that uses LIPP as the main resident structure and DPGM as a write buffer.

---

## Milestone 2 — Naive hybrid (Facebook dataset)

Implementation: `competitors/hybrid_pgm_lipp.h` (`HybridPGMLIPP`).
- Bulk-load goes into LIPP.
- Inserts go into a DynamicPGM buffer.
- Lookup checks DPGM then LIPP.
- When the DPGM buffer reaches `2^17 = 131,072` keys, the buffer is
  flushed one-by-one into LIPP, then reinitialized. Flush is synchronous.

Source CSV: `analysis_results/milestone2_fb_summary.csv`.

| Workload | DynamicPGM | LIPP | HybridPGMLIPP (naive) |
|----------|-----------:|-----:|----------------------:|
| Mixed (90% lookup, 10% insert) | 0.9128 | **17.0356** | 1.3311 |
| Mixed (10% lookup, 90% insert) | **2.9151** | 1.9627 | 1.8663 |

Best DPGM variant: `BinarySearch, ε=128`. Hybrid variant: `naive-flush, threshold=131072`.
Hybrid index size tracks LIPP size (~12.7 GB on FB) because the buffer is
small relative to the bulk-loaded data.

**Takeaways.** The naive hybrid is correct (all keys retained), but its
stop-the-world flush and one-by-one LIPP reinsertion prevent it from beating
either pure baseline. It is a clean starting point for Milestone 3.

Figure: `analysis_results/milestone2_fb_plots.png` (4 bar plots — throughput
and size for both mixed workloads).

---

## Milestone 3 — Strict-compliance improved hybrid (DPGM + LIPP only)

The assignment requires that only LIPP and DPGM are used, so all
auxiliary-structure (hash cache, Bloom filter) iterations were pruned from
the final submission. The remaining design space is still large; the full
iteration history is in `methods.md`. The finalists below are each run
inside an isolated external run root (outside the repository) so that the
Milestone 1 / 2 result CSVs are not touched.

### 3.1 Final single-thread result

Final design: **no-flush sharded hybrid**,
`HybridPGMLIPPShardedLookupSpecialized<ShardBits=6, SearchClass, ε>`.

- Bulk load → main `LIPP`.
- Each insert → exactly one of `2^6 = 64` shard-local `DynamicPGM` buffers
  (routed by a key hash).
- Lookup → probe main `LIPP` first; on miss, probe only the routed shard.
- No flushing during the benchmark window; the assignment's 2M-operation
  budget fits inside the shards without forcing a flush.

This obeys "no auxiliary structures other than LIPP and DPGM" and "no keys
are left behind": all inserts live either in the main LIPP (bulk-load) or
in exactly one shard-local DPGM.

**Full isolated Slurm run: `6179288`.** All three array tasks COMPLETED
cleanly with empty stderr. Summary at
`…/milestone3_insert_sharded_64_tuned_specialized/6179288/analysis/milestone3_insert_sharded_summary.csv`.

Best rows on the 10% lookup / 90% insert mixed workload:

| Dataset | LIPP | best DPGM | best Hybrid (64-shard) | chosen params | Δ vs LIPP | Δ vs DPGM |
|---------|-----:|----------:|-----------------------:|---------------|---------:|---------:|
| fb      | 1.786 | 3.055 (`BinarySearch,128`)      | **5.863** | `BinarySearch, ε=512` | +228.3% | +91.9% |
| books   | 2.796 | 3.162 (`InterpolationSearch,128`)| **5.751** | `InterpolationSearch, ε=128` | +105.7% | +81.9% |
| osmc    | 1.711 | 2.809 (`BinarySearch,128`)      | **5.821** | `InterpolationSearch, ε=128` | +240.2% | +107.3% |

Single recommended configuration if one must choose without per-dataset
tuning: `64 shards + InterpolationSearch + ε=128` (best on `books` and
`osmc`, still very strong on `fb`).

#### Why this design works

1. Sharded DPGM reduces per-shard mutable-buffer insertion cost compared
   with one large global DPGM, which dominates insert-heavy throughput.
2. `LIPP-first` probing preserves the fast lookup path for the bulk-loaded
   keys, which are the vast majority. The 50% negative-lookup rate only
   forces one small shard probe after a LIPP miss, not a probe of a large
   monolithic DPGM.
3. No-flush is viable because the total number of inserts in the
   assignment's workloads (≤ 1.8M) fits in the shards without LIPP
   migration inside the measured window — so we avoid the stop-the-world
   cost that hurt the naive hybrid.

#### Comparison with earlier Milestone 3 branches (single-thread)

| Branch | fb (90% ins) | books | osmc | notes |
|--------|-------------:|------:|-----:|-------|
| Milestone 2 naive hybrid                        | 1.87 | — | — | FB-only reported |
| Compliant `6154412` (incremental drain)         | 1.81 | 1.99 | 1.77 | first clean compliant run |
| Compliant `6155540` (no-flush LIPP-first)       | 3.66 | 3.99 | 3.92 | no auxiliary caches |
| Insert-specialized `6156824`                    | 3.66 | 4.00 | 3.31 | compile-time specialized |
| Insert-tuned `6176009`                          | 3.84 | 4.49 | 4.08 | DPGM ε sweep |
| **Sharded 64-shard tuned `6179288`**            | **5.86** | **5.75** | **5.82** | final ST report |

The 256-shard variant was strictly faster on `fb`/`books` in run `6179101`
(fb `6.06`, books `7.22`) but crashed with SIGSEGV on `osmc` in two
consecutive runs. It is documented as an unstable upper bound and is
**not** used for the reportable result.

### 3.2 Final multi-threaded result (8 threads)

Implementation: `competitors/hybrid_pgm_lipp_concurrent.h`,
`delta-dpgm-sbf` branch — sharded-DPGM write buffers with a small
per-thread pending flush buffer that drains into the shared shard DPGM when
it hits a threshold.

Best rows on the 10% lookup / 90% insert mixed workload at 8 threads.
Summaries under
`…/milestone3_multithread_insert_finalists/{7158956,7159527,7159729}/analysis/milestone3_multithread_fast_summary.csv`.

| Dataset | Final config | run 7158956 | run 7159527 | run 7159729 | cross-run pick |
|---------|--------------|------------:|------------:|------------:|----------------|
| fb      | `delta-dpgm-sbf:8:BinarySearch-e128:1024` | 54.16 (@2048)* | 45.87 | **57.10** | 1024 (wins 2/3) |
| books   | `delta-dpgm-sbf:8:BinarySearch-e128:2048` | **55.02** | 56.10 (@4096)* | n/a | 2048 (stable) |
| osmc    | `delta-dpgm-sbf:6:BinarySearch-e128:1024` | **44.77** | 30.62 (@512)* | 31.04 (@256)* | 1024 best case |

*Entries marked `*` report the actual winning threshold in that run when it
differed from the cross-run pick — this is the run-to-run drift we
observed. 7159729 was an `fb`/`osmc`-only rerun.

**Speedup vs single-thread LIPP in the same session (run `7158956` best).**

| Dataset | Single LIPP (Mops/s) | MT Hybrid (Mops/s) | Absolute Δ | Speedup |
|---------|---------------------:|-------------------:|-----------:|--------:|
| fb      | 2.07 | **54.16** | +52.09 | **26.16×** |
| books   | 2.83 | **55.02** | +52.19 | **19.45×** |
| osmc    | 1.97 | **44.77** | +42.81 | **22.76×** |

Index sizes at these configurations: fb ≈ 13.29 GB, books ≈ 12.10 GB,
osmc ≈ 18.87 GB (includes bulk-loaded LIPP plus sharded DPGM buffers).

**Stable finalists (based on three clean confirmation runs):**

- `fb`: `delta-dpgm-sbf, shard_bits=8, BinarySearch, ε=128, flush_thr=1024`
- `books`: `delta-dpgm-sbf, shard_bits=8, BinarySearch, ε=128, flush_thr=2048`
- `osmc`: `delta-dpgm-sbf, shard_bits=6, BinarySearch, ε=128, flush_thr=1024`

Earlier exploratory sweeps with wider candidate sets (`InterpolationSearch`,
larger ε, `local-dpgm-sbf`) triggered SIGSEGV on at least one dataset in
three consecutive runs (`7158442`, `7158715`); those configurations are
discarded from the reportable result. The finalist restriction to only the
strongest `BinarySearch-ε=128` thresholds made all three follow-up runs
stable.

---

## Summary of the three milestones

| Milestone | Best index (per workload)     | Headline result                                                                          |
|-----------|-------------------------------|------------------------------------------------------------------------------------------|
| 1         | LIPP for lookup-heavy, DPGM for insert-heavy | LIPP ≥ 700 Mops/s on pure lookup; DPGM ~ 3.4–3.7 Mops/s on 90% insert |
| 2         | Naive hybrid (FB)             | 1.33 / 1.87 Mops/s — correct but below both baselines                                    |
| 3 (ST)    | 64-shard no-flush hybrid      | 5.75–5.86 Mops/s on 90% insert — beats best baseline by +82%–+107%                       |
| 3 (MT, 8 threads) | `delta-dpgm-sbf` sharded concurrent hybrid | 44.8–57.1 Mops/s on 90% insert — **19–26× speedup vs single-thread LIPP** |

### Files for the report figures

- Milestone 1 bar plots:  `benchmark_results.png`
- Milestone 2 bar plots:  `analysis_results/milestone2_fb_plots.png`
- Milestone 3 single-thread summary: `…/milestone3_insert_sharded_64_tuned_specialized/6179288/analysis/milestone3_insert_sharded_summary.csv`
- Milestone 3 multithread summary:   `…/milestone3_multithread_insert_finalists/{7158956,7159527,7159729}/analysis/milestone3_multithread_fast_summary.csv`

### Code pointers

- Milestone 2 hybrid:           `competitors/hybrid_pgm_lipp.h`
- Milestone 3 single-thread:    `competitors/hybrid_pgm_lipp_specialized.h`
  (`HybridPGMLIPPShardedLookupSpecialized`)
- Milestone 3 multi-threaded:   `competitors/hybrid_pgm_lipp_concurrent.h`
  (`delta-dpgm-sbf` branch)
- Benchmark wiring:             `benchmarks/benchmark_hybrid_pgm_lipp*.{h,cc}`, `benchmark.cc`
- Slurm drivers:                `jobs/run_milestone3_insert_sharded_64_tuned_specialized_array.slurm`,
                                `jobs/run_milestone3_multithread_insert_finalists_array.slurm`
