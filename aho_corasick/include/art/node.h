//
// Created by florian on 05.08.15.
//

#ifndef ART_OPTIMISTIC_LOCK_COUPLING_N_H
#define ART_OPTIMISTIC_LOCK_COUPLING_N_H

// #define ART_NOREADLOCK
// #define ART_NOWRITELOCK

#include <malloc.h>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <utility>

#include "art/epoche.h"

using TupleID                            = uint64_t;
static constexpr uint8_t NULL_TERMINATOR = '\0';

#define DEFINE_NODE_LINK_FUNCTIONS(CLASS)       \
  void setSuffixLink(N *n) { links[0] = n; }    \
  N *getSuffixLink() const { return links[0]; } \
  void setOutputLink(N *n) { links[1] = n; }    \
  N *getOutputLink() const { return links[1]; }

namespace ART {

// forward declaration
class Tree;
struct Leaf;

enum class NTypes : uint8_t { N4 = 0, N16 = 1, N48 = 2, N256 = 3 };

class N {
 protected:
  friend class Tree;

  N(NTypes type, bool isCodePointEnd) : isCodePointEnd(isCodePointEnd) { setType(type); }

  N(const N &) = delete;

  N(N &&) = delete;

  // 2b type 60b version 1b lock 1b obsolete
  std::atomic<uint64_t> typeVersionLockObsolete{0b100};
  // version 1, unlocked, not obsolete
  uint8_t count = 0;
  // whether this node is the end of an unicode code point
  bool isCodePointEnd = false;

  void setType(NTypes type);

  static uint64_t convertTypeToVersion(NTypes type);

 public:
  NTypes getType() const;

  uint32_t getCount() const;

  bool isLocked(uint64_t version) const;

  void writeLockOrRestart(bool &needRestart);

  void upgradeToWriteLockOrRestart(uint64_t &version, bool &needRestart);

  void writeUnlock();

  uint64_t readLockOrRestart(bool &needRestart) const;

  /**
   * returns true if node hasn't been changed in between
   */
  void checkOrRestart(uint64_t startRead, bool &needRestart) const;
  void readUnlockOrRestart(uint64_t startRead, bool &needRestart) const;

  static bool isObsolete(uint64_t version);

  /**
   * can only be called when node is locked
   */
  void writeUnlockObsolete() { typeVersionLockObsolete.fetch_add(0b11); }

  /**
   * Aho-corasick core: Suffix and output link management
   */
  static void setSuffixLink(N *link, N *n);
  static auto getSuffixLink(const N *n) -> N *;
  static void setOutputLink(N *link, N *n);
  static auto getOutputLink(const N *n) -> N *;
  auto isTerminalNode() -> bool;

  // Leaf operators
  static Leaf *getLeaf(const N *n);
  static bool isLeaf(const N *n);
  static N *setLeaf(Leaf *leaf);

  static N *getChild(const uint8_t k, const N *node);

  static void insertAndUnlock(N *node, uint64_t v, N *parentNode, uint64_t parentVersion, uint8_t keyParent,
                              uint8_t key, std::function<N *()> generateVal, bool &needRestart, ThreadInfo &threadInfo);

  static bool change(N *node, uint8_t key, N *val);

  static void removeAndUnlock(N *node, uint64_t v, uint8_t key, N *parentNode, uint64_t parentVersion,
                              uint8_t keyParent, bool &needRestart, ThreadInfo &threadInfo);

  static N *getAnyChild(const N *n);

  static void deleteChildren(N *node);

  static void deleteNode(N *node);

  static std::tuple<N *, uint8_t> getSecondChild(N *node, const uint8_t k);

  template <typename curN, typename biggerN>
  static void insertGrow(curN *n, uint64_t v, N *parentNode, uint64_t parentVersion, uint8_t keyParent, uint8_t key,
                         std::function<N *()> generateVal, bool &needRestart, ThreadInfo &threadInfo) {
    if (!n->isFull()) {
      if (parentNode != nullptr) {
        parentNode->readUnlockOrRestart(parentVersion, needRestart);
        if (needRestart) return;
      }
      n->upgradeToWriteLockOrRestart(v, needRestart);
      if (needRestart) return;
      n->insert(key, generateVal());
      n->writeUnlock();
      return;
    }

    parentNode->upgradeToWriteLockOrRestart(parentVersion, needRestart);
    if (needRestart) return;

    n->upgradeToWriteLockOrRestart(v, needRestart);
    if (needRestart) {
      parentNode->writeUnlock();
      return;
    }

    auto nBig = biggerN::makeNode(n->isCodePointEnd);
    n->copyTo(nBig);
    nBig->insert(key, generateVal());

    N::change(parentNode, keyParent, nBig);

    n->writeUnlockObsolete();
    threadInfo.getEpoche().markNodeForDeletion(n, threadInfo);
    parentNode->writeUnlock();
  }

