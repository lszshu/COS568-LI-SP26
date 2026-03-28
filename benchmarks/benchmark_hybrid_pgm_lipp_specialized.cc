#include "benchmarks/benchmark_hybrid_pgm_lipp_specialized.h"

#include "benchmark.h"
#include "competitors/hybrid_pgm_lipp_direct_lipp_specialized.h"
#include "competitors/hybrid_pgm_lipp_specialized.h"

void benchmark_64_hybrid_pgm_lipp_specialized(tli::Benchmark<uint64_t>& benchmark,
                                              const std::string& filename) {
  if (filename.find("0.100000i") != std::string::npos) {
    benchmark.template Run<HybridPGMLIPPDirectLippSpecialized<uint64_t>>();
    benchmark.template Run<HybridPGMLIPPWriteThroughSpecialized<uint64_t>>();
    benchmark.template Run<HybridPGMLIPPLookupSpecialized<uint64_t, 0>>();
    benchmark.template Run<HybridPGMLIPPLookupSpecialized<uint64_t, 6>>();
    benchmark.template Run<HybridPGMLIPPShardedLookupSpecialized<uint64_t, 4>>();
    benchmark.template Run<HybridPGMLIPPShardedLookupSpecialized<uint64_t, 6>>();
    benchmark.template Run<HybridPGMLIPPShardedLookupSpecialized<uint64_t, 8>>();
    benchmark.template Run<HybridPGMLIPPBatchDeltaLippSpecialized<uint64_t, 32768>>();
    benchmark.template Run<HybridPGMLIPPBatchDeltaLippSpecialized<uint64_t, 65536>>();
    benchmark.template Run<HybridPGMLIPPBatchDeltaLippSpecialized<uint64_t, 131072>>();
    return;
  }

  if (filename.find("0.900000i") != std::string::npos) {
    benchmark.template Run<HybridPGMLIPPInsertSpecialized<uint64_t>>();
    return;
  }

  benchmark.template Run<HybridPGMLIPPInsertSpecialized<uint64_t>>();
}

void benchmark_64_hybrid_pgm_lipp_insert_specialized(tli::Benchmark<uint64_t>& benchmark) {
  benchmark.template Run<HybridPGMLIPPInsertSpecialized<uint64_t>>();
}

void benchmark_64_hybrid_pgm_lipp_lookup_specialized_0(tli::Benchmark<uint64_t>& benchmark) {
  benchmark.template Run<HybridPGMLIPPLookupSpecialized<uint64_t, 0>>();
}

void benchmark_64_hybrid_pgm_lipp_lookup_specialized_6(tli::Benchmark<uint64_t>& benchmark) {
  benchmark.template Run<HybridPGMLIPPLookupSpecialized<uint64_t, 6>>();
}

void benchmark_64_hybrid_pgm_lipp_lookup_sharded_specialized_4(
    tli::Benchmark<uint64_t>& benchmark) {
  benchmark.template Run<HybridPGMLIPPShardedLookupSpecialized<uint64_t, 4>>();
}

void benchmark_64_hybrid_pgm_lipp_lookup_sharded_specialized_6(
    tli::Benchmark<uint64_t>& benchmark) {
  benchmark.template Run<HybridPGMLIPPShardedLookupSpecialized<uint64_t, 6>>();
}

void benchmark_64_hybrid_pgm_lipp_lookup_sharded_specialized_8(
    tli::Benchmark<uint64_t>& benchmark) {
  benchmark.template Run<HybridPGMLIPPShardedLookupSpecialized<uint64_t, 8>>();
}

void benchmark_64_hybrid_pgm_lipp_lookup_batch_delta_lipp_specialized_32768(
    tli::Benchmark<uint64_t>& benchmark) {
  benchmark.template Run<HybridPGMLIPPBatchDeltaLippSpecialized<uint64_t, 32768>>();
}

void benchmark_64_hybrid_pgm_lipp_lookup_batch_delta_lipp_specialized_65536(
    tli::Benchmark<uint64_t>& benchmark) {
  benchmark.template Run<HybridPGMLIPPBatchDeltaLippSpecialized<uint64_t, 65536>>();
}

void benchmark_64_hybrid_pgm_lipp_lookup_batch_delta_lipp_specialized_131072(
    tli::Benchmark<uint64_t>& benchmark) {
  benchmark.template Run<HybridPGMLIPPBatchDeltaLippSpecialized<uint64_t, 131072>>();
}

void benchmark_64_hybrid_pgm_lipp_write_through_specialized(tli::Benchmark<uint64_t>& benchmark) {
  benchmark.template Run<HybridPGMLIPPWriteThroughSpecialized<uint64_t>>();
}
