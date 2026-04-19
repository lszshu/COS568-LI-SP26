#include "benchmarks/benchmark_lipp.h"

#include "benchmark.h"
#include "common.h"
#include "competitors/lipp.h"
#include "competitors/lipp_threadsafe.h"

void benchmark_64_lipp(tli::Benchmark<uint64_t>& benchmark) {
  benchmark.template Run<Lipp<uint64_t>>();
}

void benchmark_64_lipp_threadsafe(tli::Benchmark<uint64_t>& benchmark) {
  benchmark.template Run<LippThreadSafe<uint64_t>>();
}