  template <typename curN, typename smallerN>
  static void removeAndShrink(curN *n, uint64_t v, N *parentNode, uint64_t parentVersion, uint8_t keyParent,
                              uint8_t key, bool &needRestart, ThreadInfo &threadInfo) {
    if (!n->isUnderfull() || parentNode == nullptr) {
      if (parentNode != nullptr) {
        parentNode->readUnlockOrRestart(parentVersion, needRestart);
        if (needRestart) return;
      }
      n->upgradeToWriteLockOrRestart(v, needRestart);
      if (needRestart) return;

      n->remove(key);
      n->writeUnlock();
      return;
    }
    parentNode->upgradeToWriteLockOrRestart(parentVersion, needRestart);
    if (needRestart) return;

    n->upgradeToWriteLockOrRestart(v, needRestart);
    if (needRestart) {
      parentNode->writeUnlock();
      return;
    }

    auto nSmall = smallerN::makeNode(n->isCodePointEnd);
    n->copyTo(nSmall);
    nSmall->remove(key);
    N::change(parentNode, keyParent, nSmall);

    n->writeUnlockObsolete();
    threadInfo.getEpoche().markNodeForDeletion(n, threadInfo);
    parentNode->writeUnlock();
  }

  static uint64_t getChildren(const N *node, uint8_t start, uint8_t end, std::tuple<uint8_t, N *> children[],
                              uint32_t &childrenCount);
};

struct Leaf {
  uint64_t auxIndex;
  uint32_t keyLen;
  uint8_t key[];

  static auto MakeLeaf(const uint8_t *originalKey, uint32_t keyLen, bool mustAppendNull, uint64_t auxIndex) {
    void *mem = operator new(sizeof(Leaf) + keyLen + mustAppendNull);
    auto leaf = new (mem) Leaf(originalKey, keyLen, auxIndex);
    if (mustAppendNull) {
      leaf->key[keyLen] = NULL_TERMINATOR;
      leaf->keyLen++;
    }
    return leaf;
  }

  // key stored in Leaf always contain '\0'
  inline auto keyLenWithoutNullTerminator() { return keyLen - 1; }

  inline const uint8_t &operator[](std::size_t i) const {
    assert(i < keyLen);
    return key[i];
  }

  template <typename byte_t>
  inline bool equal(const byte_t *keyword, uint32_t keywordSize, bool requiresNullTerminated) const {
    assert(keyLen > 0 && key[keyLen - 1] == NULL_TERMINATOR);
    if (keywordSize + requiresNullTerminated != keyLen) { return false; }
    return std::memcmp(keyword, key, keyLen - requiresNullTerminated) == 0;
  }

 private:
  Leaf(const uint8_t *originalKey, uint32_t keyLen, uint64_t auxIndex) : auxIndex(auxIndex), keyLen(keyLen) {
    memcpy(this->key, originalKey, keyLen);
  }
};

class N4 : public N {
 public:
  uint8_t keys[4];
  N *children[4] = {nullptr, nullptr, nullptr, nullptr};
  N *links[];

  N4(bool isCodePointEnd) : N(NTypes::N4, isCodePointEnd) {
    if (isCodePointEnd) { links[0] = links[1] = nullptr; }
  }

 public:
  DEFINE_NODE_LINK_FUNCTIONS(N4);

  static auto makeNode(bool isCodePointEnd) -> N4 * {
    auto size   = (isCodePointEnd) ? (sizeof(N4) + sizeof(N *) * 2) : sizeof(N4);
    auto buffer = operator new(size);
    return new (operator new(size)) N4(isCodePointEnd);
  }

  void insert(uint8_t key, N *n);

  template <class NODE>
  void copyTo(NODE *n) const {
    for (uint32_t i = 0; i < count; ++i) { n->insert(keys[i], children[i]); }
  }

  bool change(uint8_t key, N *val);

  N *getChild(const uint8_t k) const;

  void remove(uint8_t k);

  N *getAnyChild() const;

  bool isFull() const;

  bool isUnderfull() const;

  std::tuple<N *, uint8_t> getSecondChild(const uint8_t key) const;

  void deleteChildren();

  uint64_t getChildren(uint8_t start, uint8_t end, std::tuple<uint8_t, N *> *&children, uint32_t &childrenCount) const;
};

class N16 : public N {
 public:
  uint8_t keys[16];
  N *children[16];
  N *links[];

  static uint8_t flipSign(uint8_t keyByte) {
    // Flip the sign bit, enables signed SSE comparison of unsigned values, used by Node16
    return keyByte ^ 128;
  }

