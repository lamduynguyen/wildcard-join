#pragma once

#include "common/flat_map.h"

#include <cstddef>
#include <list>
#include <stdexcept>

namespace aho_corasick {

template <typename key_t, typename value_t>
class LRUCache {
 public:
  typedef typename std::pair<key_t, value_t> key_value_pair_t;
  typedef typename std::list<key_value_pair_t>::iterator list_iterator_t;

  LRUCache(size_t max_size = 0) : max_size_(max_size) {}

  void Upsert(const key_t &key, const value_t &value) {
    auto it = item_map_.find(key);
    item_list_.push_front(key_value_pair_t(key, value));
    if (it != item_map_.end()) {
      item_list_.erase(it->second);
      item_map_.erase(it);
    }
    item_map_[key] = item_list_.begin();

    if (item_map_.size() > max_size_) {
      auto last = item_list_.end();
      last--;
      item_map_.erase(last->first);
      item_list_.pop_back();
    }
  }

  auto Get(const key_t &key) const -> value_t {
    auto it = item_map_.find(key);
    assert(it != item_map_.end());
    return it->second->second;
  }

  auto Contain(const key_t &key) const { return item_map_.find(key) != item_map_.end(); }

  auto Size() const { return item_map_.size(); }

 private:
  std::list<key_value_pair_t> item_list_;
  ankerl::unordered_dense::map<key_t, list_iterator_t> item_map_;
  size_t max_size_;
};

}  // namespace aho_corasick
