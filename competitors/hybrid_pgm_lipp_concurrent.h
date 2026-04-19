#pragma once

#include <algorithm>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <type_traits>
#include <vector>

#include "./lipp/src/core/lipp.h"
#include "PGM-index/include/pgm_index_dynamic.hpp"
#include "base.h"
#include "searches/branching_binary_search.h"

template <class KeyType, class SearchClass = BranchingBinarySearch<0>,
          std::size_t kDynamicPGMError = 128>
class HybridPGMLIPPConcurrentWorkloadAware : public Base<KeyType> {
 public:
  HybridPGMLIPPConcurrentWorkloadAware(const std::vector<int>& params) {
    if (!params.empty()) {
      if (params[0] == 1) {
        mode_ = Mode::kDeltaPGM;
      } else if (params[0] == 2) {
        mode_ = Mode::kDirectLIPP;
      } else if (params[0] == 3) {
        mode_ = Mode::kLocalPGM;
      }
    }
    if (params.size() > 1 && params[1] > 0) {
      shard_bits_ = static_cast<std::size_t>(params[1]);
    }
    if (params.size() > 2 && params[2] > 0) {
      pending_flush_threshold_ = static_cast<std::size_t>(params[2]);
    }
  }

  uint64_t Build(const std::vector<KeyValue<KeyType>>& data, size_t num_threads) {
    thread_count_ = num_threads;

    std::vector<std::pair<KeyType, uint64_t>> loading_data;
    loading_data.reserve(data.size());
    for (const auto& item : data) {
      loading_data.emplace_back(item.key, item.value);
    }

    main_lipp_shards_.clear();
    shard_upper_bounds_.clear();
    delta_lipp_shards_.clear();
    dpgm_shards_.clear();
    local_dpgm_shards_.clear();
    pending_dpgm_buffers_.clear();
    shard_mutexes_.clear();
    delta_filter_.reset();
    delta_filters_.clear();

    const std::size_t shard_cnt =
        std::min<std::size_t>(shard_count(), loading_data.size());
    if (mode_ == Mode::kDeltaLIPP) {
      delta_filter_.reset(new uint64_t[global_filter_word_count()]);
      std::fill_n(delta_filter_.get(), global_filter_word_count(), 0ull);
    } else if (mode_ != Mode::kDirectLIPP) {
      delta_filters_.resize(shard_cnt);
      for (std::size_t shard_id = 0; shard_id < shard_cnt; ++shard_id) {
        delta_filters_[shard_id].reset(new uint64_t[sharded_filter_word_count()]);
        std::fill_n(delta_filters_[shard_id].get(), sharded_filter_word_count(),
                    0ull);
      }
    }
    if (mode_ == Mode::kDeltaLIPP) {
      delta_lipp_shards_.resize(shard_cnt);
    } else if (mode_ == Mode::kDeltaPGM) {
      dpgm_shards_.resize(shard_cnt);
    } else if (mode_ == Mode::kLocalPGM && thread_count_ > 0) {
      local_dpgm_shards_.resize(thread_count_);
      for (std::size_t thread_id = 0; thread_id < thread_count_; ++thread_id) {
        local_dpgm_shards_[thread_id].reserve(shard_cnt);
        for (std::size_t shard_id = 0; shard_id < shard_cnt; ++shard_id) {
          local_dpgm_shards_[thread_id].push_back(
              std::make_unique<LocalBufferShard>());
        }
      }
    }
    shard_mutexes_.reserve(shard_cnt);
    main_lipp_shards_.reserve(shard_cnt);
    shard_upper_bounds_.reserve(shard_cnt);
    for (std::size_t shard_id = 0; shard_id < shard_cnt; ++shard_id) {
      shard_mutexes_.push_back(std::make_unique<std::shared_mutex>());
    }
    if (mode_ == Mode::kDeltaPGM && thread_count_ > 0) {
      pending_dpgm_buffers_.resize(thread_count_);
      for (std::size_t thread_id = 0; thread_id < thread_count_; ++thread_id) {
        pending_dpgm_buffers_[thread_id].reserve(shard_cnt);
        for (std::size_t shard_id = 0; shard_id < shard_cnt; ++shard_id) {
          auto pending = std::make_unique<PendingBuffer>();
          pending->entries.reserve(pending_flush_threshold_);
          pending_dpgm_buffers_[thread_id].push_back(std::move(pending));
        }
      }
    }

    return util::timing([&] {
      std::size_t shard_begin = 0;
      for (std::size_t shard_id = 0; shard_id < shard_cnt; ++shard_id) {
        std::size_t shard_end = loading_data.size() * (shard_id + 1) / shard_cnt;
        if (shard_end <= shard_begin) {
          shard_end = shard_begin + 1;
        }

        auto shard = std::make_unique<DeltaLIPP>();
        shard->bulk_load(loading_data.data() + shard_begin,
                         shard_end - shard_begin);
        shard_upper_bounds_.push_back(loading_data[shard_end - 1].first);
        main_lipp_shards_.push_back(std::move(shard));
        shard_begin = shard_end;
      }
    });
  }

