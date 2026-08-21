#ifndef ART_OPTIMISTIC_LOCK_COUPLING_N_H
#define ART_OPTIMISTIC_LOCK_COUPLING_N_H

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <functional>
#include <iterator>
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
    // A loop and not fill_n. children is an array of std::atomic now, and a
    // bulk fill over one is not something the standard says anything useful
    // about. Relaxed, since the node is not reachable by anyone else until
    // whoever is building it publishes the pointer to it.
    for (auto &child : children) { child.store(nullptr, std::memory_order_relaxed); }
    if (isCodePointEnd) { links[0] = links[1] = nullptr; }
  }

  N256(const N256 &) = delete;
  N256(N256 &&)      = delete;

  std::atomic<uint64_t> versionLock{0b10};  // 63b version | 1b lock
  uint16_t count = 0;                       // up to 256 children, so uint8_t wraps to 0 on a full node

  bool isCodePointEnd = false;

  // Atomic, and this is the whole point of the type rather than decoration.
  // Optimistic lock coupling reads a child pointer without holding anything
  // and validates afterwards by re-reading the version, so a reader's load of
  // children[k] genuinely does overlap a writer's store to the same slot under
  // the write lock. As plain N256* that is a data race, which is undefined
  // behaviour whatever x86 happens to do with it, and thread sanitizer reports
  // it on every concurrent insert.
  //
  // The version protocol is what makes the value correct: a writer takes the
  // lock before storing, so a reader that saw a torn or stale slot also sees a
  // changed version and restarts. The atomics are here to make the access
  // defined, not to add ordering the protocol does not already have, which is
  // why the loads are relaxed and cost nothing.
  std::atomic<N256 *> children[256];
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
    const uint64_t version = versionLock.load();
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
                       uint8_t key, const std::function<N256 *()> &generateVal, bool &needRestart) {
    if (parentNode != nullptr) {
      parentNode->readUnlockOrRestart(parentVersion, needRestart);
      if (needRestart) return;
    }
    upgradeToWriteLockOrRestart(v, needRestart);
    if (needRestart) return;
    // Release, so that everything generateVal wrote into the subtree it just
    // built is visible to anyone who picks this pointer up. writeUnlock below
    // is a read-modify-write and orders it too, but only against readers that
    // go through the version, and Tree::insert reads the child first.
    children[key].store(generateVal(), std::memory_order_release);
    count++;
    writeUnlock();
  }

  // Aho-Corasick: suffix and output links
  auto isTerminalNode() -> bool { return children[NULL_TERMINATOR].load(std::memory_order_relaxed) != nullptr; }

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
    children[key].store(val, std::memory_order_release);
    return true;
  }

  // Relaxed, unlike insertAndUnlock. This one is only called from the
  // generateVal lambda in Tree::insert, on nodes that are still private to the
  // thread building them; the release that publishes the whole chain is the
  // single store in insertAndUnlock.
  void insert(uint8_t key, N256 *val) {
    children[key].store(val, std::memory_order_relaxed);
    count++;
  }

  // Relaxed on purpose. This is the hot path: the probe calls it once per byte
  // of every row, and the probe is single threaded. An acquire here would put
  // an ldar in that loop on arm64 to pay for an ordering only the concurrent
  // insert path needs, so that path takes an explicit acquire fence instead.
  // See Tree::insert.
  N256 *getChild(const uint8_t k) const { return children[k].load(std::memory_order_relaxed); }

  uint64_t getChildren(uint8_t start, uint8_t end, std::tuple<uint8_t, N256 *> *children,
                       uint32_t &childrenCount) const {
  restart:
    bool needRestart = false;
    const uint64_t v = readLockOrRestart(needRestart);
    if (needRestart) goto restart;
    childrenCount = 0;
    for (unsigned i = start; i <= end; i++) {
      auto *child = this->children[i].load(std::memory_order_relaxed);
      if (child != nullptr) children[childrenCount++] = std::make_tuple(i, child);
    }
    readUnlockOrRestart(v, needRestart);
    if (needRestart) goto restart;
    return v;
  }

  static void deleteChildren(N256 *node) {
    if (N256::isLeaf(node)) { return; }
    for (uint64_t i = 0; i < 256; ++i) {
      auto *child = node->children[i].load(std::memory_order_relaxed);
      if (child != nullptr) {
        N256::deleteChildren(child);
        N256::deleteNode(child);
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
