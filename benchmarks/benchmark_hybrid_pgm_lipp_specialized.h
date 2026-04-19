#pragma once

#include "benchmark.h"

void benchmark_64_hybrid_pgm_lipp_specialized(tli::Benchmark<uint64_t>& benchmark,
                                              const std::string& filename);

void benchmark_64_hybrid_pgm_lipp_insert_specialized(tli::Benchmark<uint64_t>& benchmark);

void benchmark_64_hybrid_pgm_lipp_insert_tuned_specialized(
    tli::Benchmark<uint64_t>& benchmark);

void benchmark_64_hybrid_pgm_lipp_lookup_specialized_0(tli::Benchmark<uint64_t>& benchmark);

void benchmark_64_hybrid_pgm_lipp_lookup_specialized_6(tli::Benchmark<uint64_t>& benchmark);

void benchmark_64_hybrid_pgm_lipp_lookup_sharded_specialized_4(
    tli::Benchmark<uint64_t>& benchmark);

void benchmark_64_hybrid_pgm_lipp_lookup_sharded_specialized_2(
    tli::Benchmark<uint64_t>& benchmark);

void benchmark_64_hybrid_pgm_lipp_lookup_sharded_specialized_3(
    tli::Benchmark<uint64_t>& benchmark);

void benchmark_64_hybrid_pgm_lipp_lookup_sharded_specialized_6(
    tli::Benchmark<uint64_t>& benchmark);

void benchmark_64_hybrid_pgm_lipp_lookup_sharded_specialized_8(
    tli::Benchmark<uint64_t>& benchmark);

void benchmark_64_hybrid_pgm_lipp_lookup_sharded_tuned_specialized(
    tli::Benchmark<uint64_t>& benchmark);

void benchmark_64_hybrid_pgm_lipp_lookup_sharded_high_tuned_specialized(
    tli::Benchmark<uint64_t>& benchmark);

void benchmark_64_hybrid_pgm_lipp_lookup_sharded_64_tuned_specialized(
    tli::Benchmark<uint64_t>& benchmark);

void benchmark_64_hybrid_pgm_lipp_epoch_ring_specialized(
    tli::Benchmark<uint64_t>& benchmark);

void benchmark_64_hybrid_pgm_lipp_lookup_batch_delta_lipp_specialized_32768(
    tli::Benchmark<uint64_t>& benchmark);

void benchmark_64_hybrid_pgm_lipp_lookup_batch_delta_lipp_specialized_65536(
    tli::Benchmark<uint64_t>& benchmark);

void benchmark_64_hybrid_pgm_lipp_lookup_batch_delta_lipp_specialized_131072(
    tli::Benchmark<uint64_t>& benchmark);

void benchmark_64_hybrid_pgm_lipp_lookup_oneshot_delta_lipp_specialized(
    tli::Benchmark<uint64_t>& benchmark);

void benchmark_64_hybrid_pgm_lipp_lookup_sorted_drain_specialized(
    tli::Benchmark<uint64_t>& benchmark);

void benchmark_64_hybrid_pgm_lipp_lazy_write_through_specialized(
    tli::Benchmark<uint64_t>& benchmark);

void benchmark_64_hybrid_pgm_lipp_sentinel_marker_specialized(
    tli::Benchmark<uint64_t>& benchmark);

void benchmark_64_hybrid_pgm_lipp_auto_switch_specialized(
    tli::Benchmark<uint64_t>& benchmark);

void benchmark_64_hybrid_pgm_lipp_auto_switch_write_through_specialized(
    tli::Benchmark<uint64_t>& benchmark);

void benchmark_64_hybrid_pgm_lipp_write_through_specialized(tli::Benchmark<uint64_t>& benchmark);

void benchmark_64_hybrid_pgm_lipp_workload_aware_specialized(
    tli::Benchmark<uint64_t>& benchmark, const std::string& filename);

void benchmark_64_hybrid_pgm_lipp_concurrent_workload_aware_specialized(
    tli::Benchmark<uint64_t>& benchmark, const std::string& filename);

void benchmark_64_hybrid_pgm_lipp_concurrent_workload_aware_specialized_params(
    tli::Benchmark<uint64_t>& benchmark, const std::vector<int>& params);
