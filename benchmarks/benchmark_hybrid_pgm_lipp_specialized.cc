#include "benchmarks/benchmark_hybrid_pgm_lipp_specialized.h"

#include "benchmark.h"
#include "competitors/hybrid_pgm_lipp_concurrent.h"
#include "competitors/hybrid_pgm_lipp_direct_lipp_specialized.h"
#include "competitors/hybrid_pgm_lipp_specialized.h"
#include "searches/interpolation_search.h"
#include "searches/linear_search.h"

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
    benchmark.template Run<HybridPGMLIPPOneShotDeltaLippSpecialized<uint64_t, 32768>>();
    benchmark.template Run<HybridPGMLIPPOneShotDeltaLippSpecialized<uint64_t, 65536>>();
    benchmark.template Run<HybridPGMLIPPOneShotDeltaLippSpecialized<uint64_t, 131072>>();
    benchmark.template Run<
        HybridPGMLIPPOneShotDeltaLippSpecialized<uint64_t, 65536, InterpolationSearch<0>, 256>>();
    benchmark.template Run<HybridPGMLIPPSortedDrainSpecialized<uint64_t, 8192, 8>>();
    benchmark.template Run<HybridPGMLIPPSortedDrainSpecialized<uint64_t, 8192, 16>>();
    benchmark.template Run<HybridPGMLIPPSortedDrainSpecialized<uint64_t, 16384, 16>>();
    benchmark.template Run<HybridPGMLIPPSortedDrainSpecialized<uint64_t, 16384, 32>>();
    benchmark.template Run<HybridPGMLIPPSortedDrainSpecialized<uint64_t, 32768, 32>>();
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

