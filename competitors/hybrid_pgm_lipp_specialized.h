#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "./lipp/src/core/lipp.h"
#include "PGM-index/include/pgm_index_dynamic.hpp"
#include "base.h"
#include "hybrid_pgm_lipp_direct_lipp_specialized.h"
#include "searches/branching_binary_search.h"

template <class KeyType, class SearchClass = BranchingBinarySearch<0>,
          std::size_t kDynamicPGMError = 128>
class HybridPGMLIPPInsertSpecialized : public Base<KeyType> {
 public:
  HybridPGMLIPPInsertSpecialized(const std::vector<int>& params) { (void) params; }

  uint64_t Build(const std::vector<KeyValue<KeyType>>& data, size_t num_threads) {
    (void) num_threads;
    std::vector<std::pair<KeyType, uint64_t>> loading_data;
    loading_data.reserve(data.size());
    for (const auto& item : data) {
      loading_data.emplace_back(item.key, item.value);
    }
    active_buffer_ = BufferIndex();
    return util::timing([&] { lipp_.bulk_load(loading_data.data(), loading_data.size()); });
  }

  size_t EqualityLookup(const KeyType& lookup_key, uint32_t thread_id) const {
    (void) thread_id;
    uint64_t value;
    if (lipp_.find(lookup_key, value)) {
      return value;
    }
    auto it = active_buffer_.find(lookup_key);
    return it != active_buffer_.end() ? it->value() : util::OVERFLOW;
  }

  uint64_t RangeQuery(const KeyType& lower_key, const KeyType& upper_key, uint32_t thread_id) const {
    (void) thread_id;
    uint64_t result = 0;
    auto lipp_it = lipp_.lower_bound(lower_key);
    while (lipp_it != lipp_.end() && lipp_it->comp.data.key <= upper_key) {
      result += lipp_it->comp.data.value;
      ++lipp_it;
    }

    auto buffer_it = active_buffer_.lower_bound(lower_key);
    while (buffer_it != active_buffer_.end() && buffer_it->key() <= upper_key) {
      result += buffer_it->value();
      ++buffer_it;
    }
    return result;
  }

  void Insert(const KeyValue<KeyType>& data, uint32_t thread_id) {
    (void) thread_id;
    active_buffer_.insert(data.key, data.value);
  }

  bool applicable(bool unique, bool range_query, bool insert, bool multithread,
                  const std::string& ops_filename) const {
    (void) range_query;
    (void) insert;
    (void) ops_filename;
    return unique && !multithread;
  }

  std::string name() const { return "HybridPGMLIPPSpecialized"; }

  std::vector<std::string> variants() const {
    return {SearchClass::name(), std::to_string(kDynamicPGMError)};
  }

  std::size_t size() const { return lipp_.index_size() + active_buffer_.size_in_bytes(); }

 private:
  using BufferSearch = SearchClass;
  using BufferPGM = PGMIndex<KeyType, BufferSearch, kDynamicPGMError, 16>;
  using BufferIndex = DynamicPGMIndex<KeyType, uint64_t, BufferSearch, BufferPGM>;

  mutable LIPP<KeyType, uint64_t> lipp_;
  mutable BufferIndex active_buffer_;
};

template <class KeyType, std::size_t BatchThreshold,
          class SearchClass = BranchingBinarySearch<0>,
          std::size_t kDynamicPGMError = 128>
class HybridPGMLIPPLazyWriteThroughSpecialized : public Base<KeyType> {
 public:
  HybridPGMLIPPLazyWriteThroughSpecialized(const std::vector<int>& params) { (void) params; }

  uint64_t Build(const std::vector<KeyValue<KeyType>>& data, size_t num_threads) {
    (void) num_threads;
    std::vector<std::pair<KeyType, uint64_t>> loading_data;
    loading_data.reserve(data.size());
    for (const auto& item : data) {
      loading_data.emplace_back(item.key, item.value);
    }
    reset_batch();
    return util::timing([&] { lipp_.bulk_load(loading_data.data(), loading_data.size()); });
  }

  size_t EqualityLookup(const KeyType& lookup_key, uint32_t thread_id) const {
    (void) thread_id;
    uint64_t value;
    if (lipp_.find(lookup_key, value)) {
      return value;
    }
    auto it = active_buffer_.find(lookup_key);
    return it != active_buffer_.end() ? it->value() : util::OVERFLOW;
  }

  uint64_t RangeQuery(const KeyType& lower_key, const KeyType& upper_key, uint32_t thread_id) const {
    (void) thread_id;
    uint64_t result = 0;
    auto lipp_it = lipp_.lower_bound(lower_key);
    while (lipp_it != lipp_.end() && lipp_it->comp.data.key <= upper_key) {
      result += lipp_it->comp.data.value;
      ++lipp_it;
    }

    auto buffer_it = active_buffer_.lower_bound(lower_key);
    while (buffer_it != active_buffer_.end() && buffer_it->key() <= upper_key) {
      result += buffer_it->value();
      ++buffer_it;
    }
    return result;
  }

  void Insert(const KeyValue<KeyType>& data, uint32_t thread_id) {
    (void) thread_id;
    track_active_min(data.key);
    active_buffer_.insert(data.key, data.value);
    ++active_count_;
    if (active_count_ >= BatchThreshold) {
      flush_batch_into_lipp();
    }
  }

  bool applicable(bool unique, bool range_query, bool insert, bool multithread,
                  const std::string& ops_filename) const {
    (void) range_query;
    (void) insert;
    (void) ops_filename;
    return unique && !multithread;
  }

  std::string name() const { return "HybridPGMLIPPSpecialized"; }

  std::vector<std::string> variants() const {
    return {"lazy-write-through-specialized", std::to_string(BatchThreshold),
            SearchClass::name(), std::to_string(kDynamicPGMError)};
  }

  std::size_t size() const { return lipp_.index_size() + active_buffer_.size_in_bytes(); }

 private:
  using BufferSearch = SearchClass;
  using BufferPGM = PGMIndex<KeyType, BufferSearch, kDynamicPGMError, 16>;
  using BufferIndex = DynamicPGMIndex<KeyType, uint64_t, BufferSearch, BufferPGM>;

  void reset_batch() {
    active_buffer_ = BufferIndex();
    active_count_ = 0;
    has_active_min_ = false;
  }

  void track_active_min(const KeyType& key) {
    if (!has_active_min_ || key < active_min_key_) {
      active_min_key_ = key;
      has_active_min_ = true;
    }
  }

  void flush_batch_into_lipp() {
    if (!has_active_min_ || active_count_ == 0) {
      reset_batch();
      return;
    }
    auto it = active_buffer_.lower_bound(active_min_key_);
    while (it != active_buffer_.end()) {
      lipp_.insert(it->key(), it->value());
      ++it;
    }
    reset_batch();
  }

  mutable LIPP<KeyType, uint64_t> lipp_;
  mutable BufferIndex active_buffer_;
  std::size_t active_count_ = 0;
  KeyType active_min_key_{};
  bool has_active_min_ = false;
};

template <class KeyType, class SearchClass = BranchingBinarySearch<0>,
          std::size_t kDynamicPGMError = 128>
