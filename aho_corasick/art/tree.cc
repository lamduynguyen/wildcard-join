#include <algorithm>
#include <cassert>

#include "art/epoche.h"
#include "art/key.h"
#include "art/node.h"
#include "art/tree.h"

namespace ART {

Tree::Tree(LoadKeyFunction loadKey, CheckKeyFunction checkKey)
    : root(new N256()), loadKey(loadKey), checkKey(checkKey) {}

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
  uint32_t level = 0;

  node = root;
  v    = node->readLockOrRestart(needRestart);
  if (needRestart) goto restart;
  while (true) {
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
      if (level < k.getKeyLen() - 1) {
        auto check = checkKey(tid, k);
        return (check) ? tid : INVALID_TID;
      }
      return tid;
    }
    level++;

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
      // Create new inner node to replace the leaf
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
      reinterpret_cast<N4 *>(iterNode)->insert(k[level + prefixLength], N::setLeaf(tid));
      reinterpret_cast<N4 *>(iterNode)->insert(key[level + prefixLength], nextNode);
      node->writeUnlock();
      return;
    }
    level++;
    parentVersion = v;
  }
}

}  // namespace ART