  size_t EqualityLookup(const KeyType& lookup_key, uint32_t thread_id) const {
    (void)thread_id;

    const auto shard_id = shard_index(lookup_key);
    uint64_t value;
    if (mode_ == Mode::kDirectLIPP) {
      std::shared_lock<std::shared_mutex> guard(*shard_mutexes_[shard_id]);
      if (main_lipp_shards_[shard_id]->find(lookup_key, value)) {
        return value;
      }
      return util::OVERFLOW;
    }

    if (main_lipp_shards_[shard_id]->find(lookup_key, value)) {
      return value;
    }
    if (!delta_filter_may_contain(shard_id, lookup_key)) {
      return util::OVERFLOW;
    }

    if (mode_ == Mode::kDeltaLIPP) {
      std::shared_lock<std::shared_mutex> guard(*shard_mutexes_[shard_id]);
      const auto& shard = delta_lipp_shards_[shard_id];
      if (shard && shard->find(lookup_key, value)) {
        return value;
      }
      return util::OVERFLOW;
    }

    if (mode_ == Mode::kLocalPGM) {
      for (const auto& per_thread_shards : local_dpgm_shards_) {
        if (shard_id >= per_thread_shards.size()) {
          continue;
        }
        const auto& shard = *per_thread_shards[shard_id];
        std::shared_lock<std::shared_mutex> guard(shard.mutex);
        auto it = shard.index.find(lookup_key);
        if (it != shard.index.end()) {
          return it->value();
        }
      }
      return util::OVERFLOW;
    }

    if (find_in_pending_buffers(shard_id, lookup_key, value)) {
      return value;
    }

    std::shared_lock<std::shared_mutex> guard(*shard_mutexes_[shard_id]);
    const auto& shard = dpgm_shards_[shard_id];
    auto it = shard.find(lookup_key);
    return it != shard.end() ? it->value() : util::OVERFLOW;
  }

  uint64_t RangeQuery(const KeyType& lower_key, const KeyType& upper_key,
                      uint32_t thread_id) const {
    (void)thread_id;

    uint64_t result = 0;
    const auto lower_shard = shard_index(lower_key);
    const auto upper_shard = shard_index(upper_key);
    if (mode_ == Mode::kDirectLIPP) {
      for (std::size_t shard_id = lower_shard;
           shard_id <= upper_shard && shard_id < main_lipp_shards_.size();
           ++shard_id) {
        std::shared_lock<std::shared_mutex> guard(*shard_mutexes_[shard_id]);
        auto it = main_lipp_shards_[shard_id]->lower_bound(lower_key);
        while (it != main_lipp_shards_[shard_id]->end() &&
               it->comp.data.key <= upper_key) {
          result += it->comp.data.value;
          ++it;
        }
      }
      return result;
    }

    for (std::size_t shard_id = lower_shard;
         shard_id <= upper_shard && shard_id < main_lipp_shards_.size();
         ++shard_id) {
      auto main_it = main_lipp_shards_[shard_id]->lower_bound(lower_key);
      while (main_it != main_lipp_shards_[shard_id]->end() &&
             main_it->comp.data.key <= upper_key) {
        result += main_it->comp.data.value;
        ++main_it;
      }
    }

    if (mode_ == Mode::kDeltaLIPP) {
      for (std::size_t shard_id = 0; shard_id < delta_lipp_shards_.size(); ++shard_id) {
        std::shared_lock<std::shared_mutex> guard(*shard_mutexes_[shard_id]);
        const auto& shard = delta_lipp_shards_[shard_id];
        if (!shard) {
          continue;
        }
        auto it = shard->lower_bound(lower_key);
        while (it != shard->end() && it->comp.data.key <= upper_key) {
          result += it->comp.data.value;
          ++it;
        }
      }
      return result;
    }

    if (mode_ == Mode::kLocalPGM) {
      for (const auto& per_thread_shards : local_dpgm_shards_) {
        if (lower_shard >= per_thread_shards.size()) {
          continue;
        }
        for (std::size_t shard_id = lower_shard;
             shard_id <= upper_shard && shard_id < per_thread_shards.size();
             ++shard_id) {
          const auto& shard = *per_thread_shards[shard_id];
          std::shared_lock<std::shared_mutex> guard(shard.mutex);
          auto it = shard.index.lower_bound(lower_key);
          while (it != shard.index.end() && it->key() <= upper_key) {
            result += it->value();
            ++it;
          }
        }
      }
      return result;
    }

    for (std::size_t shard_id = 0; shard_id < dpgm_shards_.size(); ++shard_id) {
      accumulate_pending_range(shard_id, lower_key, upper_key, result);
      std::shared_lock<std::shared_mutex> guard(*shard_mutexes_[shard_id]);
      auto it = dpgm_shards_[shard_id].lower_bound(lower_key);
      while (it != dpgm_shards_[shard_id].end() && it->key() <= upper_key) {
        result += it->value();
        ++it;
      }
    }
    return result;
  }