class HybridPGMLIPPSentinelMarkerSpecialized : public Base<KeyType> {
 public:
  HybridPGMLIPPSentinelMarkerSpecialized(const std::vector<int>& params) { (void) params; }

  uint64_t Build(const std::vector<KeyValue<KeyType>>& data, size_t num_threads) {
    (void) num_threads;
    std::vector<std::pair<KeyType, uint64_t>> loading_data;
    loading_data.reserve(data.size());
    for (const auto& item : data) {
      loading_data.emplace_back(item.key, item.value);
    }
    active_buffer_ = BufferIndex();
    return util::timing([&] { lipp_.bulk_load(loading_data.data(), loading_data.size()); });
  }

  size_t EqualityLookup(const KeyType& lookup_key, uint32_t thread_id) const {
    (void) thread_id;
    uint64_t value;
    if (!lipp_.find(lookup_key, value)) {
      return util::OVERFLOW;
    }
    if (value != kSentinelValue) {
      return value;
    }
    auto it = active_buffer_.find(lookup_key);
    return it != active_buffer_.end() ? it->value() : util::OVERFLOW;
  }

  uint64_t RangeQuery(const KeyType& lower_key, const KeyType& upper_key, uint32_t thread_id) const {
    (void) thread_id;
    uint64_t result = 0;
    auto lipp_it = lipp_.lower_bound(lower_key);
    while (lipp_it != lipp_.end() && lipp_it->comp.data.key <= upper_key) {
      if (lipp_it->comp.data.value != kSentinelValue) {
        result += lipp_it->comp.data.value;
      }
      ++lipp_it;
    }

    auto buffer_it = active_buffer_.lower_bound(lower_key);
    while (buffer_it != active_buffer_.end() && buffer_it->key() <= upper_key) {
      result += buffer_it->value();
      ++buffer_it;
    }
    return result;
  }

  void Insert(const KeyValue<KeyType>& data, uint32_t thread_id) {
    (void) thread_id;
    lipp_.insert(data.key, kSentinelValue);
    active_buffer_.insert(data.key, data.value);
  }

  bool applicable(bool unique, bool range_query, bool insert, bool multithread,
                  const std::string& ops_filename) const {
    (void) range_query;
    (void) insert;
    (void) ops_filename;
    return unique && !multithread;
  }

  std::string name() const { return "HybridPGMLIPPSpecialized"; }

  std::vector<std::string> variants() const {
    return {"sentinel-marker-specialized", SearchClass::name(),
            std::to_string(kDynamicPGMError)};
  }

  std::size_t size() const { return lipp_.index_size() + active_buffer_.size_in_bytes(); }

 private:
  static constexpr uint64_t kSentinelValue = std::numeric_limits<uint64_t>::max();
  using BufferSearch = SearchClass;
  using BufferPGM = PGMIndex<KeyType, BufferSearch, kDynamicPGMError, 16>;
  using BufferIndex = DynamicPGMIndex<KeyType, uint64_t, BufferSearch, BufferPGM>;

  mutable LIPP<KeyType, uint64_t> lipp_;
  mutable BufferIndex active_buffer_;
};

template <class KeyType, std::size_t WindowSize, std::size_t BatchThreshold,
          std::size_t InsertHeavyPercent = 50,
          class SearchClass = BranchingBinarySearch<0>,
          std::size_t kDynamicPGMError = 128>
class HybridPGMLIPPAutoSwitchSpecialized : public Base<KeyType> {
 public:
  HybridPGMLIPPAutoSwitchSpecialized(const std::vector<int>& params) { (void) params; }

  uint64_t Build(const std::vector<KeyValue<KeyType>>& data, size_t num_threads) {
    (void) num_threads;
    std::vector<std::pair<KeyType, uint64_t>> loading_data;
    loading_data.reserve(data.size());
    for (const auto& item : data) {
      loading_data.emplace_back(item.key, item.value);
    }
    reset_state();
    return util::timing([&] { lipp_.bulk_load(loading_data.data(), loading_data.size()); });
  }

  size_t EqualityLookup(const KeyType& lookup_key, uint32_t thread_id) const {
    (void) thread_id;
    record_lookup();
    uint64_t value;
    if (lipp_.find(lookup_key, value)) {
      return value;
    }
    auto it = active_buffer_.find(lookup_key);
    return it != active_buffer_.end() ? it->value() : util::OVERFLOW;
  }

  uint64_t RangeQuery(const KeyType& lower_key, const KeyType& upper_key, uint32_t thread_id) const {
    (void) thread_id;
    record_lookup();
    uint64_t result = 0;
    auto lipp_it = lipp_.lower_bound(lower_key);
    while (lipp_it != lipp_.end() && lipp_it->comp.data.key <= upper_key) {
      result += lipp_it->comp.data.value;
      ++lipp_it;
    }

    auto buffer_it = active_buffer_.lower_bound(lower_key);
    while (buffer_it != active_buffer_.end() && buffer_it->key() <= upper_key) {
      result += buffer_it->value();
      ++buffer_it;
    }
    return result;
  }

  void Insert(const KeyValue<KeyType>& data, uint32_t thread_id) {
    (void) thread_id;
    record_insert();
    track_active_min(data.key);
    active_buffer_.insert(data.key, data.value);
    ++active_count_;
    if (lookup_favored_mode_ && active_count_ >= BatchThreshold) {
      flush_all_active_into_lipp();
    }
  }

  bool applicable(bool unique, bool range_query, bool insert, bool multithread,
                  const std::string& ops_filename) const {
    (void) range_query;
    (void) insert;
    (void) ops_filename;
    return unique && !multithread;
  }

  std::string name() const { return "HybridPGMLIPPSpecialized"; }

  std::vector<std::string> variants() const {
    return {"auto-switch-specialized", std::to_string(WindowSize),
            std::to_string(BatchThreshold), std::to_string(InsertHeavyPercent),
            SearchClass::name(), std::to_string(kDynamicPGMError)};
  }

  std::size_t size() const { return lipp_.index_size() + active_buffer_.size_in_bytes(); }

 private:
  using BufferSearch = SearchClass;
  using BufferPGM = PGMIndex<KeyType, BufferSearch, kDynamicPGMError, 16>;
  using BufferIndex = DynamicPGMIndex<KeyType, uint64_t, BufferSearch, BufferPGM>;

  void reset_state() {
    active_buffer_ = BufferIndex();
    active_count_ = 0;
    has_active_min_ = false;
    window_ops_ = 0;
    window_inserts_ = 0;
    lookup_favored_mode_ = true;
  }

  void track_active_min(const KeyType& key) {
    if (!has_active_min_ || key < active_min_key_) {
      active_min_key_ = key;
      has_active_min_ = true;
    }
  }

  void flush_all_active_into_lipp() const {
    if (!has_active_min_ || active_count_ == 0) {
      return;
    }
    auto it = active_buffer_.lower_bound(active_min_key_);
    while (it != active_buffer_.end()) {
      lipp_.insert(it->key(), it->value());
      ++it;
    }
    active_buffer_ = BufferIndex();
    active_count_ = 0;
    has_active_min_ = false;
  }

