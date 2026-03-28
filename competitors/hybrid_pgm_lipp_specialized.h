#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "./lipp/src/core/lipp.h"
#include "PGM-index/include/pgm_index_dynamic.hpp"
#include "base.h"
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

template <class KeyType, std::size_t ShardBits>
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
    return {"lookup-no-flush-sharded-specialized", std::to_string(ShardBits)};
  }

  std::size_t size() const {
    std::size_t total = lipp_.index_size();
    for (const auto& shard : shards_) {
      total += shard.size_in_bytes();
    }
    return total;
  }

 private:
  static constexpr std::size_t kDynamicPGMError = 128;
  using BufferSearch = BranchingBinarySearch<0>;
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