  void Insert(const KeyValue<KeyType>& data, uint32_t thread_id) {
    const auto shard_id = shard_index(data.key);
    if (mode_ == Mode::kDirectLIPP) {
      std::unique_lock<std::shared_mutex> guard(*shard_mutexes_[shard_id]);
      main_lipp_shards_[shard_id]->insert(data.key, data.value);
      return;
    }

    if (mode_ == Mode::kDeltaLIPP) {
      std::unique_lock<std::shared_mutex> guard(*shard_mutexes_[shard_id]);
      if (!delta_lipp_shards_[shard_id]) {
        delta_lipp_shards_[shard_id] = std::make_unique<DeltaLIPP>();
      }
      delta_lipp_shards_[shard_id]->insert(data.key, data.value);
      delta_filter_add(shard_id, data.key);
      return;
    }

    if (mode_ == Mode::kLocalPGM && !local_dpgm_shards_.empty()) {
      const auto local_thread_id = thread_id % local_dpgm_shards_.size();
      auto& shard = *local_dpgm_shards_[local_thread_id][shard_id];
      {
        std::unique_lock<std::shared_mutex> guard(shard.mutex);
        shard.index.insert(data.key, data.value);
      }
      delta_filter_add(shard_id, data.key);
      return;
    }

    if (thread_id < pending_dpgm_buffers_.size()) {
      auto& pending = *pending_dpgm_buffers_[thread_id][shard_id];
      bool should_flush = false;
      {
        std::lock_guard<std::mutex> guard(pending.mutex);
        pending.entries.push_back(data);
        should_flush = pending.entries.size() >= pending_flush_threshold_;
      }
      delta_filter_add(shard_id, data.key);
      if (should_flush) {
        flush_pending_buffer(thread_id, shard_id);
      }
      return;
    }

    std::unique_lock<std::shared_mutex> guard(*shard_mutexes_[shard_id]);
    dpgm_shards_[shard_id].insert(data.key, data.value);
    delta_filter_add(shard_id, data.key);
  }

  bool applicable(bool unique, bool range_query, bool insert, bool multithread,
                  const std::string& ops_filename) const {
    (void)range_query;
    (void)insert;
    (void)ops_filename;
    return unique && multithread;
  }

  std::string name() const { return "HybridPGMLIPPConcurrentWorkloadAware"; }

  std::vector<std::string> variants() const {
    if (mode_ == Mode::kDirectLIPP) {
      return {"direct-lipp", std::to_string(shard_bits_)};
    }
    if (mode_ == Mode::kDeltaLIPP) {
      return {"delta-lipp", std::to_string(shard_bits_)};
    }
    if (mode_ == Mode::kLocalPGM) {
      return {"local-dpgm-sbf", std::to_string(shard_bits_),
              SearchClass::name() + "-e" + std::to_string(kDynamicPGMError)};
    }
    return {"delta-dpgm-sbf", std::to_string(shard_bits_),
            SearchClass::name() + "-e" + std::to_string(kDynamicPGMError),
            std::to_string(pending_flush_threshold_)};
  }

  std::size_t size() const {
    std::size_t total = 0;
    for (const auto& shard : main_lipp_shards_) {
      total += shard->index_size();
    }
    if (mode_ == Mode::kDirectLIPP) {
      return total;
    }
    total += delta_filter_bytes();
    if (mode_ == Mode::kDeltaLIPP) {
      for (const auto& shard : delta_lipp_shards_) {
        if (shard) {
          total += shard->index_size();
        }
      }
      return total;
    }
    if (mode_ == Mode::kLocalPGM) {
      for (const auto& per_thread_shards : local_dpgm_shards_) {
        for (const auto& shard : per_thread_shards) {
          total += shard->index.size_in_bytes();
        }
      }
      return total;
    }

    for (const auto& shard : dpgm_shards_) {
      total += shard.size_in_bytes();
    }
    total += pending_entries() * sizeof(KeyValue<KeyType>);
    return total;
  }

