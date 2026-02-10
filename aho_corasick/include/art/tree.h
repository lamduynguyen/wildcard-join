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
#include "common/utf8.h"

namespace aho_corasick {
class AhoCorasick;
}

namespace ART {

class Tree {
 private:
  friend class aho_corasick::AhoCorasick;

  N *const root;
  Epoche epoche{256};

  inline void yield(int count) const {
    if (count > 3)
      sched_yield();
    else
      _mm_pause();
  }

  inline auto getNextChar(const char *keyword, uint64_t keywordLen, bool mustAppendNull, uint64_t index) {
    return (index >= keywordLen) ? (assert(mustAppendNull), NULL_TERMINATOR) : keyword[index];
  }

 public:
  Tree();

  Tree(const Tree &) = delete;

  Tree(Tree &&t) : root(t.root) {}

  ~Tree();

  ThreadInfo getThreadInfo();

  Leaf *lookup(const char *keyword, uint64_t keywordLen, bool requiresNullTerminated, ThreadInfo &threadEpocheInfo);

  template <typename NewKeyFn, typename UpsertFn>
  void insert(const char *keyword, uint64_t keywordLen, bool mustAppendNull, NewKeyFn &&insert_fn, UpsertFn &&upsert_fn,
              ThreadInfo &epocheInfo) {
    assert(keywordLen > 0 && ((keyword[keywordLen - 1] == NULL_TERMINATOR) == !mustAppendNull));

    // -----------------------------
    // Step 1: Build isEndCodePoint array
    // -----------------------------
    std::vector<bool> isEndCodePoint(keywordLen + mustAppendNull, true);
    const char *end = keyword + keywordLen + mustAppendNull;
    for (auto ptr = keyword; ptr < end;) {
      auto result         = umbra::Utf8::readCodePoint(ptr, end);
      const char *nextPtr = result.next;
      // All bytes except the last are not end of code point
      for (const char *b = ptr; b < nextPtr - 1; ++b) { isEndCodePoint[b - keyword] = false; }
      ptr = nextPtr;
    }

    // -----------------------------
    // Step 1: Normal trie insertion
    // -----------------------------
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

      nodeKey  = getNextChar(keyword, keywordLen, mustAppendNull, level);
      nextNode = N::getChild(nodeKey, node);
      node->checkOrRestart(v, needRestart);
      if (needRestart) goto restart;

      if (nextNode == nullptr) {
        auto generateVal = [&]() {
          auto lastNode = N::setLeaf(
            Leaf::MakeLeaf(reinterpret_cast<const uint8_t *>(keyword), keywordLen, mustAppendNull, insert_fn()));
          if (level < keywordLen - 1 + mustAppendNull) {
            auto range = std::views::iota(level + 1, keywordLen + mustAppendNull) | std::views::reverse;
            for (auto idx : range) {
              auto aboveKey = getNextChar(keyword, keywordLen, mustAppendNull, idx);
              auto n4       = N4::makeNode(isEndCodePoint[idx]);
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
        if (leaf->equal(keyword, keywordLen, mustAppendNull)) {
          // upsert
          auto tid = N::getLeaf(nextNode)->auxIndex;
          upsert_fn(tid);
          node->writeUnlock();
          return;
        }

        // Create new inner node to replace the leaf
        auto iterNode = N4::makeNode(isEndCodePoint[level]);
        N::change(node, nodeKey, iterNode);
        level++;
        assert(level < leaf->keyLen);  // prevent inserting when prefix of key exists already
        // Start inserting new intermediate nodes to represent shared prefix
        uint32_t prefixLength = 0;
        for (; (*leaf)[level + prefixLength] == getNextChar(keyword, keywordLen, mustAppendNull, level + prefixLength);
             ++prefixLength) {
          auto n4 = N4::makeNode(isEndCodePoint[level + prefixLength]);
          iterNode->insert(getNextChar(keyword, keywordLen, mustAppendNull, level + prefixLength), n4);
          iterNode = n4;
        }
        auto newNodeKey = getNextChar(keyword, keywordLen, mustAppendNull, level + prefixLength);
        assert(iterNode->getType() == NTypes::N4);            // Guarantee to be N4 here
        assert(newNodeKey != (*leaf)[level + prefixLength]);  // should be different key here
        reinterpret_cast<N4 *>(iterNode)->insert(keyword[level + prefixLength],
                                                 N::setLeaf(Leaf::MakeLeaf(reinterpret_cast<const uint8_t *>(keyword),
                                                                           keywordLen, mustAppendNull, insert_fn())));
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
