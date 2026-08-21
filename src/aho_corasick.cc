#include "aho_corasick/aho_corasick.h"

#include <cassert>
#include <cstring>
#include <queue>

namespace aho_corasick {

AhoCorasick::AhoCorasick() : trie_(std::make_unique<ART::Tree>()), unique_literal_cnt_(0) {}

auto AhoCorasick::GetRoot() const -> ART::N256 * { return trie_->root; }

void AhoCorasick::Insert(const char *keyword, uint64_t keyword_len, const PatternIndexType &keyword_auxIndex) {
  assert(keyword_len > 0);
  const bool must_append_null = (keyword[keyword_len - 1] != NULL_TERMINATOR);
  const u32 literal_len       = static_cast<u32>(keyword_len);  // excludes the null terminator appended by the trie

  auto new_tid = [&]() {
    auto tid = unique_literal_cnt_.fetch_add(1, std::memory_order_relaxed);
    LiteralEntry entry(literal_len);
    entry.bitmap.add(keyword_auxIndex.ToUint());
    literal_map_.emplace(tid, std::move(entry));
    return tid;
  };
  auto upsert = [this, keyword_auxIndex](TupleID tid) {
    assert(literal_map_.contains(tid));
    literal_map_.at(tid).bitmap.add(keyword_auxIndex.ToUint());
  };
  trie_->insert(keyword, keyword_len, must_append_null, new_tid, upsert);
}

void AhoCorasick::BuildSuffixLink() {
  std::queue<BFSNodeItem> bfs_queue;
  bfs_queue.push(BFSNodeItem{trie_->root, trie_->root, {}, 0});

  while (!bfs_queue.empty()) {
    BFSNodeItem item = bfs_queue.front();
    bfs_queue.pop();

    std::tuple<uint8_t, ART::N256 *> children[256];
    uint32_t children_cnt = 0;
    item.node->getChildren(0, 255, children, children_cnt);

    if (item.node == trie_->root) {
      assert(item.node->isLastByteOfCodePoint());
      for (uint32_t i = 0; i < children_cnt; ++i) {
        const uint8_t byte = std::get<0>(children[i]);
        auto child_node    = std::get<1>(children[i]);
        if (ART::N256::isLeaf(child_node)) { continue; }

        if (child_node->isLastByteOfCodePoint()) {
          child_node->setSuffixLink(trie_->root);
          bfs_queue.push(BFSNodeItem{child_node, child_node, {}, 0});
        } else {
          std::array<uint8_t, 6> partial_bytes{};
          partial_bytes[0] = byte;
          bfs_queue.push(BFSNodeItem{child_node, item.node, partial_bytes, 1});
        }
      }
      continue;
    }

    for (uint32_t i = 0; i < children_cnt; ++i) {
      const uint8_t byte = std::get<0>(children[i]);
      auto child_node    = std::get<1>(children[i]);

      if (byte == NULL_TERMINATOR) {
        assert(ART::N256::isLeaf(child_node));
        continue;
      }

      if (!child_node->isLastByteOfCodePoint()) {
        std::array<uint8_t, 6> new_bytes = item.bytes_since_last_cp;
        assert(item.length < 6);
        new_bytes[item.length] = byte;
        bfs_queue.push(
          BFSNodeItem{child_node, item.last_codepoint_node, new_bytes, static_cast<uint8_t>(item.length + 1)});
        continue;
      }

      assert(child_node->isLastByteOfCodePoint());
      auto suffix_node = item.last_codepoint_node->getSuffixLink();

      while (suffix_node) {
        auto possible_suffix = suffix_node;
        assert(possible_suffix->isLastByteOfCodePoint());
        bool match = true;
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

      child_node->setSuffixLink(suffix_node);
      child_node->setOutputLink(suffix_node->isTerminalNode() ? suffix_node : suffix_node->getOutputLink());
      assert(!child_node->getOutputLink() || child_node->getOutputLink()->isTerminalNode());

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
