#pragma once

#include "benchmark.h"

void benchmark_64_hybrid_pgm_lipp_incremental(tli::Benchmark<uint64_t>& benchmark,
                                              const std::string& filename);

void benchmark_64_hybrid_pgm_lipp_incremental_params(
    tli::Benchmark<uint64_t>& benchmark, const std::vector<int>& params);
