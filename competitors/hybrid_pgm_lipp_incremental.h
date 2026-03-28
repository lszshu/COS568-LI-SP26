#pragma once

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "./lipp/src/core/lipp.h"
#include "PGM-index/include/pgm_index_dynamic.hpp"
#include "base.h"
#include "searches/branching_binary_search.h"

template <class KeyType>
class HybridPGMLIPPIncremental : public Base<KeyType> {
 public:
  HybridPGMLIPPIncremental(const std::vector<int>& params) {
    if (!params.empty() && params[0] > 0) {
      flush_threshold_ = static_cast<std::size_t>(params[0]);
    }
    if (params.size() > 1 && params[1] > 0) {
      insert_flush_batch_size_ = static_cast<std::size_t>(params[1]);
    }
    if (params.size() > 2 && params[2] >= 0) {
      lookup_flush_batch_size_ = static_cast<std::size_t>(params[2]);
    }
    if (params.size() > 3) {
      prefer_lipp_first_ = params[3] != 0;
    }
    if (params.size() > 4) {
      use_lipp_shadows_ = params[4] != 0;
    }
    if (params.size() > 5 && params[5] >= 0) {
      shadow_shard_bits_ = static_cast<std::size_t>(params[5]);
    }
    if (params.size() > 6) {
      async_flush_ = params[6] != 0;
    }
    if (params.size() > 7) {
      direct_main_lipp_ = params[7] != 0;
    }
    if (async_flush_) {
      start_async_worker();
    }
  }

  ~HybridPGMLIPPIncremental() { stop_async_worker(); }

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
    finalize_async_flush_if_ready();

    if (direct_main_lipp_) {
      uint64_t value = util::NOT_FOUND;
      return find_in_main_lipp(lookup_key, value) ? value : util::OVERFLOW;
    }

    if (prefer_lipp_first_) {
      uint64_t value = util::NOT_FOUND;
      if (find_in_main_lipp(lookup_key, value)) {
        drain_flush_budget(lookup_flush_batch_size_);
        return value;
      }

      if (find_in_shadows(active_lipp_shards_, lookup_key, value)) {
        drain_flush_budget(lookup_flush_batch_size_);
        return value;
      }

      if (flush_in_progress_ && flushing_buffer_item_count_ > 0 &&
          find_in_shadows(flushing_lipp_shards_, lookup_key, value)) {
        drain_flush_budget(lookup_flush_batch_size_);
        return value;
      }

      if (use_lipp_shadows_) {
        drain_flush_budget(lookup_flush_batch_size_);
        return util::OVERFLOW;
      }

      if (active_buffer_item_count_ > 0) {
        auto active_it = active_buffer_.find(lookup_key);
        if (active_it != active_buffer_.end()) {
          drain_flush_budget(lookup_flush_batch_size_);
          return active_it->value();
        }
      }

      if (flush_in_progress_ && flushing_buffer_item_count_ > 0 &&
          flushing_lipp_shards_.empty()) {
        auto flushing_it = flushing_buffer_.find(lookup_key);
        if (flushing_it != flushing_buffer_.end()) {
          drain_flush_budget(lookup_flush_batch_size_);
          return flushing_it->value();
        }
      }

      drain_flush_budget(lookup_flush_batch_size_);
      return util::OVERFLOW;
    }

    if (active_buffer_item_count_ > 0) {
      auto active_it = active_buffer_.find(lookup_key);
      if (active_it != active_buffer_.end()) {
        drain_flush_budget(lookup_flush_batch_size_);
        return active_it->value();
      }
    }

    if (flush_in_progress_ && flushing_buffer_item_count_ > 0) {
      auto flushing_it = flushing_buffer_.find(lookup_key);
      if (flushing_it != flushing_buffer_.end()) {
        drain_flush_budget(lookup_flush_batch_size_);
        return flushing_it->value();
      }
    }

    uint64_t value = util::NOT_FOUND;
    if (!find_in_main_lipp(lookup_key, value)) {
      drain_flush_budget(lookup_flush_batch_size_);
      return util::OVERFLOW;
    }

