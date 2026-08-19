#ifndef ART_OPTIMISTIC_LOCK_COUPLING_N_H
#define ART_OPTIMISTIC_LOCK_COUPLING_N_H

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <utility>

using TupleID                            = uint64_t;
static constexpr uint8_t NULL_TERMINATOR = '\0';

namespace ART {

class Tree;

// Leaf only stores auxIndex; key bytes are fully encoded in the trie path.
// literal_len lives in AhoCorasick::literal_map_ alongside the bitmap.
// Defined before N256 because N256::deleteNode has to delete one.
struct Leaf {
  uint64_t auxIndex;

  static Leaf *MakeLeaf(uint64_t auxIndex) { return new Leaf(auxIndex); }

 private:
  explicit Leaf(uint64_t auxIndex) : auxIndex(auxIndex) {}
};

class N256 {
 protected:
  friend class Tree;

  N256(bool isCodePointEnd) : isCodePointEnd(isCodePointEnd) {
    memset(children, NULL_TERMINATOR, sizeof(children));
    if (isCodePointEnd) { links[0] = links[1] = nullptr; }
  }

  N256(const N256 &) = delete;
  N256(N256 &&)      = delete;

  std::atomic<uint64_t> versionLock{0b10};  // 63b version | 1b lock
  uint16_t count = 0;                       // up to 256 children, so uint8_t wraps to 0 on a full node

  bool isCodePointEnd = false;
  N256 *children[256];
  N256 *links[];

 public:
  static auto makeNode(bool isCodePointEnd) -> N256 * {
    auto size = isCodePointEnd ? (sizeof(N256) + sizeof(N256 *) * 2) : sizeof(N256);
    return new (operator new(size)) N256(isCodePointEnd);
  }

  auto getCount() { return count; }

  inline bool isLastByteOfCodePoint() { return isCodePointEnd; }

  // Optimistic lock coupling
  bool isLocked(uint64_t version) const { return (version & 0b1) == 0b1; }

  void upgradeToWriteLockOrRestart(uint64_t &version, bool &needRestart) {
    if (versionLock.compare_exchange_strong(version, version + 0b1))
      version = version + 0b1;
    else
      needRestart = true;
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

  // keyParent is unused. In a full ART it is the byte the parent uses to
  // point at this node, needed when an insert grows the node and the parent
  // pointer has to be rewritten. This trie is N256 only and never grows, so
  // nothing reads it. Kept in the signature rather than deleted, since a node
  // type that does grow would want it back.
  void insertAndUnlock(uint64_t v, N256 *parentNode, uint64_t parentVersion, [[maybe_unused]] uint8_t keyParent,
                       uint8_t key, std::function<N256 *()> generateVal, bool &needRestart) {
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

  // Aho-Corasick: suffix and output links
  auto isTerminalNode() -> bool { return children[NULL_TERMINATOR] != nullptr; }

  void setSuffixLink(N256 *n) { links[0] = n; }

  N256 *getSuffixLink() const { return links[0]; }

  void setOutputLink(N256 *n) { links[1] = n; }

  N256 *getOutputLink() const { return links[1]; }

  // Leaf tagging (high bit of pointer)
  static Leaf *getLeaf(const N256 *n) {
    return reinterpret_cast<Leaf *>(reinterpret_cast<uintptr_t>(n) & ((static_cast<uint64_t>(1) << 63) - 1));
  }

  static bool isLeaf(const N256 *n) { return (reinterpret_cast<uint64_t>(n) >> 63) == 1; }

  static N256 *setLeaf(Leaf *leaf) {
    return reinterpret_cast<N256 *>(reinterpret_cast<uintptr_t>(leaf) | (static_cast<uint64_t>(1) << 63));
  }

  // Child access
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
    uint64_t v       = readLockOrRestart(needRestart);
    if (needRestart) goto restart;
    childrenCount = 0;
    for (unsigned i = start; i <= end; i++) {
      if (this->children[i] != nullptr) children[childrenCount++] = std::make_tuple(i, this->children[i]);
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
    if (N256::isLeaf(node)) {
      delete N256::getLeaf(node);
      return;
    }
    operator delete(static_cast<N256 *>(node));
  }
};

}  // namespace ART

#endif  // ART_OPTIMISTIC_LOCK_COUPLING_N_H
