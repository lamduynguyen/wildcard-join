#include "aho_corasick/aho_corasick.h"

#include <cassert>
#include <cstring>
#include <queue>

namespace aho_corasick {

std::atomic<u64> AhoCorasick::NUMBER_OF_UNIQUE_LITERALS = 0;

AhoCorasick::AhoCorasick() : trie_(std::make_unique<ART::Tree>()) {}

auto AhoCorasick::GetRoot() const -> ART::N256 * { return trie_->root; }

void AhoCorasick::Insert(const char *keyword, uint64_t keyword_len, const PatternIndexType &keyword_auxIndex) {
  assert(keyword_len > 0);
  bool must_append_null = (keyword[keyword_len - 1] != NULL_TERMINATOR);
  auto new_tid          = [&]() {
    auto new_tid    = NUMBER_OF_UNIQUE_LITERALS.fetch_add(1, std::memory_order_relaxed);
    auto new_bitmap = roaring::Roaring64Map();
    new_bitmap.add(keyword_auxIndex.ToUint());
    literal_map_.emplace(new_tid, new_bitmap);
    return new_tid;
  };
  auto upsert = [this, keyword_auxIndex](TupleID tid) {
    assert(literal_map_.contains(tid));
    literal_map_[tid].add(keyword_auxIndex.ToUint());
  };
  trie_->insert(keyword, keyword_len, must_append_null, new_tid, upsert);
}

void AhoCorasick::BuildSuffixLink() {
  // Single-threaded BFS for constructing suffix and output links
  std::queue<BFSNodeItem> bfs_queue;
  bfs_queue.push(BFSNodeItem{trie_->root, trie_->root, {}, 0});

  while (!bfs_queue.empty()) {
    BFSNodeItem item = bfs_queue.front();
    bfs_queue.pop();

    // Get children of current node
    std::tuple<uint8_t, ART::N256 *> children[256];
    uint32_t children_cnt = 0;
    item.node->getChildren(0, 255, children, children_cnt);

    // --- Handle root node ---
    if (item.node == trie_->root) {
      assert(item.node->isLastByteOfCodePoint());

      for (uint32_t i = 0; i < children_cnt; ++i) {
        const uint8_t byte = std::get<0>(children[i]);
        auto child_node    = std::get<1>(children[i]);
        if (ART::N256::isLeaf(child_node)) { continue; }

        if (child_node->isLastByteOfCodePoint()) {
          // Child is end of code point => suffix link points to root
          child_node->setSuffixLink(trie_->root);
          bfs_queue.push(BFSNodeItem{child_node, child_node, {}, 0});
        } else {
          // Child is intermediate byte => start new partial sequence
          std::array<uint8_t, 6> partial_bytes{};
          partial_bytes[0] = byte;
          bfs_queue.push(BFSNodeItem{child_node, item.node, partial_bytes, 1});
        }
      }
      continue;
    }

    // --- Handle non-root node ---
    for (uint32_t i = 0; i < children_cnt; ++i) {
      const uint8_t byte = std::get<0>(children[i]);
      auto child_node    = std::get<1>(children[i]);

      // #1. A leaf node that contain literal here, continue
      if (byte == NULL_TERMINATOR) {
        assert(ART::N256::isLeaf(child_node));
        continue;
      }

      // #2. Intermediary bytes of a code point, do not evaluate AhoCorasick suffix and output links
      if (!child_node->isLastByteOfCodePoint()) {
        // Intermediate byte => extend the partial sequence
        std::array<uint8_t, 6> new_bytes = item.bytes_since_last_cp;
        assert(item.length < 6);  // Historic UTF-8 safety
        new_bytes[item.length] = byte;

        bfs_queue.push(
          BFSNodeItem{child_node, item.last_codepoint_node, new_bytes, static_cast<uint8_t>(item.length + 1)});
        continue;
      }

      // #3. Child is last byte of a code point => compute suffix link
      assert(child_node->isLastByteOfCodePoint());
      auto suffix_node = item.last_codepoint_node->getSuffixLink();

      while (suffix_node) {
        auto possible_suffix = suffix_node;
        assert(possible_suffix->isLastByteOfCodePoint());
        bool match = true;
        // Walk the partial byte sequence to find suffix
        for (uint8_t idx = 0; idx < item.length; ++idx) {
          auto next = possible_suffix->getChild(item.bytes_since_last_cp[idx]);
          if (!next) {
            match = false;
            break;
          }
          possible_suffix = next;
        }
        if (match) {
          auto next = possible_suffix->getChild(byte);
          if (next) {
            assert(next->isLastByteOfCodePoint());
            suffix_node = next;
            break;
          }
        }
        suffix_node = suffix_node->getSuffixLink();
      }
      if (!suffix_node) { suffix_node = trie_->root; }
      assert(suffix_node->isLastByteOfCodePoint());

      // Set suffix and output links
      child_node->setSuffixLink(suffix_node);
      child_node->setOutputLink(suffix_node->isTerminalNode() ? suffix_node : suffix_node->getOutputLink());
      assert(!child_node->getOutputLink() || child_node->getOutputLink()->isTerminalNode());

      // Reset intermediate bytes for next BFS
      bfs_queue.push(BFSNodeItem{child_node, child_node, {}, 0});
    }
  }
}

auto AhoCorasick::VisitCodePoint(ART::N256 *cur, const char *cp, u8 cp_len) -> ART::N256 * {
  assert(cur->isLastByteOfCodePoint());
  for (auto idx = 0U; idx < cp_len; idx++) {
    auto next = cur->getChild(cp[idx]);
    if (next == nullptr) { return nullptr; }
    if (idx > 0) { assert(!cur->isLastByteOfCodePoint()); }
    cur = next;
  }
  return cur;
}

}  // namespace aho_corasick