void benchmark_64_hybrid_pgm_lipp_insert_tuned_specialized(
    tli::Benchmark<uint64_t>& benchmark) {
  benchmark.template Run<
      HybridPGMLIPPInsertSpecialized<uint64_t, BranchingBinarySearch<0>, 64>>();
  benchmark.template Run<
      HybridPGMLIPPInsertSpecialized<uint64_t, BranchingBinarySearch<0>, 128>>();
  benchmark.template Run<
      HybridPGMLIPPInsertSpecialized<uint64_t, BranchingBinarySearch<0>, 256>>();
  benchmark.template Run<
      HybridPGMLIPPInsertSpecialized<uint64_t, BranchingBinarySearch<0>, 512>>();
  benchmark.template Run<
      HybridPGMLIPPInsertSpecialized<uint64_t, BranchingBinarySearch<0>, 1024>>();
  benchmark.template Run<
      HybridPGMLIPPInsertSpecialized<uint64_t, BranchingBinarySearch<0>, 2048>>();
  benchmark.template Run<
      HybridPGMLIPPInsertSpecialized<uint64_t, InterpolationSearch<0>, 64>>();
  benchmark.template Run<
      HybridPGMLIPPInsertSpecialized<uint64_t, InterpolationSearch<0>, 128>>();
  benchmark.template Run<
      HybridPGMLIPPInsertSpecialized<uint64_t, InterpolationSearch<0>, 256>>();
  benchmark.template Run<
      HybridPGMLIPPInsertSpecialized<uint64_t, InterpolationSearch<0>, 512>>();
  benchmark.template Run<
      HybridPGMLIPPInsertSpecialized<uint64_t, InterpolationSearch<0>, 1024>>();
  benchmark.template Run<
      HybridPGMLIPPInsertSpecialized<uint64_t, InterpolationSearch<0>, 2048>>();
  benchmark.template Run<HybridPGMLIPPInsertSpecialized<uint64_t, LinearSearch<0>, 32>>();
  benchmark.template Run<HybridPGMLIPPInsertSpecialized<uint64_t, LinearSearch<0>, 64>>();
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

void benchmark_64_hybrid_pgm_lipp_lookup_sharded_specialized_2(
    tli::Benchmark<uint64_t>& benchmark) {
  benchmark.template Run<HybridPGMLIPPShardedLookupSpecialized<uint64_t, 2>>();
}

void benchmark_64_hybrid_pgm_lipp_lookup_sharded_specialized_3(
    tli::Benchmark<uint64_t>& benchmark) {
  benchmark.template Run<HybridPGMLIPPShardedLookupSpecialized<uint64_t, 3>>();
}

void benchmark_64_hybrid_pgm_lipp_lookup_sharded_specialized_6(
    tli::Benchmark<uint64_t>& benchmark) {
  benchmark.template Run<HybridPGMLIPPShardedLookupSpecialized<uint64_t, 6>>();
}

void benchmark_64_hybrid_pgm_lipp_lookup_sharded_specialized_8(
    tli::Benchmark<uint64_t>& benchmark) {
  benchmark.template Run<HybridPGMLIPPShardedLookupSpecialized<uint64_t, 8>>();
}

void benchmark_64_hybrid_pgm_lipp_lookup_sharded_tuned_specialized(
    tli::Benchmark<uint64_t>& benchmark) {
  benchmark.template Run<
      HybridPGMLIPPShardedLookupSpecialized<uint64_t, 3, BranchingBinarySearch<0>, 64>>();
  benchmark.template Run<
      HybridPGMLIPPShardedLookupSpecialized<uint64_t, 3, BranchingBinarySearch<0>, 128>>();
  benchmark.template Run<
      HybridPGMLIPPShardedLookupSpecialized<uint64_t, 3, BranchingBinarySearch<0>, 256>>();
  benchmark.template Run<
      HybridPGMLIPPShardedLookupSpecialized<uint64_t, 3, BranchingBinarySearch<0>, 512>>();
  benchmark.template Run<
      HybridPGMLIPPShardedLookupSpecialized<uint64_t, 3, BranchingBinarySearch<0>, 1024>>();
  benchmark.template Run<
      HybridPGMLIPPShardedLookupSpecialized<uint64_t, 3, InterpolationSearch<0>, 128>>();
  benchmark.template Run<
      HybridPGMLIPPShardedLookupSpecialized<uint64_t, 3, InterpolationSearch<0>, 256>>();
  benchmark.template Run<
      HybridPGMLIPPShardedLookupSpecialized<uint64_t, 3, InterpolationSearch<0>, 512>>();
}

void benchmark_64_hybrid_pgm_lipp_lookup_sharded_high_tuned_specialized(
    tli::Benchmark<uint64_t>& benchmark) {
  benchmark.template Run<
      HybridPGMLIPPShardedLookupSpecialized<uint64_t, 6, BranchingBinarySearch<0>, 128>>();
  benchmark.template Run<
      HybridPGMLIPPShardedLookupSpecialized<uint64_t, 6, BranchingBinarySearch<0>, 512>>();
  benchmark.template Run<
      HybridPGMLIPPShardedLookupSpecialized<uint64_t, 6, BranchingBinarySearch<0>, 1024>>();
  benchmark.template Run<
      HybridPGMLIPPShardedLookupSpecialized<uint64_t, 6, InterpolationSearch<0>, 128>>();
  benchmark.template Run<
      HybridPGMLIPPShardedLookupSpecialized<uint64_t, 8, BranchingBinarySearch<0>, 128>>();
  benchmark.template Run<
      HybridPGMLIPPShardedLookupSpecialized<uint64_t, 8, BranchingBinarySearch<0>, 512>>();
  benchmark.template Run<
      HybridPGMLIPPShardedLookupSpecialized<uint64_t, 8, BranchingBinarySearch<0>, 1024>>();
  benchmark.template Run<
      HybridPGMLIPPShardedLookupSpecialized<uint64_t, 8, BranchingBinarySearch<0>, 2048>>();
  benchmark.template Run<
      HybridPGMLIPPShardedLookupSpecialized<uint64_t, 8, InterpolationSearch<0>, 128>>();
  benchmark.template Run<
      HybridPGMLIPPShardedLookupSpecialized<uint64_t, 8, InterpolationSearch<0>, 256>>();
  benchmark.template Run<
      HybridPGMLIPPShardedLookupSpecialized<uint64_t, 8, InterpolationSearch<0>, 512>>();
  benchmark.template Run<
      HybridPGMLIPPShardedLookupSpecialized<uint64_t, 8, InterpolationSearch<0>, 1024>>();
}

void benchmark_64_hybrid_pgm_lipp_lookup_sharded_64_tuned_specialized(
    tli::Benchmark<uint64_t>& benchmark) {
  benchmark.template Run<
      HybridPGMLIPPShardedLookupSpecialized<uint64_t, 6, BranchingBinarySearch<0>, 128>>();
  benchmark.template Run<
      HybridPGMLIPPShardedLookupSpecialized<uint64_t, 6, BranchingBinarySearch<0>, 512>>();
  benchmark.template Run<
      HybridPGMLIPPShardedLookupSpecialized<uint64_t, 6, BranchingBinarySearch<0>, 1024>>();
  benchmark.template Run<
      HybridPGMLIPPShardedLookupSpecialized<uint64_t, 6, InterpolationSearch<0>, 128>>();
}

void benchmark_64_hybrid_pgm_lipp_epoch_ring_specialized(
    tli::Benchmark<uint64_t>& benchmark) {
  benchmark.template Run<HybridPGMLIPPEpochRingSpecialized<uint64_t, 4, 262144>>();
  benchmark.template Run<HybridPGMLIPPEpochRingSpecialized<uint64_t, 8, 262144>>();
  benchmark.template Run<HybridPGMLIPPEpochRingSpecialized<uint64_t, 4, 524288>>();
  benchmark.template Run<HybridPGMLIPPEpochRingSpecialized<uint64_t, 8, 524288>>();
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

void benchmark_64_hybrid_pgm_lipp_lookup_oneshot_delta_lipp_specialized(
    tli::Benchmark<uint64_t>& benchmark) {
  benchmark.template Run<HybridPGMLIPPOneShotDeltaLippSpecialized<uint64_t, 32768>>();
  benchmark.template Run<HybridPGMLIPPOneShotDeltaLippSpecialized<uint64_t, 65536>>();
  benchmark.template Run<HybridPGMLIPPOneShotDeltaLippSpecialized<uint64_t, 131072>>();
  benchmark.template Run<
      HybridPGMLIPPOneShotDeltaLippSpecialized<uint64_t, 65536, InterpolationSearch<0>, 256>>();
}

void benchmark_64_hybrid_pgm_lipp_lookup_sorted_drain_specialized(
    tli::Benchmark<uint64_t>& benchmark) {
  benchmark.template Run<HybridPGMLIPPSortedDrainSpecialized<uint64_t, 8192, 8>>();
  benchmark.template Run<HybridPGMLIPPSortedDrainSpecialized<uint64_t, 8192, 16>>();
  benchmark.template Run<HybridPGMLIPPSortedDrainSpecialized<uint64_t, 16384, 16>>();
  benchmark.template Run<HybridPGMLIPPSortedDrainSpecialized<uint64_t, 16384, 32>>();
  benchmark.template Run<HybridPGMLIPPSortedDrainSpecialized<uint64_t, 32768, 32>>();
}

void benchmark_64_hybrid_pgm_lipp_lazy_write_through_specialized(
    tli::Benchmark<uint64_t>& benchmark) {
  benchmark.template Run<HybridPGMLIPPLazyWriteThroughSpecialized<uint64_t, 64>>();
  benchmark.template Run<HybridPGMLIPPLazyWriteThroughSpecialized<uint64_t, 256>>();
  benchmark.template Run<HybridPGMLIPPLazyWriteThroughSpecialized<uint64_t, 1024>>();
}

void benchmark_64_hybrid_pgm_lipp_sentinel_marker_specialized(
    tli::Benchmark<uint64_t>& benchmark) {
  benchmark.template Run<HybridPGMLIPPSentinelMarkerSpecialized<uint64_t>>();
  benchmark.template Run<
      HybridPGMLIPPSentinelMarkerSpecialized<uint64_t, InterpolationSearch<0>, 256>>();
  benchmark.template Run<
      HybridPGMLIPPSentinelMarkerSpecialized<uint64_t, BranchingBinarySearch<0>, 256>>();
}

void benchmark_64_hybrid_pgm_lipp_auto_switch_specialized(
    tli::Benchmark<uint64_t>& benchmark) {
  benchmark.template Run<HybridPGMLIPPAutoSwitchSpecialized<uint64_t, 1024, 256, 50>>();
  benchmark.template Run<HybridPGMLIPPAutoSwitchSpecialized<uint64_t, 4096, 256, 50>>();
  benchmark.template Run<HybridPGMLIPPAutoSwitchSpecialized<uint64_t, 2048, 128, 40>>();
}

void benchmark_64_hybrid_pgm_lipp_auto_switch_write_through_specialized(
    tli::Benchmark<uint64_t>& benchmark) {
  benchmark.template Run<HybridPGMLIPPAutoSwitchWriteThroughSpecialized<uint64_t, 1024, 50>>();
  benchmark.template Run<HybridPGMLIPPAutoSwitchWriteThroughSpecialized<uint64_t, 4096, 50>>();
  benchmark.template Run<HybridPGMLIPPAutoSwitchWriteThroughSpecialized<uint64_t, 2048, 40>>();
}

void benchmark_64_hybrid_pgm_lipp_write_through_specialized(tli::Benchmark<uint64_t>& benchmark) {
  benchmark.template Run<HybridPGMLIPPWriteThroughSpecialized<uint64_t>>();
}

void benchmark_64_hybrid_pgm_lipp_workload_aware_specialized(
    tli::Benchmark<uint64_t>& benchmark, const std::string& filename) {
  if (filename.find("0.100000i") != std::string::npos) {
    benchmark.template Run<HybridPGMLIPPWorkloadAwareSpecialized<uint64_t>>({0, 0});
    return;
  }

  if (filename.find("0.900000i") != std::string::npos) {
    const int shard_bits =
        filename.find("osmc_100M_public_uint64") != std::string::npos ? 6 : 8;
    benchmark.template Run<HybridPGMLIPPWorkloadAwareSpecialized<uint64_t>>({1, shard_bits});
    return;
  }

  benchmark.template Run<HybridPGMLIPPWorkloadAwareSpecialized<uint64_t>>({0, 0});
}

void benchmark_64_hybrid_pgm_lipp_concurrent_workload_aware_specialized(
    tli::Benchmark<uint64_t>& benchmark, const std::string& filename) {
  if (filename.find("0.100000i") != std::string::npos) {
    benchmark.template Run<HybridPGMLIPPConcurrentWorkloadAware<uint64_t>>({0, 4});
    benchmark.template Run<HybridPGMLIPPConcurrentWorkloadAware<uint64_t>>({0, 6});
    benchmark.template Run<HybridPGMLIPPConcurrentWorkloadAware<uint64_t>>({0, 8});
    benchmark.template Run<HybridPGMLIPPConcurrentWorkloadAware<uint64_t>>({2, 4});
    benchmark.template Run<HybridPGMLIPPConcurrentWorkloadAware<uint64_t>>({2, 6});
    benchmark.template Run<HybridPGMLIPPConcurrentWorkloadAware<uint64_t>>({2, 8});
    return;
  }

  if (filename.find("0.900000i") != std::string::npos) {
    benchmark.template Run<HybridPGMLIPPConcurrentWorkloadAware<uint64_t>>({1, 4});
    benchmark.template Run<HybridPGMLIPPConcurrentWorkloadAware<uint64_t>>({1, 5});
    benchmark.template Run<HybridPGMLIPPConcurrentWorkloadAware<uint64_t>>({1, 6});
    benchmark.template Run<HybridPGMLIPPConcurrentWorkloadAware<uint64_t>>({1, 8});
    return;
  }

  benchmark.template Run<HybridPGMLIPPConcurrentWorkloadAware<uint64_t>>({0, 8});
}

void benchmark_64_hybrid_pgm_lipp_concurrent_workload_aware_specialized_params(
    tli::Benchmark<uint64_t>& benchmark, const std::vector<int>& params) {
  benchmark.template Run<HybridPGMLIPPConcurrentWorkloadAware<uint64_t>>(params);
}
