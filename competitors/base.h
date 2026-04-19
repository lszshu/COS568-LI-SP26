#pragma once

#include "../util.h"
#include "searches/search.h"

template<class KeyType>
class Base {
 public:
  uint64_t Build(const std::vector<KeyValue<KeyType>>&, size_t) {
    return 0;
  }

  size_t EqualityLookup(const KeyType&, uint32_t) const {
    return util::NOT_FOUND;
  }

  uint64_t RangeQuery(const KeyType&, const KeyType&, uint32_t) const {
    return 0;
  }
  
  void Insert(const KeyValue<KeyType>&, uint32_t) {}

  std::string name() const { return "Unknown"; }

  std::size_t size() const { return 0; }

  bool applicable(bool, bool, bool, bool, const std::string&) const {
    return true;
  }

  std::vector<std::string> variants() const { 
    return std::vector<std::string>();
  }

  double searchAverageTime() const { return 0; }
  double searchLatency(uint64_t op_cnt) const { return 0; }
  double searchBound() const { return 0; }
  void initSearch() {}
  uint64_t runMultithread(void *(* func)(void *), FGParam *params,
                          size_t thread_cnt) {
    util::running = false;
    util::ready_threads = 0;
    const auto allowed_cpus = util::allowed_cpu_ids();

    std::vector<std::thread> workers;
    workers.reserve(thread_cnt);
    for (size_t worker_i = 0; worker_i < thread_cnt; ++worker_i) {
      workers.emplace_back([&, worker_i]() {
        if (!allowed_cpus.empty()) {
          util::set_cpu_affinity(allowed_cpus[worker_i % allowed_cpus.size()]);
        }
        func(reinterpret_cast<void*>(&params[worker_i]));
      });
    }

    while (util::ready_threads.load(std::memory_order_acquire) < thread_cnt) {
      std::this_thread::yield();
    }

    const auto start = std::chrono::high_resolution_clock::now();
    util::running = true;
    for (auto& worker : workers) {
      worker.join();
    }
    const auto end = std::chrono::high_resolution_clock::now();
    util::running = false;

    return std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
        .count();
  }
};

template<class KeyType, class SearchClass>
class Competitor: public Base<KeyType> {
 public:
  double searchAverageTime() const { return SearchClass::searchAverageTime(); }
  double searchLatency(uint64_t op_cnt) const { return (double)SearchClass::searchTotalTime() / op_cnt; }
  double searchBound() const { return SearchClass::searchBound(); }
  void initSearch() { SearchClass::initSearch(); }
};
