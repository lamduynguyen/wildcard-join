//
// Created by florian on 18.11.15.
//

#ifndef ART_OPTIMISTICLOCK_COUPLING_N_H
#define ART_OPTIMISTICLOCK_COUPLING_N_H

#include <algorithm>
#include <cassert>
#include <functional>
#include <ranges>

#include "art/node.h"

namespace aho_corasick {
class AhoCorasick;
}

namespace ART {

class Tree {
 public:
  using LoadKeyFunction                = std::function<void(TupleID tid, Key &key)>;
  static constexpr TupleID INVALID_TID = std::numeric_limits<TupleID>::max();

 private:
  friend class aho_corasick::AhoCorasick;

  N *const root;
  LoadKeyFunction loadKey;
  Epoche epoche{256};

  void yield(int count) const;

 public:
  Tree(LoadKeyFunction loadKey);

  Tree(const Tree &) = delete;

  Tree(Tree &&t) : root(t.root) {}

  ~Tree();

  ThreadInfo getThreadInfo();

  TupleID lookup(const Key &k, ThreadInfo &threadEpocheInfo) const;

  template <typename NewKeyFn, typename UpsertFn>
  void insert(const Key &k, NewKeyFn &&insert_fn, UpsertFn &&upsert_fn, ThreadInfo &epocheInfo) {
    EpocheGuard epocheGuard(epocheInfo);
    int restartCount = 0;
  restart:
    if (restartCount++) yield(restartCount);
    bool needRestart = false;

    N *node       = nullptr;
    N *nextNode   = root;
    N *parentNode = nullptr;
    uint8_t parentKey, nodeKey = 0;
    uint64_t parentVersion = 0;
    uint32_t level         = 0;

    while (true) {
      parentNode = node;
      parentKey  = nodeKey;
      node       = nextNode;
      auto v     = node->readLockOrRestart(needRestart);
      if (needRestart) goto restart;

      nodeKey  = k[level];
      nextNode = N::getChild(nodeKey, node);
      node->checkOrRestart(v, needRestart);
      if (needRestart) goto restart;

      if (nextNode == nullptr) {
        auto generateVal = [&]() {
          auto lastNode = N::setLeaf(insert_fn());
          if (level < k.getKeyLen() - 1) {
            auto range = std::views::iota(level + 1, k.getKeyLen()) | std::views::reverse;
            for (auto idx : range) {
              auto aboveKey = k[idx];
              auto n4       = new N4();
              n4->insert(aboveKey, lastNode);
              lastNode = n4;
            }
          }
          return lastNode;
        };
        N::insertAndUnlock(node, v, parentNode, parentVersion, parentKey, nodeKey, generateVal, needRestart,
                           epocheInfo);
        if (needRestart) goto restart;
        return;
      }

      if (parentNode != nullptr) {
        parentNode->readUnlockOrRestart(parentVersion, needRestart);
        if (needRestart) goto restart;
      }

      if (N::isLeaf(nextNode)) {
        node->upgradeToWriteLockOrRestart(v, needRestart);
        if (needRestart) goto restart;

        // Matching key, call upsert() -- Only works with trie. With prefix tree/radix tree/variants, this is wrong
        if (level + 1 == k.getKeyLen()) {
          auto tid = N::getLeaf(nextNode);
          upsert_fn(tid);
          node->writeUnlock();
          return;
        }

        // Create new inner node to replace the leaf
        Key key;
        loadKey(N::getLeaf(nextNode), key);
        auto iterNode = new N4();
        N::change(node, nodeKey, iterNode);
        level++;
        assert(level < key.getKeyLen());  // prevent inserting when prefix of key exists already
        // Start inserting new intermediate nodes to represent shared prefix
        uint32_t prefixLength = 0;
        for (; key[level + prefixLength] == k[level + prefixLength]; prefixLength++) {
          auto nodeKey = key[level + prefixLength];
          auto n4      = new N4();
          iterNode->insert(nodeKey, n4);
          iterNode = n4;
        }
        assert(iterNode->getType() == NTypes::N4);                     // Guarantee to be N4 here
        assert(k[level + prefixLength] != key[level + prefixLength]);  // should be different key here
        reinterpret_cast<N4 *>(iterNode)->insert(k[level + prefixLength], N::setLeaf(insert_fn()));
        reinterpret_cast<N4 *>(iterNode)->insert(key[level + prefixLength], nextNode);
        node->writeUnlock();
        return;
      }
      level++;
      parentVersion = v;
    }
  }
};

}  // namespace ART

#endif  // ART_OPTIMISTICLOCK_COUPLING_N_H
