#include "art/node.h"

#include <algorithm>
#include <cassert>

namespace ART {

void N::setType(NTypes type) { typeVersionLockObsolete.fetch_add(convertTypeToVersion(type)); }

uint64_t N::convertTypeToVersion(NTypes type) { return (static_cast<uint64_t>(type) << 62); }

NTypes N::getType() const { return static_cast<NTypes>(typeVersionLockObsolete.load(std::memory_order_relaxed) >> 62); }

uint32_t N::getCount() const { return count; }

void N::writeLockOrRestart(bool &needRestart) {
  uint64_t version;
  version = readLockOrRestart(needRestart);
  if (needRestart) return;

  upgradeToWriteLockOrRestart(version, needRestart);
  if (needRestart) return;
}

void N::upgradeToWriteLockOrRestart(uint64_t &version, bool &needRestart) {
  if (typeVersionLockObsolete.compare_exchange_strong(version, version + 0b10)) {
    version = version + 0b10;
  } else {
    needRestart = true;
  }
}

void N::writeUnlock() { typeVersionLockObsolete.fetch_add(0b10); }

N *N::getAnyChild(const N *node) {
  switch (node->getType()) {
    case NTypes::N4: {
      auto n = static_cast<const N4 *>(node);
      return n->getAnyChild();
    }
    case NTypes::N16: {
      auto n = static_cast<const N16 *>(node);
      return n->getAnyChild();
    }
    case NTypes::N48: {
      auto n = static_cast<const N48 *>(node);
      return n->getAnyChild();
    }
    case NTypes::N256: {
      auto n = static_cast<const N256 *>(node);
      return n->getAnyChild();
    }
  }
  assert(false);
  __builtin_unreachable();
}

bool N::change(N *node, uint8_t key, N *val) {
  switch (node->getType()) {
    case NTypes::N4: {
      auto n = static_cast<N4 *>(node);
      return n->change(key, val);
    }
    case NTypes::N16: {
      auto n = static_cast<N16 *>(node);
      return n->change(key, val);
    }
    case NTypes::N48: {
      auto n = static_cast<N48 *>(node);
      return n->change(key, val);
    }
    case NTypes::N256: {
      auto n = static_cast<N256 *>(node);
      return n->change(key, val);
    }
  }
  assert(false);
  __builtin_unreachable();
}

void N::insertAndUnlock(N *node, uint64_t v, N *parentNode, uint64_t parentVersion, uint8_t keyParent, uint8_t key,
                        std::function<N *()> generateVal, bool &needRestart, ThreadInfo &threadInfo) {
  switch (node->getType()) {
    case NTypes::N4: {
      auto n = static_cast<N4 *>(node);
      insertGrow<N4, N16>(n, v, parentNode, parentVersion, keyParent, key, generateVal, needRestart, threadInfo);
      break;
    }
    case NTypes::N16: {
      auto n = static_cast<N16 *>(node);
      insertGrow<N16, N48>(n, v, parentNode, parentVersion, keyParent, key, generateVal, needRestart, threadInfo);
      break;
    }
    case NTypes::N48: {
      auto n = static_cast<N48 *>(node);
      insertGrow<N48, N256>(n, v, parentNode, parentVersion, keyParent, key, generateVal, needRestart, threadInfo);
      break;
    }
    case NTypes::N256: {
      auto n = static_cast<N256 *>(node);
      insertGrow<N256, N256>(n, v, parentNode, parentVersion, keyParent, key, generateVal, needRestart, threadInfo);
      break;
    }
  }
}

void N::setSuffixLink(N *link, N *node) {
  assert(node->isCodePointEnd);
  switch (node->getType()) {
    case NTypes::N4: {
      auto n = static_cast<N4 *>(node);
      return n->setSuffixLink(link);
    }
    case NTypes::N16: {
      auto n = static_cast<N16 *>(node);
      return n->setSuffixLink(link);
    }
    case NTypes::N48: {
      auto n = static_cast<N48 *>(node);
      return n->setSuffixLink(link);
    }
    case NTypes::N256: {
      auto n = static_cast<N256 *>(node);
      return n->setSuffixLink(link);
    }
  }
  assert(false);
  __builtin_unreachable();
}

auto N::getSuffixLink(const N *node) -> N * {
  assert(node->isCodePointEnd);
  switch (node->getType()) {
    case NTypes::N4: {
      auto n = static_cast<const N4 *>(node);
      return n->getSuffixLink();
    }
    case NTypes::N16: {
      auto n = static_cast<const N16 *>(node);
      return n->getSuffixLink();
    }
    case NTypes::N48: {
      auto n = static_cast<const N48 *>(node);
      return n->getSuffixLink();
    }
    case NTypes::N256: {
      auto n = static_cast<const N256 *>(node);
      return n->getSuffixLink();
    }
  }
  assert(false);
  __builtin_unreachable();
}

void N::setOutputLink(N *link, N *node) {
  assert(node->isCodePointEnd);
  switch (node->getType()) {
    case NTypes::N4: {
      auto n = static_cast<N4 *>(node);
      return n->setOutputLink(link);
    }
    case NTypes::N16: {
      auto n = static_cast<N16 *>(node);
      return n->setOutputLink(link);
    }
    case NTypes::N48: {
      auto n = static_cast<N48 *>(node);
      return n->setOutputLink(link);
    }
    case NTypes::N256: {
      auto n = static_cast<N256 *>(node);
      return n->setOutputLink(link);
    }
  }
  assert(false);
  __builtin_unreachable();
}

auto N::getOutputLink(const N *node) -> N * {
  assert(node->isCodePointEnd);
  switch (node->getType()) {
    case NTypes::N4: {
      auto n = static_cast<const N4 *>(node);
      return n->getOutputLink();
    }
    case NTypes::N16: {
      auto n = static_cast<const N16 *>(node);
      return n->getOutputLink();
    }
    case NTypes::N48: {
      auto n = static_cast<const N48 *>(node);
      return n->getOutputLink();
    }
    case NTypes::N256: {
      auto n = static_cast<const N256 *>(node);
      return n->getOutputLink();
    }
  }
  assert(false);
  __builtin_unreachable();
}

// A node is a terminal node only if it has a leaf child
auto N::isTerminalNode() -> bool { return getChild(NULL_TERMINATOR, this) != nullptr; }

N *N::getChild(const uint8_t k, const N *node) {
  switch (node->getType()) {
    case NTypes::N4: {
      auto n = static_cast<const N4 *>(node);
      return n->getChild(k);
    }
    case NTypes::N16: {
      auto n = static_cast<const N16 *>(node);
      return n->getChild(k);
    }
    case NTypes::N48: {
      auto n = static_cast<const N48 *>(node);
      return n->getChild(k);
    }
    case NTypes::N256: {
      auto n = static_cast<const N256 *>(node);
      return n->getChild(k);
    }
  }
  assert(false);
  __builtin_unreachable();
}

void N::deleteChildren(N *node) {
  if (N::isLeaf(node)) { return; }
  switch (node->getType()) {
    case NTypes::N4: {
      auto n = static_cast<N4 *>(node);
      n->deleteChildren();
      return;
    }
    case NTypes::N16: {
      auto n = static_cast<N16 *>(node);
      n->deleteChildren();
      return;
    }
    case NTypes::N48: {
      auto n = static_cast<N48 *>(node);
      n->deleteChildren();
      return;
    }
    case NTypes::N256: {
      auto n = static_cast<N256 *>(node);
      n->deleteChildren();
      return;
    }
  }
  assert(false);
  __builtin_unreachable();
}

void N::removeAndUnlock(N *node, uint64_t v, uint8_t key, N *parentNode, uint64_t parentVersion, uint8_t keyParent,
                        bool &needRestart, ThreadInfo &threadInfo) {
  switch (node->getType()) {
    case NTypes::N4: {
      auto n = static_cast<N4 *>(node);
      removeAndShrink<N4, N4>(n, v, parentNode, parentVersion, keyParent, key, needRestart, threadInfo);
      break;
    }
    case NTypes::N16: {
      auto n = static_cast<N16 *>(node);
      removeAndShrink<N16, N4>(n, v, parentNode, parentVersion, keyParent, key, needRestart, threadInfo);
      break;
    }
    case NTypes::N48: {
      auto n = static_cast<N48 *>(node);
      removeAndShrink<N48, N16>(n, v, parentNode, parentVersion, keyParent, key, needRestart, threadInfo);
      break;
    }
    case NTypes::N256: {
      auto n = static_cast<N256 *>(node);
      removeAndShrink<N256, N48>(n, v, parentNode, parentVersion, keyParent, key, needRestart, threadInfo);
      break;
    }
  }
}

bool N::isLocked(uint64_t version) const { return ((version & 0b10) == 0b10); }

uint64_t N::readLockOrRestart(bool &needRestart) const {
  uint64_t version;
  version = typeVersionLockObsolete.load();
  if (isLocked(version) || isObsolete(version)) { needRestart = true; }
  return version;
}

bool N::isObsolete(uint64_t version) { return (version & 1) == 1; }

void N::checkOrRestart(uint64_t startRead, bool &needRestart) const { readUnlockOrRestart(startRead, needRestart); }

void N::readUnlockOrRestart(uint64_t startRead, bool &needRestart) const {
  needRestart = (startRead != typeVersionLockObsolete.load());
}

bool N::isLeaf(const N *n) {
  return (reinterpret_cast<uint64_t>(n) & (static_cast<uint64_t>(1) << 63)) == (static_cast<uint64_t>(1) << 63);
}

N *N::setLeaf(Leaf *leaf) {
  return reinterpret_cast<N *>(reinterpret_cast<uintptr_t>(leaf) | (static_cast<uint64_t>(1) << 63));
}

Leaf *N::getLeaf(const N *n) {
  return reinterpret_cast<Leaf *>(reinterpret_cast<uintptr_t>(n) & ((static_cast<uint64_t>(1) << 63) - 1));
}

std::tuple<N *, uint8_t> N::getSecondChild(N *node, const uint8_t key) {
  switch (node->getType()) {
    case NTypes::N4: {
      auto n = static_cast<N4 *>(node);
      return n->getSecondChild(key);
    }
    default: {
      assert(false);
      __builtin_unreachable();
    }
  }
}

void N::deleteNode(N *node) {
  if (N::isLeaf(node)) { return; }
  switch (node->getType()) {
    case NTypes::N4: {
      auto n = static_cast<N4 *>(node);
      operator delete(n);
      return;
    }
    case NTypes::N16: {
      auto n = static_cast<N16 *>(node);
      operator delete(n);
      return;
    }
    case NTypes::N48: {
      auto n = static_cast<N48 *>(node);
      operator delete(n);
      return;
    }
    case NTypes::N256: {
      auto n = static_cast<N256 *>(node);
      operator delete(n);
      return;
    }
  }
  __builtin_unreachable();
}

uint64_t N::getChildren(const N *node, uint8_t start, uint8_t end, std::tuple<uint8_t, N *> children[],
                        uint32_t &childrenCount) {
  switch (node->getType()) {
    case NTypes::N4: {
      auto n = static_cast<const N4 *>(node);
      return n->getChildren(start, end, children, childrenCount);
    }
    case NTypes::N16: {
      auto n = static_cast<const N16 *>(node);
      return n->getChildren(start, end, children, childrenCount);
    }
    case NTypes::N48: {
      auto n = static_cast<const N48 *>(node);
      return n->getChildren(start, end, children, childrenCount);
    }
    case NTypes::N256: {
      auto n = static_cast<const N256 *>(node);
      return n->getChildren(start, end, children, childrenCount);
    }
  }
  assert(false);
  __builtin_unreachable();
}
}  // namespace ART