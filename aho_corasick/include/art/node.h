#ifndef ART_OPTIMISTIC_LOCK_COUPLING_N_H
#define ART_OPTIMISTIC_LOCK_COUPLING_N_H

#include <malloc.h>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <utility>

using TupleID                            = uint64_t;
static constexpr uint8_t NULL_TERMINATOR = '\0';

namespace ART {

// forward declaration
class Tree;
struct Leaf;

class N256 {
 protected:
  friend class Tree;

  N256(bool isCodePointEnd) : isCodePointEnd(isCodePointEnd) {
    memset(children, NULL_TERMINATOR, sizeof(children));
    if (isCodePointEnd) { links[0] = links[1] = nullptr; }
  }

  N256(const N256 &) = delete;

  N256(N256 &&) = delete;

  std::atomic<uint64_t> versionLock{0b10};  // 63b version 1b lock
  uint8_t count       = 0;                  // version 1, unlocked
  bool isCodePointEnd = false;              // whether this node is the end of an unicode code point
  N256 *children[256];
  N256 *links[];

 public:
  static auto makeNode(bool isCodePointEnd) -> N256 * {
    auto size = (isCodePointEnd) ? (sizeof(N256) + sizeof(N256 *) * 2) : sizeof(N256);
    return new (operator new(size)) N256(isCodePointEnd);
  }

  uint32_t getCount() const { return count; }

  inline bool isLastByteOfCodePoint() { return isCodePointEnd; }

  // Latch coupling primitives
  bool isLocked(uint64_t version) const { return ((version & 0b1) == 0b1); }

  void writeLockOrRestart(bool &needRestart) {
    uint64_t version;
    version = readLockOrRestart(needRestart);
    if (needRestart) return;

    upgradeToWriteLockOrRestart(version, needRestart);
    if (needRestart) return;
  }

  void upgradeToWriteLockOrRestart(uint64_t &version, bool &needRestart) {
    if (versionLock.compare_exchange_strong(version, version + 0b1)) {
      version = version + 0b1;
    } else {
      needRestart = true;
    }
  }

  void writeUnlock() { versionLock.fetch_add(0b1); }

  uint64_t readLockOrRestart(bool &needRestart) const {
    uint64_t version = versionLock.load();
    if (isLocked(version)) { needRestart = true; }
    return version;
  }

  void readUnlockOrRestart(uint64_t startRead, bool &needRestart) const {
    needRestart = (startRead != versionLock.load());
  }

  void insertAndUnlock(uint64_t v, N256 *parentNode, uint64_t parentVersion, uint8_t keyParent, uint8_t key,
                       std::function<N256 *()> generateVal, bool &needRestart) {
    if (parentNode != nullptr) {
      parentNode->readUnlockOrRestart(parentVersion, needRestart);
      if (needRestart) return;
    }
    upgradeToWriteLockOrRestart(v, needRestart);
    if (needRestart) return;
    children[key] = generateVal();
    count++;
    writeUnlock();
  }

  // Aho-corasick core: Suffix and output link management
  auto isTerminalNode() -> bool { return children[NULL_TERMINATOR] != nullptr; }

  void setSuffixLink(N256 *n) { links[0] = n; }

  N256 *getSuffixLink() const { return links[0]; }

  void setOutputLink(N256 *n) { links[1] = n; }

  N256 *getOutputLink() const { return links[1]; }

  // Leaf operators
  static Leaf *getLeaf(const N256 *n) {
    return reinterpret_cast<Leaf *>(reinterpret_cast<uintptr_t>(n) & ((static_cast<uint64_t>(1) << 63) - 1));
  }

  static bool isLeaf(const N256 *n) {
    return (reinterpret_cast<uint64_t>(n) & (static_cast<uint64_t>(1) << 63)) == (static_cast<uint64_t>(1) << 63);
  }

  static N256 *setLeaf(Leaf *leaf) {
    return reinterpret_cast<N256 *>(reinterpret_cast<uintptr_t>(leaf) | (static_cast<uint64_t>(1) << 63));
  }

  // Child operators
  bool change(uint8_t key, N256 *val) {
    children[key] = val;
    return true;
  }

  void insert(uint8_t key, N256 *val) {
    children[key] = val;
    count++;
  }

  N256 *getChild(const uint8_t k) const { return children[k]; }

  uint64_t getChildren(uint8_t start, uint8_t end, std::tuple<uint8_t, N256 *> *children,
                       uint32_t &childrenCount) const {
  restart:
    bool needRestart = false;
    uint64_t v;
    v = readLockOrRestart(needRestart);
    if (needRestart) goto restart;
    childrenCount = 0;
    for (unsigned i = start; i <= end; i++) {
      if (this->children[i] != nullptr) {
        children[childrenCount] = std::make_tuple(i, this->children[i]);
        childrenCount++;
      }
    }
    readUnlockOrRestart(v, needRestart);
    if (needRestart) goto restart;
    return v;
  }

  static void deleteChildren(N256 *node) {
    if (N256::isLeaf(node)) { return; }
    for (uint64_t i = 0; i < 256; ++i) {
      if (node->children[i] != nullptr) {
        N256::deleteChildren(node->children[i]);
        N256::deleteNode(node->children[i]);
      }
    }
  }

  static void deleteNode(N256 *node) {
    if (N256::isLeaf(node)) { return; }
    auto n = static_cast<N256 *>(node);
    operator delete(n);
  }
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
  inline auto keyLenWithoutNullTerminator() const { return keyLen - 1; }

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

}  // namespace ART

#endif  // ART_OPTIMISTIC_LOCK_COUPLING_N_H