    drain_flush_budget(lookup_flush_batch_size_);
    return value;
  }

  uint64_t RangeQuery(const KeyType& lower_key, const KeyType& upper_key, uint32_t thread_id) const {
    (void) thread_id;
    finalize_async_flush_if_ready();

    uint64_t result = 0;

    if (direct_main_lipp_) {
      auto lipp_it = lipp_.lower_bound(lower_key);
      while (lipp_it != lipp_.end() && lipp_it->comp.data.key <= upper_key) {
        result += lipp_it->comp.data.value;
        ++lipp_it;
      }
      return result;
    }

    if (async_flush_) {
      std::shared_lock<std::shared_mutex> lipp_lock(lipp_mutex_);
      auto lipp_it = lipp_.lower_bound(lower_key);
      while (lipp_it != lipp_.end() && lipp_it->comp.data.key <= upper_key) {
        result += lipp_it->comp.data.value;
        ++lipp_it;
      }
    } else {
      auto lipp_it = lipp_.lower_bound(lower_key);
      while (lipp_it != lipp_.end() && lipp_it->comp.data.key <= upper_key) {
        result += lipp_it->comp.data.value;
        ++lipp_it;
      }
    }

    auto active_it = active_buffer_.lower_bound(lower_key);
    while (active_it != active_buffer_.end() && active_it->key() <= upper_key) {
      result += active_it->value();
      ++active_it;
    }

    if (flush_in_progress_) {
      auto flushing_it = flushing_buffer_.lower_bound(lower_key);
      if (flush_resume_valid_ && lower_key <= flush_resume_key_) {
        flushing_it = flush_iterator();
      }
      while (flushing_it != flushing_buffer_.end() && flushing_it->key() <= upper_key) {
        result += flushing_it->value();
        ++flushing_it;
      }
    }

    drain_flush_budget(lookup_flush_batch_size_);
    return result;
  }

  void Insert(const KeyValue<KeyType>& data, uint32_t thread_id) {
    (void) thread_id;
    finalize_async_flush_if_ready();

    if (direct_main_lipp_) {
      insert_into_main_lipp(data.key, data.value);
      return;
    }

    active_buffer_.insert(data.key, data.value);
    if (use_lipp_shadows_) {
      insert_into_shadows(active_lipp_shards_, data.key, data.value);
    }
    ++active_buffer_item_count_;

    if (!flush_in_progress_ && active_buffer_item_count_ >= flush_threshold_) {
      start_flush();
    }

    drain_flush_budget(compute_insert_drain_budget());
  }

  bool applicable(bool unique, bool range_query, bool insert, bool multithread,
                  const std::string& ops_filename) const {
    (void) range_query;
    (void) insert;
    (void) ops_filename;
    return unique && !multithread;
  }

  std::string name() const { return "HybridPGMLIPPIncremental"; }

  std::vector<std::string> variants() const {
    return {
        direct_main_lipp_ ? "direct-main-lipp"
                          : (prefer_lipp_first_ ? "double-buffered-compliant-lipp-first"
                                                : "double-buffered-compliant-buffer-first"),
        std::to_string(flush_threshold_) + ":" +
            std::to_string(insert_flush_batch_size_) + ":" +
            std::to_string(lookup_flush_batch_size_) + ":" +
            std::to_string(prefer_lipp_first_ ? 1 : 0) + ":" +
            std::to_string(use_lipp_shadows_ ? 1 : 0) + ":" +
            std::to_string(shadow_shard_bits_) + ":" +
            std::to_string(async_flush_ ? 1 : 0) + ":" +
            std::to_string(direct_main_lipp_ ? 1 : 0),
    };
  }

  std::size_t size() const {
    if (direct_main_lipp_) {
      return lipp_.index_size();
    }
    return lipp_.index_size() + active_buffer_.size_in_bytes() + flushing_buffer_.size_in_bytes() +
           shadow_bytes(active_lipp_shards_) + shadow_bytes(flushing_lipp_shards_);
  }

 private:
  static constexpr std::size_t kDefaultFlushThreshold = 1u << 17;
  static constexpr std::size_t kDefaultInsertFlushBatchSize = 256;
  static constexpr std::size_t kDefaultLookupFlushBatchSize = 0;
  static constexpr std::size_t kBacklogDrainMultiplier = 4;
  static constexpr std::size_t kAdaptivePressureDivisor = 4;
  static constexpr std::size_t kMaxAdaptivePressureSteps = 4;
  static constexpr std::size_t kDynamicPGMError = 128;

  using BufferSearch = BranchingBinarySearch<0>;
  using BufferPGM = PGMIndex<KeyType, BufferSearch, kDynamicPGMError, 16>;
  using BufferIndex = DynamicPGMIndex<KeyType, uint64_t, BufferSearch, BufferPGM>;
  using ShadowLIPP = LIPP<KeyType, uint64_t>;
  using ShadowContainer = std::vector<std::unique_ptr<ShadowLIPP>>;

  bool find_in_main_lipp(const KeyType& key, uint64_t& value) const {
    if (async_flush_) {
      std::shared_lock<std::shared_mutex> lipp_lock(lipp_mutex_);
      return lipp_.find(key, value);
    }
    return lipp_.find(key, value);
  }

  void insert_into_main_lipp(const KeyType& key, uint64_t value) const {
    if (async_flush_) {
      std::unique_lock<std::shared_mutex> lipp_lock(lipp_mutex_);
      lipp_.insert(key, value);
      return;
    }
    lipp_.insert(key, value);
  }

  void reset_buffers() {
    active_buffer_ = BufferIndex();
    flushing_buffer_ = BufferIndex();
    active_lipp_shards_.clear();
    flushing_lipp_shards_.clear();
    active_buffer_item_count_ = 0;
    flushing_buffer_item_count_ = 0;
    flush_in_progress_ = false;
    flush_resume_valid_ = false;
    flush_skip_equal_ = false;
    if (async_flush_) {
      std::lock_guard<std::mutex> guard(async_mutex_);
      async_flush_requested_ = false;
      async_flush_done_ = false;
    }
  }

  void start_flush() {
    flushing_buffer_ = std::move(active_buffer_);
    flushing_buffer_item_count_ = active_buffer_item_count_;
    flushing_lipp_shards_ = use_lipp_shadows_ ? std::move(active_lipp_shards_) : ShadowContainer();
    active_buffer_ = BufferIndex();
    active_lipp_shards_.clear();
    active_buffer_item_count_ = 0;
    flush_in_progress_ = flushing_buffer_item_count_ > 0;
    flush_resume_valid_ = false;
    flush_skip_equal_ = false;
    if (async_flush_ && flush_in_progress_) {
      {
        std::lock_guard<std::mutex> guard(async_mutex_);
        async_flush_requested_ = true;
        async_flush_done_ = false;
      }
      async_cv_.notify_one();
    }
  }

  std::size_t shadow_shard_count() const {
    if (!use_lipp_shadows_) {
      return 0;
    }
    return shadow_shard_bits_ == 0 ? 1 : (std::size_t{1} << shadow_shard_bits_);
  }

  std::size_t shadow_shard_index(const KeyType& key) const {
    if (shadow_shard_bits_ == 0) {
      return 0;
    }
    return std::hash<KeyType>{}(key) & ((std::size_t{1} << shadow_shard_bits_) - 1);
  }

  void ensure_shadow_container(ShadowContainer& shadows) const {
    if (shadows.empty()) {
      shadows.resize(shadow_shard_count());
    }
  }

  void insert_into_shadows(ShadowContainer& shadows, const KeyType& key, uint64_t value) const {
    ensure_shadow_container(shadows);
    auto shard_idx = shadow_shard_index(key);
    if (!shadows[shard_idx]) {
      shadows[shard_idx] = std::make_unique<ShadowLIPP>();
    }
    shadows[shard_idx]->insert(key, value);
  }

  bool find_in_shadows(const ShadowContainer& shadows, const KeyType& key, uint64_t& value) const {
    if (shadows.empty()) {
      return false;
    }
    auto shard_idx = shadow_shard_index(key);
    const auto& shard = shadows[shard_idx];
    return shard && shard->find(key, value);
  }

  std::size_t shadow_bytes(const ShadowContainer& shadows) const {
    std::size_t total = 0;
    for (const auto& shard : shadows) {
      if (shard) {
        total += shard->index_size();
      }
    }
    return total;
  }

  void start_async_worker() {
    async_worker_ = std::thread([this] { async_flush_loop(); });
  }

  void stop_async_worker() {
    if (!async_worker_.joinable()) {
      return;
    }
    {
      std::lock_guard<std::mutex> guard(async_mutex_);
      async_worker_stop_ = true;
      async_flush_requested_ = false;
    }
    async_cv_.notify_all();
    async_worker_.join();
  }

  void finalize_async_flush_if_ready() const {
    if (!async_flush_) {
      return;
    }
    std::lock_guard<std::mutex> guard(async_mutex_);
    if (!async_flush_done_) {
      return;
    }
    flushing_buffer_ = BufferIndex();
    flushing_lipp_shards_.clear();
    flushing_buffer_item_count_ = 0;
    flush_in_progress_ = false;
    flush_resume_valid_ = false;
    flush_skip_equal_ = false;
    async_flush_done_ = false;
  }

  void async_flush_loop() {
    while (true) {
      {
        std::unique_lock<std::mutex> lock(async_mutex_);
        async_cv_.wait(lock, [this] { return async_worker_stop_ || async_flush_requested_; });
        if (async_worker_stop_) {
          return;
        }
        async_flush_requested_ = false;
      }

      auto it = flushing_buffer_.lower_bound(std::numeric_limits<KeyType>::min());
      while (it != flushing_buffer_.end()) {
        insert_into_main_lipp(it->key(), it->value());
        ++it;
      }

      std::lock_guard<std::mutex> guard(async_mutex_);
      async_flush_done_ = true;
    }
  }

  typename BufferIndex::iterator flush_iterator() const {
    auto begin_key = std::numeric_limits<KeyType>::min();
    auto it = flush_resume_valid_ ? flushing_buffer_.lower_bound(flush_resume_key_)
                                  : flushing_buffer_.lower_bound(begin_key);
    if (flush_resume_valid_ && flush_skip_equal_ &&
        it != flushing_buffer_.end() && it->key() == flush_resume_key_) {
      ++it;
    }
    return it;
  }

  std::size_t compute_insert_drain_budget() const {
    std::size_t budget = insert_flush_batch_size_;
    if (!flush_in_progress_) {
      return budget;
    }

    if (active_buffer_item_count_ >= flush_threshold_) {
      budget = std::max(budget, insert_flush_batch_size_ * kBacklogDrainMultiplier);
    }

    std::size_t pressure_window =
        std::max<std::size_t>(std::size_t{1}, flush_threshold_ / kAdaptivePressureDivisor);
    std::size_t pressure_steps = active_buffer_item_count_ / pressure_window;
    if (flushing_buffer_item_count_ > flush_threshold_ / 2) {
      ++pressure_steps;
    }
    pressure_steps = std::min(pressure_steps, kMaxAdaptivePressureSteps);
    budget = std::max(budget, insert_flush_batch_size_ * (1 + pressure_steps));
    return budget;
  }

  void drain_flush_budget(std::size_t budget) const {
    if (async_flush_) {
      finalize_async_flush_if_ready();
      return;
    }
    if (!flush_in_progress_ || budget == 0) {
      return;
    }

    auto it = flush_iterator();
    std::size_t processed = 0;
    while (it != flushing_buffer_.end() && processed < budget) {
      insert_into_main_lipp(it->key(), it->value());
      flush_resume_key_ = it->key();
      flush_resume_valid_ = true;
      flush_skip_equal_ = true;
      ++it;
      ++processed;
    }

    if (processed > 0) {
      flushing_buffer_item_count_ -= std::min(flushing_buffer_item_count_, processed);
    }

    if (it == flushing_buffer_.end()) {
      flushing_buffer_ = BufferIndex();
      flushing_lipp_shards_.clear();
      flushing_buffer_item_count_ = 0;
      flush_in_progress_ = false;
      flush_resume_valid_ = false;
      flush_skip_equal_ = false;
    }
  }

  mutable LIPP<KeyType, uint64_t> lipp_;
  mutable ShadowContainer active_lipp_shards_;
  mutable ShadowContainer flushing_lipp_shards_;
  mutable BufferIndex active_buffer_;
  mutable BufferIndex flushing_buffer_;
  mutable std::size_t active_buffer_item_count_ = 0;
  mutable std::size_t flushing_buffer_item_count_ = 0;
  mutable bool flush_in_progress_ = false;
  mutable KeyType flush_resume_key_ {};
  mutable bool flush_resume_valid_ = false;
  mutable bool flush_skip_equal_ = false;
  mutable std::shared_mutex lipp_mutex_;
  mutable std::mutex async_mutex_;
  mutable std::condition_variable async_cv_;
  mutable bool async_flush_requested_ = false;
  mutable bool async_flush_done_ = false;
  bool async_worker_stop_ = false;
  std::thread async_worker_;
  std::size_t flush_threshold_ = kDefaultFlushThreshold;
  std::size_t insert_flush_batch_size_ = kDefaultInsertFlushBatchSize;
  std::size_t lookup_flush_batch_size_ = kDefaultLookupFlushBatchSize;
  bool prefer_lipp_first_ = false;
  bool use_lipp_shadows_ = false;
  std::size_t shadow_shard_bits_ = 0;
  bool async_flush_ = false;
  bool direct_main_lipp_ = false;
};