 private:
  enum class Mode {
    kDeltaLIPP,
    kDeltaPGM,
    kDirectLIPP,
    kLocalPGM,
  };

  using BufferSearch = SearchClass;
  using BufferPGM = PGMIndex<KeyType, BufferSearch, kDynamicPGMError, 16>;
  using BufferIndex = DynamicPGMIndex<KeyType, uint64_t, BufferSearch, BufferPGM>;
  using DeltaLIPP = LIPP<KeyType, uint64_t>;
  struct PendingBuffer {
    mutable std::mutex mutex;
    std::vector<KeyValue<KeyType>> entries;
  };
  struct LocalBufferShard {
    mutable std::shared_mutex mutex;
    BufferIndex index;
  };
  static constexpr std::size_t kGlobalFilterBits = std::size_t{1} << 22;
  static constexpr std::size_t kShardedFilterBits = std::size_t{1} << 18;
  static constexpr std::size_t kFilterHashes = 3;
  static constexpr std::size_t kDefaultPendingFlushThreshold = 64;

  std::size_t shard_count() const {
    return shard_bits_ == 0 ? 1 : (std::size_t{1} << shard_bits_);
  }

  std::size_t shard_index(const KeyType& key) const {
    const auto it = std::lower_bound(shard_upper_bounds_.begin(),
                                     shard_upper_bounds_.end(), key);
    if (it == shard_upper_bounds_.end()) {
      return shard_upper_bounds_.size() - 1;
    }
    return static_cast<std::size_t>(it - shard_upper_bounds_.begin());
  }

  std::size_t global_filter_word_count() const { return kGlobalFilterBits / 64; }

  std::size_t sharded_filter_word_count() const {
    return kShardedFilterBits / 64;
  }

  static uint64_t mix_key(uint64_t value) {
    value += 0x9e3779b97f4a7c15ull;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
    return value ^ (value >> 31);
  }

  uint64_t key_to_u64(const KeyType& key) const {
    if constexpr (std::is_integral<KeyType>::value) {
      return static_cast<uint64_t>(key);
    }
    return static_cast<uint64_t>(std::hash<KeyType>{}(key));
  }

  std::size_t delta_filter_bytes() const {
    if (mode_ == Mode::kDeltaLIPP && delta_filter_) {
      return global_filter_word_count() * sizeof(uint64_t);
    }
    return delta_filters_.size() * sharded_filter_word_count() *
           sizeof(uint64_t);
  }

  bool delta_filter_may_contain(std::size_t shard_id, const KeyType& key) const {
    if (mode_ == Mode::kDeltaLIPP) {
      if (!delta_filter_) {
        return true;
      }
      const uint64_t mixed = mix_key(key_to_u64(key));
      uint64_t step = mix_key(mixed ^ 0x9e3779b97f4a7c15ull);
      step |= 1ull;
      for (std::size_t hash_id = 0; hash_id < kFilterHashes; ++hash_id) {
        const uint64_t bit_index =
            (mixed + hash_id * step) & (kGlobalFilterBits - 1);
        const uint64_t mask = 1ull << (bit_index & 63);
        if ((__atomic_load_n(&delta_filter_[bit_index >> 6], __ATOMIC_RELAXED) &
             mask) == 0) {
          return false;
        }
      }
      return true;
    }
    if (shard_id >= delta_filters_.size()) {
      return true;
    }
    const uint64_t* filter = delta_filters_[shard_id].get();
    const uint64_t mixed = mix_key(key_to_u64(key));
    uint64_t step = mix_key(mixed ^ 0x9e3779b97f4a7c15ull);
    step |= 1ull;
    for (std::size_t hash_id = 0; hash_id < kFilterHashes; ++hash_id) {
      const uint64_t bit_index =
          (mixed + hash_id * step) & (kShardedFilterBits - 1);
      const uint64_t mask = 1ull << (bit_index & 63);
      if ((__atomic_load_n(&filter[bit_index >> 6], __ATOMIC_RELAXED) & mask) ==
          0) {
        return false;
      }
    }
    return true;
  }

