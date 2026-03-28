#pragma once

#include "./lipp/src/core/lipp.h"
#include "base.h"

template <class KeyType>
class HybridPGMLIPPDirectLippSpecialized : public Base<KeyType> {
 public:
  HybridPGMLIPPDirectLippSpecialized(const std::vector<int>& params) { (void) params; }

  uint64_t Build(const std::vector<KeyValue<KeyType>>& data, size_t num_threads) {
    (void) num_threads;
    std::vector<std::pair<KeyType, uint64_t>> loading_data;
    loading_data.reserve(data.size());
    for (const auto& item : data) {
      loading_data.emplace_back(item.key, item.value);
    }
    return util::timing([&] { lipp_.bulk_load(loading_data.data(), loading_data.size()); });
  }

  size_t EqualityLookup(const KeyType& lookup_key, uint32_t thread_id) const {
    (void) thread_id;
    uint64_t value;
    if (!lipp_.find(lookup_key, value)) {
      return util::NOT_FOUND;
    }
    return value;
  }

  uint64_t RangeQuery(const KeyType& lower_key, const KeyType& upper_key, uint32_t thread_id) const {
    (void) thread_id;
    auto it = lipp_.lower_bound(lower_key);
    uint64_t result = 0;
    while (it != lipp_.end() && it->comp.data.key <= upper_key) {
      result += it->comp.data.value;
      ++it;
    }
    return result;
  }

  void Insert(const KeyValue<KeyType>& data, uint32_t thread_id) {
    (void) thread_id;
    lipp_.insert(data.key, data.value);
  }

  bool applicable(bool unique, bool range_query, bool insert, bool multithread,
                  const std::string& ops_filename) const {
    (void) range_query;
    (void) insert;
    (void) ops_filename;
    return unique && !multithread;
  }

  std::string name() const { return "HybridPGMLIPPDirectLippSpecialized"; }

  std::vector<std::string> variants() const { return {"direct-main-lipp-specialized"}; }

  std::size_t size() const { return lipp_.index_size(); }

 private:
  mutable LIPP<KeyType, uint64_t> lipp_;
};
