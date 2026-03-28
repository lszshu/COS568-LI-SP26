#include "benchmarks/benchmark_hybrid_pgm_lipp_incremental.h"

#include "benchmark.h"
#include "competitors/hybrid_pgm_lipp_incremental.h"

void benchmark_64_hybrid_pgm_lipp_incremental(tli::Benchmark<uint64_t>& benchmark,
                                              const std::string& filename) {
  if (filename.find("0.100000i") != std::string::npos) {
    benchmark.template Run<HybridPGMLIPPIncremental<uint64_t>>({32768, 64, 64, 1, 1, 0});
    benchmark.template Run<HybridPGMLIPPIncremental<uint64_t>>({32768, 128, 128, 1, 1, 0});
    benchmark.template Run<HybridPGMLIPPIncremental<uint64_t>>({65536, 128, 64, 1, 1, 0});
    benchmark.template Run<HybridPGMLIPPIncremental<uint64_t>>({262144, 1, 0, 1, 1, 0});
    benchmark.template Run<HybridPGMLIPPIncremental<uint64_t>>({262144, 1, 0, 1, 1, 6});
    benchmark.template Run<HybridPGMLIPPIncremental<uint64_t>>({262144, 1, 0, 1, 0, 0});
    benchmark.template Run<HybridPGMLIPPIncremental<uint64_t>>({131072, 0, 0, 0, 0, 0, 0, 1});
    return;
  }

  if (filename.find("0.900000i") != std::string::npos) {
    benchmark.template Run<HybridPGMLIPPIncremental<uint64_t>>({262144, 2048, 0, 0, 0, 0});
    benchmark.template Run<HybridPGMLIPPIncremental<uint64_t>>({262144, 2048, 0, 1, 0, 0});
    benchmark.template Run<HybridPGMLIPPIncremental<uint64_t>>({524288, 2048, 0, 1, 0, 0});
    benchmark.template Run<HybridPGMLIPPIncremental<uint64_t>>({1048576, 1, 0, 1, 0, 0});
    benchmark.template Run<HybridPGMLIPPIncremental<uint64_t>>({2097152, 1, 0, 1, 0, 0});
    benchmark.template Run<HybridPGMLIPPIncremental<uint64_t>>({2097152, 1, 0, 0, 0, 0});
    return;
  }

  benchmark.template Run<HybridPGMLIPPIncremental<uint64_t>>();
}

void benchmark_64_hybrid_pgm_lipp_incremental_params(
    tli::Benchmark<uint64_t>& benchmark, const std::vector<int>& params) {
  benchmark.template Run<HybridPGMLIPPIncremental<uint64_t>>(params);
}