  void evaluate_window() const {
    if (window_ops_ < WindowSize) {
      return;
    }
    bool next_lookup_favored = window_inserts_ * 100 < window_ops_ * InsertHeavyPercent;
    if (next_lookup_favored && !lookup_favored_mode_) {
      flush_all_active_into_lipp();
    }
    lookup_favored_mode_ = next_lookup_favored;
    window_ops_ = 0;
    window_inserts_ = 0;
  }

  void record_lookup() const {
    ++window_ops_;
    evaluate_window();
  }

  void record_insert() const {
    ++window_ops_;
    ++window_inserts_;
    evaluate_window();
  }

  mutable LIPP<KeyType, uint64_t> lipp_;
  mutable BufferIndex active_buffer_;
  mutable std::size_t active_count_ = 0;
  mutable std::size_t window_ops_ = 0;
  mutable std::size_t window_inserts_ = 0;
  mutable KeyType active_min_key_{};
  mutable bool has_active_min_ = false;
  mutable bool lookup_favored_mode_ = true;
};

template <class KeyType, std::size_t WindowSize, std::size_t InsertHeavyPercent = 50,
          class SearchClass = BranchingBinarySearch<0>,
          std::size_t kDynamicPGMError = 128>
class HybridPGMLIPPAutoSwitchWriteThroughSpecialized : public Base<KeyType> {
 public:
  HybridPGMLIPPAutoSwitchWriteThroughSpecialized(const std::vector<int>& params) { (void) params; }

  uint64_t Build(const std::vector<KeyValue<KeyType>>& data, size_t num_threads) {
    (void) num_threads;
    std::vector<std::pair<KeyType, uint64_t>> loading_data;
    loading_data.reserve(data.size());
    for (const auto& item : data) {
      loading_data.emplace_back(item.key, item.value);
    }
    reset_state();
    return util::timing([&] { lipp_.bulk_load(loading_data.data(), loading_data.size()); });
  }

  size_t EqualityLookup(const KeyType& lookup_key, uint32_t thread_id) const {
    (void) thread_id;
    record_lookup();
    uint64_t value;
    if (lipp_.find(lookup_key, value)) {
      return value;
    }
    auto it = active_buffer_.find(lookup_key);
    return it != active_buffer_.end() ? it->value() : util::OVERFLOW;
  }

  uint64_t RangeQuery(const KeyType& lower_key, const KeyType& upper_key, uint32_t thread_id) const {
    (void) thread_id;
    record_lookup();
    uint64_t result = 0;
    auto lipp_it = lipp_.lower_bound(lower_key);
    while (lipp_it != lipp_.end() && lipp_it->comp.data.key <= upper_key) {
      result += lipp_it->comp.data.value;
      ++lipp_it;
    }

    auto buffer_it = active_buffer_.lower_bound(lower_key);
    while (buffer_it != active_buffer_.end() && buffer_it->key() <= upper_key) {
      result += buffer_it->value();
      ++buffer_it;
    }
    return result;
  }

  void Insert(const KeyValue<KeyType>& data, uint32_t thread_id) {
    (void) thread_id;
    record_insert();
    if (lookup_favored_mode_) {
      lipp_.insert(data.key, data.value);
      return;
    }
    track_active_min(data.key);
    active_buffer_.insert(data.key, data.value);
  }

  bool applicable(bool unique, bool range_query, bool insert, bool multithread,
                  const std::string& ops_filename) const {
    (void) range_query;
    (void) insert;
    (void) ops_filename;
    return unique && !multithread;
  }

  std::string name() const { return "HybridPGMLIPPSpecialized"; }

  std::vector<std::string> variants() const {
    return {"auto-switch-write-through-specialized", std::to_string(WindowSize),
            std::to_string(InsertHeavyPercent), SearchClass::name(),
            std::to_string(kDynamicPGMError)};
  }

  std::size_t size() const { return lipp_.index_size() + active_buffer_.size_in_bytes(); }

 private:
  using BufferSearch = SearchClass;
  using BufferPGM = PGMIndex<KeyType, BufferSearch, kDynamicPGMError, 16>;
  using BufferIndex = DynamicPGMIndex<KeyType, uint64_t, BufferSearch, BufferPGM>;

  void reset_state() {
    active_buffer_ = BufferIndex();
    has_active_min_ = false;
    window_ops_ = 0;
    window_inserts_ = 0;
    lookup_favored_mode_ = true;
  }

  void track_active_min(const KeyType& key) {
    if (!has_active_min_ || key < active_min_key_) {
      active_min_key_ = key;
      has_active_min_ = true;
    }
  }

  void flush_all_active_into_lipp() const {
    if (!has_active_min_) {
      return;
    }
    auto it = active_buffer_.lower_bound(active_min_key_);
    while (it != active_buffer_.end()) {
      lipp_.insert(it->key(), it->value());
      ++it;
    }
    active_buffer_ = BufferIndex();
    has_active_min_ = false;
  }

  void evaluate_window() const {
    if (window_ops_ < WindowSize) {
      return;
    }
    bool next_lookup_favored = window_inserts_ * 100 < window_ops_ * InsertHeavyPercent;
    if (next_lookup_favored && !lookup_favored_mode_) {
      flush_all_active_into_lipp();
    }
    lookup_favored_mode_ = next_lookup_favored;
    window_ops_ = 0;
    window_inserts_ = 0;
  }

  void record_lookup() const {
    ++window_ops_;
    evaluate_window();
  }

  void record_insert() const {
    ++window_ops_;
    ++window_inserts_;
    evaluate_window();
  }

  mutable LIPP<KeyType, uint64_t> lipp_;
  mutable BufferIndex active_buffer_;
  mutable std::size_t window_ops_ = 0;
  mutable std::size_t window_inserts_ = 0;
  mutable KeyType active_min_key_{};
  mutable bool has_active_min_ = false;
  mutable bool lookup_favored_mode_ = true;
};

template <class KeyType>
class HybridPGMLIPPWriteThroughSpecialized : public Base<KeyType> {
 public:
  HybridPGMLIPPWriteThroughSpecialized(const std::vector<int>& params) { (void) params; }

  uint64_t Build(const std::vector<KeyValue<KeyType>>& data, size_t num_threads) {
    (void) num_threads;
    std::vector<std::pair<KeyType, uint64_t>> loading_data;
    loading_data.reserve(data.size());
    for (const auto& item : data) {
      loading_data.emplace_back(item.key, item.value);
    }
    active_buffer_ = BufferIndex();
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
    active_buffer_.insert(data.key, data.value);
  }

  bool applicable(bool unique, bool range_query, bool insert, bool multithread,
                  const std::string& ops_filename) const {
    (void) range_query;
    (void) insert;
    (void) ops_filename;
    return unique && !multithread;
  }

  std::string name() const { return "HybridPGMLIPPSpecialized"; }

  std::vector<std::string> variants() const { return {"lookup-write-through-specialized"}; }

  std::size_t size() const { return lipp_.index_size() + active_buffer_.size_in_bytes(); }

 private:
  static constexpr std::size_t kDynamicPGMError = 128;
  using BufferSearch = BranchingBinarySearch<0>;
  using BufferPGM = PGMIndex<KeyType, BufferSearch, kDynamicPGMError, 16>;
  using BufferIndex = DynamicPGMIndex<KeyType, uint64_t, BufferSearch, BufferPGM>;

