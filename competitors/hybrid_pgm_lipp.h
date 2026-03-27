#pragma once

#include <cstddef>
#include <limits>
#include <string>
#include <vector>

#include "./lipp/src/core/lipp.h"
#include "PGM-index/include/pgm_index_dynamic.hpp"
#include "base.h"
#include "searches/branching_binary_search.h"

template <class KeyType>
class HybridPGMLIPP : public Base<KeyType> {
 public:
  HybridPGMLIPP(const std::vector<int>& params) {
    if (!params.empty() && params[0] > 0) {
      flush_threshold_ = static_cast<std::size_t>(params[0]);
    }
  }

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

    auto buffer_it = buffer_.find(lookup_key);
    if (buffer_it != buffer_.end()) {
      return buffer_it->value();
    }

    uint64_t value = util::NOT_FOUND;
    if (!lipp_.find(lookup_key, value)) {
      return util::OVERFLOW;
    }
    return value;
  }

  uint64_t RangeQuery(const KeyType& lower_key, const KeyType& upper_key, uint32_t thread_id) const {
    (void) thread_id;

    uint64_t result = 0;

    auto lipp_it = lipp_.lower_bound(lower_key);
    while (lipp_it != lipp_.end() && lipp_it->comp.data.key <= upper_key) {
      result += lipp_it->comp.data.value;
      ++lipp_it;
    }

    auto buffer_it = buffer_.lower_bound(lower_key);
    while (buffer_it != buffer_.end() && buffer_it->key() <= upper_key) {
      result += buffer_it->value();
      ++buffer_it;
    }

    return result;
  }

  void Insert(const KeyValue<KeyType>& data, uint32_t thread_id) {
    (void) thread_id;

    buffer_.insert(data.key, data.value);
    ++buffer_item_count_;
    if (buffer_item_count_ >= flush_threshold_) {
      flush_buffer();
    }
  }

  bool applicable(bool unique, bool range_query, bool insert, bool multithread, const std::string& ops_filename) const {
    (void) range_query;
    (void) insert;
    (void) ops_filename;
    return unique && !multithread;
  }

  std::string name() const { return "HybridPGMLIPP"; }

  std::vector<std::string> variants() const {
    return {"naive-flush", std::to_string(flush_threshold_)};
  }

  std::size_t size() const {
    return lipp_.index_size() + buffer_.size_in_bytes();
  }

 private:
  static constexpr std::size_t kDefaultFlushThreshold = 1u << 17;
  static constexpr std::size_t kDynamicPGMError = 128;

  using BufferSearch = BranchingBinarySearch<0>;
  using BufferPGM = PGMIndex<KeyType, BufferSearch, kDynamicPGMError, 16>;
  using BufferIndex = DynamicPGMIndex<KeyType, uint64_t, BufferSearch, BufferPGM>;

  void flush_buffer() {
    for (auto it = buffer_.lower_bound(std::numeric_limits<KeyType>::min()); it != buffer_.end(); ++it) {
      lipp_.insert(it->key(), it->value());
    }

    buffer_ = BufferIndex();
    buffer_item_count_ = 0;
  }

  LIPP<KeyType, uint64_t> lipp_;
  BufferIndex buffer_;
  std::size_t flush_threshold_ = kDefaultFlushThreshold;
  std::size_t buffer_item_count_ = 0;
};
