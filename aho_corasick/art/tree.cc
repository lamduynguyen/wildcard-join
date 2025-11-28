#include <algorithm>
#include <cassert>

#include "art/epoche.h"
#include "art/key.h"
#include "art/node.h"
#include "art/tree.h"

namespace ART {

Tree::Tree(LoadKeyFunction loadKey, CheckKeyFunction checkKey)
    : root(new N256(nullptr, 0)), loadKey(loadKey), checkKey(checkKey) {}

Tree::~Tree() {
  N::deleteChildren(root);
  N::deleteNode(root);
}

ThreadInfo Tree::getThreadInfo() { return ThreadInfo(this->epoche); }

void yield(int count) {
  if (count > 3)
    sched_yield();
  else
    _mm_pause();
}

TupleID Tree::lookup(const Key &k, ThreadInfo &threadEpocheInfo) const {
  EpocheGuardReadonly epocheGuard(threadEpocheInfo);
  int restartCount = 0;
restart:
  if (restartCount++) yield(restartCount);
  bool needRestart = false;

  N *node;
  N *parentNode = nullptr;
  uint64_t v;
  uint32_t level             = 0;
  bool optimisticPrefixMatch = false;

  node = root;
  v    = node->readLockOrRestart(needRestart);
  if (needRestart) goto restart;
  while (true) {
    switch (checkPrefix(node, k, level)) {  // increases level
      case CheckPrefixResult::NoMatch:
        node->readUnlockOrRestart(v, needRestart);
        if (needRestart) goto restart;
        return INVALID_TID;
      case CheckPrefixResult::OptimisticMatch: optimisticPrefixMatch = true; [[fallthrough]];
      case CheckPrefixResult::Match:
        if (k.getKeyLen() <= level) { return INVALID_TID; }
        parentNode = node;
        node       = N::getChild(k[level], parentNode);
        parentNode->checkOrRestart(v, needRestart);
        if (needRestart) goto restart;

        if (node == nullptr) { return INVALID_TID; }
        if (N::isLeaf(node)) {
          parentNode->readUnlockOrRestart(v, needRestart);
          if (needRestart) goto restart;

          TupleID tid = N::getLeaf(node);
          if (level < k.getKeyLen() - 1 || optimisticPrefixMatch) {
            auto check = checkKey(tid, k);
            return (check) ? tid : INVALID_TID;
          }
          return tid;
        }
        level++;
    }
    uint64_t nv = node->readLockOrRestart(needRestart);
    if (needRestart) goto restart;

    parentNode->readUnlockOrRestart(v, needRestart);
    if (needRestart) goto restart;
    v = nv;
  }
}

void Tree::insert(const Key &k, TupleID tid, ThreadInfo &epocheInfo) {
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

    uint32_t nextLevel = level;

    uint8_t nonMatchingKey;
    Prefix remainingPrefix;
    auto res = checkPrefixPessimistic(node, k, nextLevel, nonMatchingKey, remainingPrefix, loadKey,
                                      needRestart);  // increases level
    if (needRestart) goto restart;
    switch (res) {
      case CheckPrefixPessimisticResult::NoMatch: {
        parentNode->upgradeToWriteLockOrRestart(parentVersion, needRestart);
        if (needRestart) goto restart;

        node->upgradeToWriteLockOrRestart(v, needRestart);
        if (needRestart) {
          parentNode->writeUnlock();
          goto restart;
        }
        // 1) Create new node which will be parent of node, Set common prefix, level to this node
        auto newNode = new N4(node->getPrefix(), nextLevel - level);

        // 2)  add node and (tid, *k) as children
        newNode->insert(k[nextLevel], N::setLeaf(tid));
        newNode->insert(nonMatchingKey, node);

        // 3) upgradeToWriteLockOrRestart, update parentNode to point to the new node, unlock
        N::change(parentNode, parentKey, newNode);
        parentNode->writeUnlock();

        // 4) update prefix of node, unlock
        node->setPrefix(remainingPrefix, node->getPrefixLength() - ((nextLevel - level) + 1));

        node->writeUnlock();
        return;
      }
      case CheckPrefixPessimisticResult::Match: break;
    }
    level    = nextLevel;
    nodeKey  = k[level];
    nextNode = N::getChild(nodeKey, node);
    node->checkOrRestart(v, needRestart);
    if (needRestart) goto restart;

    if (nextNode == nullptr) {
      N::insertAndUnlock(node, v, parentNode, parentVersion, parentKey, nodeKey, N::setLeaf(tid), needRestart,
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

      Key key;
      loadKey(N::getLeaf(nextNode), key);

      if (key == k) {
        // upsert
        N::change(node, k[level], N::setLeaf(tid));
        node->writeUnlock();
        return;
      }

      level++;
      assert(level < key.getKeyLen());  // prevent inserting when prefix of key exists already
      uint32_t prefixLength = 0;
      while (key[level + prefixLength] == k[level + prefixLength]) { prefixLength++; }

      auto n4 = new N4(&k[level], prefixLength);
      n4->insert(k[level + prefixLength], N::setLeaf(tid));
      n4->insert(key[level + prefixLength], nextNode);
      N::change(node, nodeKey, n4);
      node->writeUnlock();
      return;
    }
    level++;
    parentVersion = v;
  }
}

void Tree::remove(const Key &k, TupleID tid, ThreadInfo &threadInfo) {
  EpocheGuard epocheGuard(threadInfo);
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

    switch (checkPrefix(node, k, level)) {  // increases level
      case CheckPrefixResult::NoMatch:
        node->readUnlockOrRestart(v, needRestart);
        if (needRestart) goto restart;
        return;
      case CheckPrefixResult::OptimisticMatch:
        // fallthrough
      case CheckPrefixResult::Match: {
        nodeKey  = k[level];
        nextNode = N::getChild(nodeKey, node);

        node->checkOrRestart(v, needRestart);
        if (needRestart) goto restart;

        if (nextNode == nullptr) {
          node->readUnlockOrRestart(v, needRestart);
          if (needRestart) goto restart;
          return;
        }
        if (N::isLeaf(nextNode)) {
          if (N::getLeaf(nextNode) != tid) { return; }
          assert(parentNode == nullptr || node->getCount() != 1);
          if (node->getCount() == 2 && parentNode != nullptr) {
            parentNode->upgradeToWriteLockOrRestart(parentVersion, needRestart);
            if (needRestart) goto restart;

            node->upgradeToWriteLockOrRestart(v, needRestart);
            if (needRestart) {
              parentNode->writeUnlock();
              goto restart;
            }
            // 1. check remaining entries
            N *secondNodeN;
            uint8_t secondNodeK;
            std::tie(secondNodeN, secondNodeK) = N::getSecondChild(node, nodeKey);
            if (N::isLeaf(secondNodeN)) {
              // N::remove(node, k[level]); not necessary
              N::change(parentNode, parentKey, secondNodeN);

              parentNode->writeUnlock();
              node->writeUnlockObsolete();
              this->epoche.markNodeForDeletion(node, threadInfo);
            } else {
              secondNodeN->writeLockOrRestart(needRestart);
              if (needRestart) {
                node->writeUnlock();
                parentNode->writeUnlock();
                goto restart;
              }

              // N::remove(node, k[level]); not necessary
              N::change(parentNode, parentKey, secondNodeN);
              parentNode->writeUnlock();

              secondNodeN->addPrefixBefore(node, secondNodeK);
              secondNodeN->writeUnlock();

              node->writeUnlockObsolete();
              this->epoche.markNodeForDeletion(node, threadInfo);
            }
          } else {
            N::removeAndUnlock(node, v, k[level], parentNode, parentVersion, parentKey, needRestart, threadInfo);
            if (needRestart) goto restart;
          }
          return;
        }
        level++;
        parentVersion = v;
      }
    }
  }
}

}  // namespace ART
