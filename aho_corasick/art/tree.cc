#include <algorithm>
#include <cassert>
#include <ranges>

#include "art/epoche.h"
#include "art/key.h"
#include "art/node.h"
#include "art/tree.h"

namespace ART {

// TODO: Should we consider null to be a codepoint end?
Tree::Tree() : root(N256::makeNode(true)) {}

Tree::~Tree() {
  N::deleteChildren(root);
  N::deleteNode(root);
}

ThreadInfo Tree::getThreadInfo() { return ThreadInfo(this->epoche); }

void Tree::yield(int count) const {
  if (count > 3)
    sched_yield();
  else
    _mm_pause();
}

Leaf *Tree::lookup(const Key &k, ThreadInfo &threadEpocheInfo) const {
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
    if (k.getKeyLen() <= level) { return nullptr; }
    parentNode = node;
    node       = N::getChild(k[level], parentNode);
    parentNode->checkOrRestart(v, needRestart);
    if (needRestart) goto restart;

    if (node == nullptr) { return nullptr; }
    if (N::isLeaf(node)) {
      parentNode->readUnlockOrRestart(v, needRestart);
      if (needRestart) goto restart;

      auto leaf = N::getLeaf(node);
      return ((*leaf) == k) ? leaf : nullptr;
    }
    level++;

    uint64_t nv = node->readLockOrRestart(needRestart);
    if (needRestart) goto restart;

    parentNode->readUnlockOrRestart(v, needRestart);
    if (needRestart) goto restart;
    v = nv;
  }
}

}  // namespace ART