  void delta_filter_add(std::size_t shard_id, const KeyType& key) {
    if (mode_ == Mode::kDeltaLIPP) {
      if (!delta_filter_) {
        return;
      }
      const uint64_t mixed = mix_key(key_to_u64(key));
      uint64_t step = mix_key(mixed ^ 0x9e3779b97f4a7c15ull);
      step |= 1ull;
      for (std::size_t hash_id = 0; hash_id < kFilterHashes; ++hash_id) {
        const uint64_t bit_index =
            (mixed + hash_id * step) & (kGlobalFilterBits - 1);
        const uint64_t mask = 1ull << (bit_index & 63);
        __atomic_fetch_or(&delta_filter_[bit_index >> 6], mask,
                          __ATOMIC_RELAXED);
      }
      return;
    }
    if (shard_id >= delta_filters_.size()) {
      return;
    }
    uint64_t* filter = delta_filters_[shard_id].get();
    const uint64_t mixed = mix_key(key_to_u64(key));
    uint64_t step = mix_key(mixed ^ 0x9e3779b97f4a7c15ull);
    step |= 1ull;
    for (std::size_t hash_id = 0; hash_id < kFilterHashes; ++hash_id) {
      const uint64_t bit_index =
          (mixed + hash_id * step) & (kShardedFilterBits - 1);
      const uint64_t mask = 1ull << (bit_index & 63);
      __atomic_fetch_or(&filter[bit_index >> 6], mask, __ATOMIC_RELAXED);
    }
  }

  bool find_in_pending_buffers(std::size_t shard_id, const KeyType& lookup_key,
                               uint64_t& value) const {
    for (const auto& per_thread_buffers : pending_dpgm_buffers_) {
      if (shard_id >= per_thread_buffers.size()) {
        continue;
      }
      const auto& pending = *per_thread_buffers[shard_id];
      std::lock_guard<std::mutex> guard(pending.mutex);
      for (auto it = pending.entries.rbegin(); it != pending.entries.rend(); ++it) {
        if (it->key == lookup_key) {
          value = it->value;
          return true;
        }
      }
    }
    return false;
  }

  void accumulate_pending_range(std::size_t shard_id, const KeyType& lower_key,
                                const KeyType& upper_key, uint64_t& result) const {
    for (const auto& per_thread_buffers : pending_dpgm_buffers_) {
      if (shard_id >= per_thread_buffers.size()) {
        continue;
      }
      const auto& pending = *per_thread_buffers[shard_id];
      std::lock_guard<std::mutex> guard(pending.mutex);
      for (const auto& item : pending.entries) {
        if (item.key >= lower_key && item.key <= upper_key) {
          result += item.value;
        }
      }
    }
  }

  void flush_pending_buffer(std::size_t thread_id, std::size_t shard_id) {
    if (thread_id >= pending_dpgm_buffers_.size() ||
        shard_id >= pending_dpgm_buffers_[thread_id].size()) {
      return;
    }

    std::vector<KeyValue<KeyType>> batch;
    auto& pending = *pending_dpgm_buffers_[thread_id][shard_id];
    {
      std::lock_guard<std::mutex> guard(pending.mutex);
      if (pending.entries.empty()) {
        return;
      }
      batch.swap(pending.entries);
    }

    std::unique_lock<std::shared_mutex> guard(*shard_mutexes_[shard_id]);
    for (const auto& item : batch) {
      dpgm_shards_[shard_id].insert(item.key, item.value);
    }
  }

  std::size_t pending_entries() const {
    std::size_t total = 0;
    for (const auto& per_thread_buffers : pending_dpgm_buffers_) {
      for (const auto& pending : per_thread_buffers) {
        std::lock_guard<std::mutex> guard(pending->mutex);
        total += pending->entries.size();
      }
    }
    return total;
  }

  mutable std::vector<std::unique_ptr<DeltaLIPP>> main_lipp_shards_;
  mutable std::vector<KeyType> shard_upper_bounds_;
  mutable std::vector<std::unique_ptr<DeltaLIPP>> delta_lipp_shards_;
  mutable std::vector<BufferIndex> dpgm_shards_;
  mutable std::vector<std::vector<std::unique_ptr<LocalBufferShard>>> local_dpgm_shards_;
  mutable std::vector<std::vector<std::unique_ptr<PendingBuffer>>> pending_dpgm_buffers_;
  mutable std::vector<std::unique_ptr<std::shared_mutex>> shard_mutexes_;
  mutable std::unique_ptr<uint64_t[]> delta_filter_;
  mutable std::vector<std::unique_ptr<uint64_t[]>> delta_filters_;
  Mode mode_ = Mode::kDeltaLIPP;
  std::size_t shard_bits_ = 8;
  std::size_t thread_count_ = 0;
  std::size_t pending_flush_threshold_ = kDefaultPendingFlushThreshold;
};
