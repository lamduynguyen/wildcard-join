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
 private:
  friend class aho_corasick::AhoCorasick;

  N *const root;
  Epoche epoche{256};

  void yield(int count) const;

 public:
  Tree();

  Tree(const Tree &) = delete;

  Tree(Tree &&t) : root(t.root) {}

  ~Tree();

  ThreadInfo getThreadInfo();

  Leaf *lookup(const Key &k, ThreadInfo &threadEpocheInfo) const;

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
          auto lastNode = N::setLeaf(Leaf::MakeLeaf(k.data, k.getKeyLen(), insert_fn()));
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
        auto leaf = reinterpret_cast<Leaf *>(N::getLeaf(nextNode));
        if (*leaf == k) {
          // upsert
          auto tid = N::getLeaf(nextNode)->aux_index;
          upsert_fn(tid);
          node->writeUnlock();
          return;
        }

        // Create new inner node to replace the leaf
        auto iterNode = new N4();
        N::change(node, nodeKey, iterNode);
        level++;
        assert(level < leaf->key_len);  // prevent inserting when prefix of key exists already
        // Start inserting new intermediate nodes to represent shared prefix
        uint32_t prefixLength = 0;
        for (; (*leaf)[level + prefixLength] == k[level + prefixLength]; prefixLength++) {
          auto nodeKey = k[level + prefixLength];
          auto n4      = new N4();
          iterNode->insert(nodeKey, n4);
          iterNode = n4;
        }
        assert(iterNode->getType() == NTypes::N4);                         // Guarantee to be N4 here
        assert(k[level + prefixLength] != (*leaf)[level + prefixLength]);  // should be different key here
        reinterpret_cast<N4 *>(iterNode)->insert(k[level + prefixLength],
                                                 N::setLeaf(Leaf::MakeLeaf(k.data, k.getKeyLen(), insert_fn())));
        reinterpret_cast<N4 *>(iterNode)->insert((*leaf)[level + prefixLength], nextNode);
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