  mutable LIPP<KeyType, uint64_t> lipp_;
  mutable BufferIndex active_buffer_;
};

template <class KeyType, class SearchClass = BranchingBinarySearch<0>,
          std::size_t kDynamicPGMError = 128>
class HybridPGMLIPPWorkloadAwareSpecialized : public Base<KeyType> {
 public:
  HybridPGMLIPPWorkloadAwareSpecialized(const std::vector<int>& params) {
    if (!params.empty() && params[0] != 0) {
      mode_ = Mode::kInsertHeavySharded;
    }
    if (params.size() > 1 && params[1] > 0) {
      shard_bits_ = static_cast<std::size_t>(params[1]);
    }
  }

  uint64_t Build(const std::vector<KeyValue<KeyType>>& data, size_t num_threads) {
    if (!insert_heavy_mode()) {
      shards_.clear();
      return direct_lipp_.Build(data, num_threads);
    }

    (void) num_threads;
    std::vector<std::pair<KeyType, uint64_t>> loading_data;
    loading_data.reserve(data.size());
    for (const auto& item : data) {
      loading_data.emplace_back(item.key, item.value);
    }
    shards_.clear();
    shards_.resize(shard_count());
    return util::timing([&] { lipp_.bulk_load(loading_data.data(), loading_data.size()); });
  }

  size_t EqualityLookup(const KeyType& lookup_key, uint32_t thread_id) const {
    if (!insert_heavy_mode()) {
      return direct_lipp_.EqualityLookup(lookup_key, thread_id);
    }

    (void) thread_id;
    uint64_t value;
    if (lipp_.find(lookup_key, value)) {
      return value;
    }
    const auto& shard = shards_[bucket_index(lookup_key)];
    auto it = shard.find(lookup_key);
    return it != shard.end() ? it->value() : util::OVERFLOW;
  }

  uint64_t RangeQuery(const KeyType& lower_key, const KeyType& upper_key, uint32_t thread_id) const {
    if (!insert_heavy_mode()) {
      return direct_lipp_.RangeQuery(lower_key, upper_key, thread_id);
    }

    (void) thread_id;
    uint64_t result = 0;
    auto lipp_it = lipp_.lower_bound(lower_key);
    while (lipp_it != lipp_.end() && lipp_it->comp.data.key <= upper_key) {
      result += lipp_it->comp.data.value;
      ++lipp_it;
    }

    for (const auto& shard : shards_) {
      auto it = shard.lower_bound(lower_key);
      while (it != shard.end() && it->key() <= upper_key) {
        result += it->value();
        ++it;
      }
    }
    return result;
  }

  void Insert(const KeyValue<KeyType>& data, uint32_t thread_id) {
    if (!insert_heavy_mode()) {
      direct_lipp_.Insert(data, thread_id);
      return;
    }
    (void) thread_id;
    shards_[bucket_index(data.key)].insert(data.key, data.value);
  }

  bool applicable(bool unique, bool range_query, bool insert, bool multithread,
                  const std::string& ops_filename) const {
    (void) range_query;
    (void) insert;
    (void) ops_filename;
    return unique && !multithread;
  }

  std::string name() const { return "HybridPGMLIPPWorkloadAwareSpecialized"; }

  std::vector<std::string> variants() const {
    return {insert_heavy_mode() ? "workload-aware-sharded" : "workload-aware-direct-lipp",
            std::to_string(shard_bits_), SearchClass::name(),
            std::to_string(kDynamicPGMError)};
  }

  std::size_t size() const {
    if (!insert_heavy_mode()) {
      return direct_lipp_.size();
    }
    std::size_t total = lipp_.index_size();
    for (const auto& shard : shards_) {
      total += shard.size_in_bytes();
    }
    return total;
  }

 private:
  enum class Mode {
    kLookupHeavyDirect,
    kInsertHeavySharded,
  };

  using BufferSearch = SearchClass;
  using BufferPGM = PGMIndex<KeyType, BufferSearch, kDynamicPGMError, 16>;
  using BufferIndex = DynamicPGMIndex<KeyType, uint64_t, BufferSearch, BufferPGM>;

  bool insert_heavy_mode() const { return mode_ == Mode::kInsertHeavySharded; }

  std::size_t shard_count() const {
    return shard_bits_ == 0 ? 1 : (std::size_t{1} << shard_bits_);
  }

  std::size_t bucket_index(const KeyType& key) const {
    if (shard_bits_ == 0) {
      return 0;
    }
    if constexpr (std::is_integral<KeyType>::value) {
      uint64_t mixed = static_cast<uint64_t>(key) * 11400714819323198485ull;
      return static_cast<std::size_t>(mixed >> (64 - shard_bits_));
    }
    return std::hash<KeyType>{}(key) & (shard_count() - 1);
  }

  mutable LIPP<KeyType, uint64_t> lipp_;
  mutable HybridPGMLIPPDirectLippSpecialized<KeyType> direct_lipp_{std::vector<int>()};
  mutable std::vector<BufferIndex> shards_;
  Mode mode_ = Mode::kLookupHeavyDirect;
  std::size_t shard_bits_ = 0;
};

template <class KeyType, std::size_t ShadowShardBits>
class HybridPGMLIPPLookupSpecialized : public Base<KeyType> {
 public:
  HybridPGMLIPPLookupSpecialized(const std::vector<int>& params) { (void) params; }

  uint64_t Build(const std::vector<KeyValue<KeyType>>& data, size_t num_threads) {
    (void) num_threads;
    std::vector<std::pair<KeyType, uint64_t>> loading_data;
    loading_data.reserve(data.size());
    for (const auto& item : data) {
      loading_data.emplace_back(item.key, item.value);
    }
    active_buffer_ = BufferIndex();
    shadows_.clear();
    return util::timing([&] { lipp_.bulk_load(loading_data.data(), loading_data.size()); });
  }

  size_t EqualityLookup(const KeyType& lookup_key, uint32_t thread_id) const {
    (void) thread_id;
    uint64_t value;
    if (lipp_.find(lookup_key, value)) {
      return value;
    }
    return find_in_shadows(lookup_key, value) ? value : util::OVERFLOW;
  }

  uint64_t RangeQuery(const KeyType& lower_key, const KeyType& upper_key, uint32_t thread_id) const {
    (void) thread_id;
    uint64_t result = 0;
    auto lipp_it = lipp_.lower_bound(lower_key);
    while (lipp_it != lipp_.end() && lipp_it->comp.data.key <= upper_key) {
      result += lipp_it->comp.data.value;
      ++lipp_it;
    }

    auto buffer_it = active_buffer_.lower_bound(lower_key);
    while (buffer_it != active_buffer_.end() && buffer_it->key() <= upper_key) {
      result += buffer_it->value();
      ++buffer_it;
    }
    return result;
  }

  void Insert(const KeyValue<KeyType>& data, uint32_t thread_id) {
    (void) thread_id;
    active_buffer_.insert(data.key, data.value);
    insert_into_shadows(data.key, data.value);
  }

