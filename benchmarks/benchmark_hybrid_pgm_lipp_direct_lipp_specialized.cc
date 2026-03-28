#include "benchmarks/benchmark_hybrid_pgm_lipp_direct_lipp_specialized.h"

#include "benchmark.h"
#include "competitors/hybrid_pgm_lipp_direct_lipp_specialized.h"

void benchmark_64_hybrid_pgm_lipp_direct_lipp_specialized(
    tli::Benchmark<uint64_t>& benchmark) {
  benchmark.template Run<HybridPGMLIPPDirectLippSpecialized<uint64_t>>();
}