  static inline unsigned ctz(uint16_t x) {
    // Count trailing zeros, only defined for x>0
#ifdef __GNUC__
    return __builtin_ctz(x);
#else
    // Adapted from Hacker's Delight
    unsigned n = 1;
    if ((x & 0xFF) == 0) {
      n += 8;
      x = x >> 8;
    }
    if ((x & 0x0F) == 0) {
      n += 4;
      x = x >> 4;
    }
    if ((x & 0x03) == 0) {
      n += 2;
      x = x >> 2;
    }
    return n - (x & 1);
#endif
  }

  N *const *getChildPos(const uint8_t k) const;

  N16(bool isCodePointEnd) : N(NTypes::N16, isCodePointEnd) {
    memset(keys, 0, sizeof(keys));
    memset(children, 0, sizeof(children));
    if (isCodePointEnd) { links[0] = links[1] = nullptr; }
  }

 public:
  DEFINE_NODE_LINK_FUNCTIONS(N16);

  static auto makeNode(bool isCodePointEnd) -> N16 * {
    auto size = (isCodePointEnd) ? (sizeof(N16) + sizeof(N *) * 2) : sizeof(N16);
    return new (operator new(size)) N16(isCodePointEnd);
  }

  void insert(uint8_t key, N *n);

  template <class NODE>
  void copyTo(NODE *n) const {
    for (unsigned i = 0; i < count; i++) { n->insert(flipSign(keys[i]), children[i]); }
  }

  bool change(uint8_t key, N *val);

  N *getChild(const uint8_t k) const;

  void remove(uint8_t k);

  N *getAnyChild() const;

  bool isFull() const;

  bool isUnderfull() const;

  void deleteChildren();

  uint64_t getChildren(uint8_t start, uint8_t end, std::tuple<uint8_t, N *> *&children, uint32_t &childrenCount) const;
};

class N48 : public N {
  uint8_t childIndex[256];
  N *children[48];
  N *links[];

  N48(bool isCodePointEnd) : N(NTypes::N48, isCodePointEnd) {
    memset(childIndex, emptyMarker, sizeof(childIndex));
    memset(children, 0, sizeof(children));
    if (isCodePointEnd) { links[0] = links[1] = nullptr; }
  }

 public:
  static const uint8_t emptyMarker = 48;

  DEFINE_NODE_LINK_FUNCTIONS(N48);

  static auto makeNode(bool isCodePointEnd) -> N48 * {
    auto size = (isCodePointEnd) ? (sizeof(N48) + sizeof(N *) * 2) : sizeof(N48);
    return new (operator new(size)) N48(isCodePointEnd);
  }

  void insert(uint8_t key, N *n);

  template <class NODE>
  void copyTo(NODE *n) const {
    for (unsigned i = 0; i < 256; i++) {
      if (childIndex[i] != emptyMarker) { n->insert(i, children[childIndex[i]]); }
    }
  }

  bool change(uint8_t key, N *val);

  N *getChild(const uint8_t k) const;

  void remove(uint8_t k);

  N *getAnyChild() const;

  bool isFull() const;

  bool isUnderfull() const;

  void deleteChildren();

  uint64_t getChildren(uint8_t start, uint8_t end, std::tuple<uint8_t, N *> *&children, uint32_t &childrenCount) const;
};

class N256 : public N {
  N *children[256];
  N *links[];

  N256(bool isCodePointEnd) : N(NTypes::N256, isCodePointEnd) {
    memset(children, NULL_TERMINATOR, sizeof(children));
    if (isCodePointEnd) { links[0] = links[1] = nullptr; }
  }

 public:
  DEFINE_NODE_LINK_FUNCTIONS(N256);

  static auto makeNode(bool isCodePointEnd) -> N256 * {
    auto size = (isCodePointEnd) ? (sizeof(N256) + sizeof(N *) * 2) : sizeof(N256);
    return new (operator new(size)) N256(isCodePointEnd);
  }

  void insert(uint8_t key, N *val);

  template <class NODE>
  void copyTo(NODE *n) const {
    for (int i = 0; i < 256; ++i) {
      if (children[i] != nullptr) { n->insert(i, children[i]); }
    }
  }

  bool change(uint8_t key, N *n);

  N *getChild(const uint8_t k) const;

  void remove(uint8_t k);

  N *getAnyChild() const;

  bool isFull() const;

  bool isUnderfull() const;

  void deleteChildren();

  uint64_t getChildren(uint8_t start, uint8_t end, std::tuple<uint8_t, N *> *&children, uint32_t &childrenCount) const;
};
}  // namespace ART
#endif  // ART_OPTIMISTIC_LOCK_COUPLING_N_H