  bool applicable(bool unique, bool range_query, bool insert, bool multithread,
                  const std::string& ops_filename) const {
    (void) range_query;
    (void) insert;
    (void) ops_filename;
    return unique && !multithread;
  }

  std::string name() const { return "HybridPGMLIPPSpecialized"; }

  std::vector<std::string> variants() const {
    return {"lookup-no-flush-specialized", std::to_string(ShadowShardBits)};
  }

  std::size_t size() const {
    std::size_t total = lipp_.index_size() + active_buffer_.size_in_bytes();
    for (const auto& shard : shadows_) {
      if (shard) {
        total += shard->index_size();
      }
    }
    return total;
  }

 private:
  static constexpr std::size_t kDynamicPGMError = 128;
  using BufferSearch = BranchingBinarySearch<0>;
  using BufferPGM = PGMIndex<KeyType, BufferSearch, kDynamicPGMError, 16>;
  using BufferIndex = DynamicPGMIndex<KeyType, uint64_t, BufferSearch, BufferPGM>;
  using ShadowLIPP = LIPP<KeyType, uint64_t>;

  static constexpr std::size_t kShadowShardCount =
      ShadowShardBits == 0 ? 1 : (std::size_t{1} << ShadowShardBits);

  std::size_t shadow_index(const KeyType& key) const {
    if constexpr (ShadowShardBits == 0) {
      return 0;
    }
    return std::hash<KeyType>{}(key) & (kShadowShardCount - 1);
  }

  void ensure_shadow_container() {
    if (shadows_.empty()) {
      shadows_.resize(kShadowShardCount);
    }
  }

  void insert_into_shadows(const KeyType& key, uint64_t value) {
    ensure_shadow_container();
    auto idx = shadow_index(key);
    if (!shadows_[idx]) {
      shadows_[idx] = std::make_unique<ShadowLIPP>();
    }
    shadows_[idx]->insert(key, value);
  }

  bool find_in_shadows(const KeyType& key, uint64_t& value) const {
    if (shadows_.empty()) {
      return false;
    }
    auto idx = shadow_index(key);
    const auto& shard = shadows_[idx];
    return shard && shard->find(key, value);
  }

  mutable LIPP<KeyType, uint64_t> lipp_;
  mutable BufferIndex active_buffer_;
  std::vector<std::unique_ptr<ShadowLIPP>> shadows_;
};

template <class KeyType, std::size_t ShardBits,
          class SearchClass = BranchingBinarySearch<0>,
          std::size_t kDynamicPGMError = 128>
class HybridPGMLIPPShardedLookupSpecialized : public Base<KeyType> {
 public:
  HybridPGMLIPPShardedLookupSpecialized(const std::vector<int>& params) { (void) params; }

  uint64_t Build(const std::vector<KeyValue<KeyType>>& data, size_t num_threads) {
    (void) num_threads;
    std::vector<std::pair<KeyType, uint64_t>> loading_data;
    loading_data.reserve(data.size());
    for (const auto& item : data) {
      loading_data.emplace_back(item.key, item.value);
    }
    shards_.clear();
    shards_.resize(kShardCount);
    return util::timing([&] { lipp_.bulk_load(loading_data.data(), loading_data.size()); });
  }

  size_t EqualityLookup(const KeyType& lookup_key, uint32_t thread_id) const {
    (void) thread_id;
    uint64_t value;
    if (lipp_.find(lookup_key, value)) {
      return value;
    }
    const auto& shard = shards_[bucket_index(lookup_key)];
    auto it = shard.find(lookup_key);
    return it != shard.end() ? it->value() : util::OVERFLOW;
  }

  uint64_t RangeQuery(const KeyType& lower_key, const KeyType& upper_key, uint32_t thread_id) const {
    (void) thread_id;
    uint64_t result = 0;
    auto lipp_it = lipp_.lower_bound(lower_key);
    while (lipp_it != lipp_.end() && lipp_it->comp.data.key <= upper_key) {
      result += lipp_it->comp.data.value;
      ++lipp_it;
    }

    for (const auto& shard : shards_) {
      auto shard_it = shard.lower_bound(lower_key);
      while (shard_it != shard.end() && shard_it->key() <= upper_key) {
        result += shard_it->value();
        ++shard_it;
      }
    }
    return result;
  }

  void Insert(const KeyValue<KeyType>& data, uint32_t thread_id) {
    (void) thread_id;
    shards_[bucket_index(data.key)].insert(data.key, data.value);
  }

  bool applicable(bool unique, bool range_query, bool insert, bool multithread,
                  const std::string& ops_filename) const {
    (void) range_query;
    (void) insert;
    (void) ops_filename;
    return unique && !multithread;
  }

  std::string name() const { return "HybridPGMLIPPSpecialized"; }

  std::vector<std::string> variants() const {
    return {"lookup-no-flush-sharded-specialized", std::to_string(ShardBits),
            SearchClass::name(), std::to_string(kDynamicPGMError)};
  }

  std::size_t size() const {
    std::size_t total = lipp_.index_size();
    for (const auto& shard : shards_) {
      total += shard.size_in_bytes();
    }
    return total;
  }

 private:
  using BufferSearch = SearchClass;
  using BufferPGM = PGMIndex<KeyType, BufferSearch, kDynamicPGMError, 16>;
  using BufferIndex = DynamicPGMIndex<KeyType, uint64_t, BufferSearch, BufferPGM>;

  static constexpr std::size_t kShardCount = std::size_t{1} << ShardBits;

  std::size_t bucket_index(const KeyType& key) const {
    if constexpr (std::is_integral<KeyType>::value) {
      uint64_t mixed = static_cast<uint64_t>(key) * 11400714819323198485ull;
      return static_cast<std::size_t>(mixed >> (64 - ShardBits));
    } else {
      return std::hash<KeyType>{}(key) & (kShardCount - 1);
    }
  }

  mutable LIPP<KeyType, uint64_t> lipp_;
  mutable std::vector<BufferIndex> shards_;
};

template <class KeyType, std::size_t EpochCount, std::size_t EpochCapacity,
          class SearchClass = BranchingBinarySearch<0>,
          std::size_t kDynamicPGMError = 128>
class HybridPGMLIPPEpochRingSpecialized : public Base<KeyType> {
 public:
  HybridPGMLIPPEpochRingSpecialized(const std::vector<int>& params) { (void) params; }

  uint64_t Build(const std::vector<KeyValue<KeyType>>& data, size_t num_threads) {
    (void) num_threads;
    std::vector<std::pair<KeyType, uint64_t>> loading_data;
    loading_data.reserve(data.size());
    for (const auto& item : data) {
      loading_data.emplace_back(item.key, item.value);
    }
    buffers_.clear();
    buffers_.resize(EpochCount);
    counts_.assign(EpochCount, 0);
    min_keys_.assign(EpochCount, KeyType{});
    has_min_.assign(EpochCount, false);
    active_epoch_ = 0;
    return util::timing([&] { lipp_.bulk_load(loading_data.data(), loading_data.size()); });
  }

