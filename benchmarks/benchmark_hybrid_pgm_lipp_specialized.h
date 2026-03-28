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

void benchmark_64_hybrid_pgm_lipp_lookup_sharded_specialized_6(
    tli::Benchmark<uint64_t>& benchmark);

void benchmark_64_hybrid_pgm_lipp_lookup_sharded_specialized_8(
    tli::Benchmark<uint64_t>& benchmark);

void benchmark_64_hybrid_pgm_lipp_lookup_batch_delta_lipp_specialized_32768(
    tli::Benchmark<uint64_t>& benchmark);

void benchmark_64_hybrid_pgm_lipp_lookup_batch_delta_lipp_specialized_65536(
    tli::Benchmark<uint64_t>& benchmark);

void benchmark_64_hybrid_pgm_lipp_lookup_batch_delta_lipp_specialized_131072(
    tli::Benchmark<uint64_t>& benchmark);

void benchmark_64_hybrid_pgm_lipp_write_through_specialized(tli::Benchmark<uint64_t>& benchmark);