  size_t EqualityLookup(const KeyType& lookup_key, uint32_t thread_id) const {
    (void) thread_id;
    uint64_t value;
    if (lipp_.find(lookup_key, value)) {
      return value;
    }
    for (std::size_t offset = 0; offset < EpochCount; ++offset) {
      auto idx = epoch_index(offset);
      if (counts_[idx] == 0) {
        continue;
      }
      const auto& buffer = buffers_[idx];
      auto it = buffer.find(lookup_key);
      if (it != buffer.end()) {
        return it->value();
      }
    }
    return util::OVERFLOW;
  }

  uint64_t RangeQuery(const KeyType& lower_key, const KeyType& upper_key,
                      uint32_t thread_id) const {
    (void) thread_id;
    uint64_t result = 0;
    auto lipp_it = lipp_.lower_bound(lower_key);
    while (lipp_it != lipp_.end() && lipp_it->comp.data.key <= upper_key) {
      result += lipp_it->comp.data.value;
      ++lipp_it;
    }

    for (std::size_t idx = 0; idx < EpochCount; ++idx) {
      if (counts_[idx] == 0) {
        continue;
      }
      auto it = buffers_[idx].lower_bound(lower_key);
      while (it != buffers_[idx].end() && it->key() <= upper_key) {
        result += it->value();
        ++it;
      }
    }
    return result;
  }

  void Insert(const KeyValue<KeyType>& data, uint32_t thread_id) {
    (void) thread_id;
    if (!has_min_[active_epoch_] || data.key < min_keys_[active_epoch_]) {
      min_keys_[active_epoch_] = data.key;
      has_min_[active_epoch_] = true;
    }
    buffers_[active_epoch_].insert(data.key, data.value);
    ++counts_[active_epoch_];
    if (counts_[active_epoch_] >= EpochCapacity) {
      rotate_epoch();
    }
  }

  bool applicable(bool unique, bool range_query, bool insert, bool multithread,
                  const std::string& ops_filename) const {
    (void) range_query;
    (void) insert;
    (void) ops_filename;
    return unique && !multithread;
  }

  std::string name() const { return "HybridPGMLIPPSpecialized"; }

  std::vector<std::string> variants() const {
    return {"epoch-ring-specialized", std::to_string(EpochCount),
            std::to_string(EpochCapacity), SearchClass::name(),
            std::to_string(kDynamicPGMError)};
  }

  std::size_t size() const {
    std::size_t total = lipp_.index_size();
    for (const auto& buffer : buffers_) {
      total += buffer.size_in_bytes();
    }
    return total;
  }

 private:
  using BufferSearch = SearchClass;
  using BufferPGM = PGMIndex<KeyType, BufferSearch, kDynamicPGMError, 16>;
  using BufferIndex = DynamicPGMIndex<KeyType, uint64_t, BufferSearch, BufferPGM>;

  std::size_t epoch_index(std::size_t offset) const {
    return (active_epoch_ + EpochCount - offset) % EpochCount;
  }

  void flush_epoch_into_lipp(std::size_t idx) {
    if (counts_[idx] == 0 || !has_min_[idx]) {
      return;
    }
    auto it = buffers_[idx].lower_bound(min_keys_[idx]);
    while (it != buffers_[idx].end()) {
      lipp_.insert(it->key(), it->value());
      ++it;
    }
    buffers_[idx] = BufferIndex();
    counts_[idx] = 0;
    has_min_[idx] = false;
  }

  void rotate_epoch() {
    auto next_epoch = (active_epoch_ + 1) % EpochCount;
    flush_epoch_into_lipp(next_epoch);
    active_epoch_ = next_epoch;
  }

  mutable LIPP<KeyType, uint64_t> lipp_;
  mutable std::vector<BufferIndex> buffers_;
  mutable std::vector<std::size_t> counts_;
  mutable std::vector<KeyType> min_keys_;
  mutable std::vector<bool> has_min_;
  mutable std::size_t active_epoch_ = 0;
};

template <class KeyType, std::size_t RebuildThreshold>
class HybridPGMLIPPBatchDeltaLippSpecialized : public Base<KeyType> {
 public:
  HybridPGMLIPPBatchDeltaLippSpecialized(const std::vector<int>& params) { (void) params; }

  uint64_t Build(const std::vector<KeyValue<KeyType>>& data, size_t num_threads) {
    (void) num_threads;
    std::vector<std::pair<KeyType, uint64_t>> loading_data;
    loading_data.reserve(data.size());
    for (const auto& item : data) {
      loading_data.emplace_back(item.key, item.value);
    }
    active_buffer_ = BufferIndex();
    delta_lipp_.reset();
    active_count_ = 0;
    delta_count_ = 0;
    has_active_min_ = false;
    has_delta_min_ = false;
    return util::timing([&] { lipp_.bulk_load(loading_data.data(), loading_data.size()); });
  }

  size_t EqualityLookup(const KeyType& lookup_key, uint32_t thread_id) const {
    (void) thread_id;
    uint64_t value;
    if (lipp_.find(lookup_key, value)) {
      return value;
    }
    if (delta_lipp_ && delta_lipp_->find(lookup_key, value)) {
      return value;
    }
    auto it = active_buffer_.find(lookup_key);
    return it != active_buffer_.end() ? it->value() : util::OVERFLOW;
  }

  uint64_t RangeQuery(const KeyType& lower_key, const KeyType& upper_key, uint32_t thread_id) const {
    (void) thread_id;
    uint64_t result = 0;

    auto lipp_it = lipp_.lower_bound(lower_key);
    while (lipp_it != lipp_.end() && lipp_it->comp.data.key <= upper_key) {
      result += lipp_it->comp.data.value;
      ++lipp_it;
    }

    if (delta_lipp_) {
      auto delta_it = delta_lipp_->lower_bound(lower_key);
      while (delta_it != delta_lipp_->end() && delta_it->comp.data.key <= upper_key) {
        result += delta_it->comp.data.value;
        ++delta_it;
      }
    }

    auto active_it = active_buffer_.lower_bound(lower_key);
    while (active_it != active_buffer_.end() && active_it->key() <= upper_key) {
      result += active_it->value();
      ++active_it;
    }
    return result;
  }

  void Insert(const KeyValue<KeyType>& data, uint32_t thread_id) {
    (void) thread_id;
    if (!has_active_min_ || data.key < active_min_key_) {
      active_min_key_ = data.key;
      has_active_min_ = true;
    }
    active_buffer_.insert(data.key, data.value);
    ++active_count_;
    if (active_count_ >= RebuildThreshold) {
      rebuild_delta();
    }
  }

  bool applicable(bool unique, bool range_query, bool insert, bool multithread,
                  const std::string& ops_filename) const {
    (void) range_query;
    (void) insert;
    (void) ops_filename;
    return unique && !multithread;
  }

  std::string name() const { return "HybridPGMLIPPSpecialized"; }

  std::vector<std::string> variants() const {
    return {"lookup-batch-delta-lipp-specialized", std::to_string(RebuildThreshold)};
  }

  std::size_t size() const {
    return lipp_.index_size() + (delta_lipp_ ? delta_lipp_->index_size() : 0) +
           active_buffer_.size_in_bytes();
  }

 private:
  static constexpr std::size_t kDynamicPGMError = 128;
  using BufferSearch = BranchingBinarySearch<0>;
  using BufferPGM = PGMIndex<KeyType, BufferSearch, kDynamicPGMError, 16>;
  using BufferIndex = DynamicPGMIndex<KeyType, uint64_t, BufferSearch, BufferPGM>;
  using DeltaLIPP = LIPP<KeyType, uint64_t>;

  void rebuild_delta() {
    std::vector<std::pair<KeyType, uint64_t>> delta_entries;
    std::vector<std::pair<KeyType, uint64_t>> active_entries;
    if (delta_lipp_ && delta_count_ > 0) {
      delta_entries.reserve(delta_count_);
      auto it = delta_lipp_->lower_bound(delta_min_key_);
      while (it != delta_lipp_->end()) {
        delta_entries.emplace_back(it->comp.data.key, it->comp.data.value);
        ++it;
      }
    }
    if (active_count_ > 0) {
      active_entries.reserve(active_count_);
      auto it = active_buffer_.lower_bound(active_min_key_);
      while (it != active_buffer_.end()) {
        active_entries.emplace_back(it->key(), it->value());
        ++it;
      }
    }

    std::vector<std::pair<KeyType, uint64_t>> merged;
    merged.reserve(delta_entries.size() + active_entries.size());
    std::merge(delta_entries.begin(), delta_entries.end(), active_entries.begin(), active_entries.end(),
               std::back_inserter(merged),
               [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

    if (merged.empty()) {
      delta_lipp_.reset();
      delta_count_ = 0;
      has_delta_min_ = false;
    } else {
      auto next = std::make_unique<DeltaLIPP>();
      next->bulk_load(merged.data(), merged.size());
      delta_lipp_ = std::move(next);
      delta_count_ = merged.size();
      delta_min_key_ = merged.front().first;
      has_delta_min_ = true;
    }

    active_buffer_ = BufferIndex();
    active_count_ = 0;
    has_active_min_ = false;
  }

  mutable LIPP<KeyType, uint64_t> lipp_;
  mutable BufferIndex active_buffer_;
  std::unique_ptr<DeltaLIPP> delta_lipp_;
  std::size_t active_count_ = 0;
  std::size_t delta_count_ = 0;
  KeyType active_min_key_{};
  KeyType delta_min_key_{};
  bool has_active_min_ = false;
  bool has_delta_min_ = false;
};

template <class KeyType, std::size_t FreezeThreshold, class SearchClass = BranchingBinarySearch<0>,
          std::size_t kDynamicPGMError = 128>
class HybridPGMLIPPOneShotDeltaLippSpecialized : public Base<KeyType> {
 public:
  HybridPGMLIPPOneShotDeltaLippSpecialized(const std::vector<int>& params) { (void) params; }

  uint64_t Build(const std::vector<KeyValue<KeyType>>& data, size_t num_threads) {
    (void) num_threads;
    std::vector<std::pair<KeyType, uint64_t>> loading_data;
    loading_data.reserve(data.size());
    for (const auto& item : data) {
      loading_data.emplace_back(item.key, item.value);
    }
    active_buffer_ = BufferIndex();
    delta_lipp_.reset();
    active_count_ = 0;
    has_active_min_ = false;
    delta_frozen_ = false;
    return util::timing([&] { lipp_.bulk_load(loading_data.data(), loading_data.size()); });
  }

  size_t EqualityLookup(const KeyType& lookup_key, uint32_t thread_id) const {
    (void) thread_id;
    uint64_t value;
    if (lipp_.find(lookup_key, value)) {
      return value;
    }
    if (delta_lipp_ && delta_lipp_->find(lookup_key, value)) {
      return value;
    }
    auto it = active_buffer_.find(lookup_key);
    return it != active_buffer_.end() ? it->value() : util::OVERFLOW;
  }

  uint64_t RangeQuery(const KeyType& lower_key, const KeyType& upper_key, uint32_t thread_id) const {
    (void) thread_id;
    uint64_t result = 0;
    auto lipp_it = lipp_.lower_bound(lower_key);
    while (lipp_it != lipp_.end() && lipp_it->comp.data.key <= upper_key) {
      result += lipp_it->comp.data.value;
      ++lipp_it;
    }

    if (delta_lipp_) {
      auto delta_it = delta_lipp_->lower_bound(lower_key);
      while (delta_it != delta_lipp_->end() && delta_it->comp.data.key <= upper_key) {
        result += delta_it->comp.data.value;
        ++delta_it;
      }
    }

    auto active_it = active_buffer_.lower_bound(lower_key);
    while (active_it != active_buffer_.end() && active_it->key() <= upper_key) {
      result += active_it->value();
      ++active_it;
    }
    return result;
  }

  void Insert(const KeyValue<KeyType>& data, uint32_t thread_id) {
    (void) thread_id;
    if (!has_active_min_ || data.key < active_min_key_) {
      active_min_key_ = data.key;
      has_active_min_ = true;
    }
    active_buffer_.insert(data.key, data.value);
    ++active_count_;
    if (!delta_frozen_ && active_count_ >= FreezeThreshold) {
      freeze_active_into_delta();
    }
  }

  bool applicable(bool unique, bool range_query, bool insert, bool multithread,
                  const std::string& ops_filename) const {
    (void) range_query;
    (void) insert;
    (void) ops_filename;
    return unique && !multithread;
  }

  std::string name() const { return "HybridPGMLIPPSpecialized"; }

  std::vector<std::string> variants() const {
    return {"lookup-oneshot-delta-lipp-specialized", std::to_string(FreezeThreshold),
            SearchClass::name(), std::to_string(kDynamicPGMError)};
  }

  std::size_t size() const {
    return lipp_.index_size() + (delta_lipp_ ? delta_lipp_->index_size() : 0) +
           active_buffer_.size_in_bytes();
  }

 private:
  using BufferSearch = SearchClass;
  using BufferPGM = PGMIndex<KeyType, BufferSearch, kDynamicPGMError, 16>;
  using BufferIndex = DynamicPGMIndex<KeyType, uint64_t, BufferSearch, BufferPGM>;
  using DeltaLIPP = LIPP<KeyType, uint64_t>;

  void freeze_active_into_delta() {
    if (!has_active_min_ || active_count_ == 0) {
      delta_frozen_ = true;
      return;
    }
    std::vector<std::pair<KeyType, uint64_t>> entries;
    entries.reserve(active_count_);
    auto it = active_buffer_.lower_bound(active_min_key_);
    while (it != active_buffer_.end()) {
      entries.emplace_back(it->key(), it->value());
      ++it;
    }
    if (!entries.empty()) {
      auto next = std::make_unique<DeltaLIPP>();
      next->bulk_load(entries.data(), entries.size());
      delta_lipp_ = std::move(next);
    }
    active_buffer_ = BufferIndex();
    active_count_ = 0;
    has_active_min_ = false;
    delta_frozen_ = true;
  }

  mutable LIPP<KeyType, uint64_t> lipp_;
  mutable BufferIndex active_buffer_;
  std::unique_ptr<DeltaLIPP> delta_lipp_;
  std::size_t active_count_ = 0;
  KeyType active_min_key_{};
  bool has_active_min_ = false;
  bool delta_frozen_ = false;
};

template <class KeyType, std::size_t ActiveThreshold, std::size_t DrainBatch,
          class SearchClass = BranchingBinarySearch<0>, std::size_t kDynamicPGMError = 128>
class HybridPGMLIPPSortedDrainSpecialized : public Base<KeyType> {
 public:
  HybridPGMLIPPSortedDrainSpecialized(const std::vector<int>& params) { (void) params; }

  uint64_t Build(const std::vector<KeyValue<KeyType>>& data, size_t num_threads) {
    (void) num_threads;
    std::vector<std::pair<KeyType, uint64_t>> loading_data;
    loading_data.reserve(data.size());
    for (const auto& item : data) {
      loading_data.emplace_back(item.key, item.value);
    }
    reset_buffers();
    return util::timing([&] { lipp_.bulk_load(loading_data.data(), loading_data.size()); });
  }

  size_t EqualityLookup(const KeyType& lookup_key, uint32_t thread_id) const {
    (void) thread_id;
    drain_step();
    uint64_t value;
    if (lipp_.find(lookup_key, value)) {
      return value;
    }
    auto it = active_buffer_.find(lookup_key);
    if (it != active_buffer_.end()) {
      return it->value();
    }
    if (!flush_in_progress_) {
      return util::OVERFLOW;
    }
    auto flushing_it = flushing_buffer_.find(lookup_key);
    return flushing_it != flushing_buffer_.end() ? flushing_it->value() : util::OVERFLOW;
  }

  uint64_t RangeQuery(const KeyType& lower_key, const KeyType& upper_key, uint32_t thread_id) const {
    (void) thread_id;
    drain_step();
    uint64_t result = 0;

    auto lipp_it = lipp_.lower_bound(lower_key);
    while (lipp_it != lipp_.end() && lipp_it->comp.data.key <= upper_key) {
      result += lipp_it->comp.data.value;
      ++lipp_it;
    }

    auto active_it = active_buffer_.lower_bound(lower_key);
    while (active_it != active_buffer_.end() && active_it->key() <= upper_key) {
      result += active_it->value();
      ++active_it;
    }

    if (flush_in_progress_) {
      auto begin_key =
          flush_resume_valid_ ? std::max(lower_key, flush_resume_key_) : lower_key;
      auto flushing_it = flushing_buffer_.lower_bound(begin_key);
      if (flush_resume_valid_ && flush_skip_equal_ && flushing_it != flushing_buffer_.end() &&
          flushing_it->key() == flush_resume_key_) {
        ++flushing_it;
      }
      while (flushing_it != flushing_buffer_.end() && flushing_it->key() <= upper_key) {
        result += flushing_it->value();
        ++flushing_it;
      }
    }

    return result;
  }

  void Insert(const KeyValue<KeyType>& data, uint32_t thread_id) {
    (void) thread_id;
    drain_step();
    track_active_min(data.key);
    active_buffer_.insert(data.key, data.value);
    ++active_count_;
    maybe_start_flush();
  }

  bool applicable(bool unique, bool range_query, bool insert, bool multithread,
                  const std::string& ops_filename) const {
    (void) range_query;
    (void) insert;
    (void) ops_filename;
    return unique && !multithread;
  }

  std::string name() const { return "HybridPGMLIPPSpecialized"; }

  std::vector<std::string> variants() const {
    return {"lookup-sorted-drain-specialized", std::to_string(ActiveThreshold),
            std::to_string(DrainBatch), SearchClass::name(),
            std::to_string(kDynamicPGMError)};
  }

  std::size_t size() const {
    return lipp_.index_size() + active_buffer_.size_in_bytes() + flushing_buffer_.size_in_bytes();
  }

 private:
  using BufferSearch = SearchClass;
  using BufferPGM = PGMIndex<KeyType, BufferSearch, kDynamicPGMError, 16>;
  using BufferIndex = DynamicPGMIndex<KeyType, uint64_t, BufferSearch, BufferPGM>;
  void reset_buffers() {
    active_buffer_ = BufferIndex();
    flushing_buffer_ = BufferIndex();
    active_count_ = 0;
    flushing_count_ = 0;
    drained_count_ = 0;
    has_active_min_ = false;
    has_flushing_min_ = false;
    flush_in_progress_ = false;
    flush_resume_valid_ = false;
    flush_skip_equal_ = false;
  }

  void track_active_min(const KeyType& key) {
    if (!has_active_min_ || key < active_min_key_) {
      active_min_key_ = key;
      has_active_min_ = true;
    }
  }

  void maybe_start_flush() {
    if (flush_in_progress_ || active_count_ < ActiveThreshold) {
      return;
    }
    flushing_buffer_ = std::move(active_buffer_);
    flushing_count_ = active_count_;
    drained_count_ = 0;
    flushing_min_key_ = active_min_key_;
    has_flushing_min_ = has_active_min_;
    flush_in_progress_ = flushing_count_ > 0;
    flush_resume_valid_ = false;
    flush_skip_equal_ = false;
    active_buffer_ = BufferIndex();
    active_count_ = 0;
    has_active_min_ = false;
  }

  void drain_step() const {
    if (!flush_in_progress_) {
      return;
    }
    auto it = flush_resume_valid_ ? flushing_buffer_.lower_bound(flush_resume_key_)
                                  : flushing_buffer_.lower_bound(flushing_min_key_);
    if (flush_resume_valid_ && flush_skip_equal_ && it != flushing_buffer_.end() &&
        it->key() == flush_resume_key_) {
      ++it;
    }
    if (it == flushing_buffer_.end()) {
      flushing_buffer_ = BufferIndex();
      flushing_count_ = 0;
      drained_count_ = 0;
      has_flushing_min_ = false;
      flush_in_progress_ = false;
      flush_resume_valid_ = false;
      flush_skip_equal_ = false;
      return;
    }
    std::size_t drained_now = 0;
    while (drained_now < DrainBatch && it != flushing_buffer_.end()) {
      lipp_.insert(it->key(), it->value());
      flush_resume_key_ = it->key();
      flush_resume_valid_ = true;
      flush_skip_equal_ = true;
      ++it;
      ++drained_now;
      ++drained_count_;
    }
    if (it == flushing_buffer_.end() || drained_count_ >= flushing_count_) {
      flushing_buffer_ = BufferIndex();
      flushing_count_ = 0;
      drained_count_ = 0;
      has_flushing_min_ = false;
      flush_in_progress_ = false;
      flush_resume_valid_ = false;
      flush_skip_equal_ = false;
    }
  }

  mutable LIPP<KeyType, uint64_t> lipp_;
  mutable BufferIndex active_buffer_;
  mutable BufferIndex flushing_buffer_;
  mutable std::size_t active_count_ = 0;
  mutable std::size_t flushing_count_ = 0;
  mutable std::size_t drained_count_ = 0;
  mutable KeyType active_min_key_{};
  mutable KeyType flushing_min_key_{};
  mutable KeyType flush_resume_key_{};
  mutable bool has_active_min_ = false;
  mutable bool has_flushing_min_ = false;
  mutable bool flush_in_progress_ = false;
  mutable bool flush_resume_valid_ = false;
  mutable bool flush_skip_equal_ = false;
};
