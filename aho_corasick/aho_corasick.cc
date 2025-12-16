#include "aho_corasick/aho_corasick.h"

#include <cassert>
#include <cstring>
#include <queue>

namespace aho_corasick {

AhoCorasick::AhoCorasick() : trie_(std::make_unique<ART::Tree>()) {}

auto AhoCorasick::Local() -> ART::ThreadInfo { return trie_->getThreadInfo(); }

auto AhoCorasick::GetRoot() -> ART::N * { return trie_->root; }

void AhoCorasick::Insert(const char *keyword, uint64_t keyword_size, const PatternIndexType &keyword_aux_index,
                         ART::ThreadInfo &t) {
  assert(keyword_size > 0);                                   // Never accept null key
  assert(keyword[keyword_size - 1] == ART::NULL_TERMINATOR);  // All keywords/patterns must end with null terminator
  Key key;
  key.set(keyword, keyword_size);
  auto new_tid = [&]() {
    auto it = pattern_.emplace_back(std::vector<PatternIndexType>{keyword_aux_index});
    return it - pattern_.begin();
  };
  auto upsert = [this, keyword_aux_index](TupleID tid) { pattern_[tid].emplace_back(keyword_aux_index); };
  trie_->insert(key, new_tid, upsert, t);
}

void AhoCorasick::BuildSuffixLink(u16 number_of_threads) {
  // Single-threaded for now. TODO: Do we need multi-threaded version?
  auto bfs_stack = std::queue<std::pair<ART::N *, uint32_t>>();
  bfs_stack.emplace(trie_->root, 0);

  while (!bfs_stack.empty()) {
    auto [node, node_level] = bfs_stack.front();
    bfs_stack.pop();

    // get current node's children
    std::tuple<uint8_t, ART::N *> children[256];
    uint32_t children_cnt = 0;
    ART::N::getChildren(node, 0u, 255u, children, children_cnt);

    // children of the root node all point suffix link to the root
    if (node == trie_->root) {
      for (auto i = 0; i < children_cnt; ++i) {
        const auto key = std::get<0>(children[i]);
        const auto n   = std::get<1>(children[i]);
        if (!ART::N::isLeaf(n)) {
          n->setSuffixLink(node);
          bfs_stack.emplace(n, node_level + 1);
        }
      }
      continue;
    }

    // otherwise, start matching new suffix link
    for (auto i = 0; i < children_cnt; ++i) {
      const auto key = std::get<0>(children[i]);
      const auto n   = std::get<1>(children[i]);

      if (key != ART::NULL_TERMINATOR) {
        auto suffix_node = node->getSuffixLink();
        do {
          auto possible_suffix = ART::N::getChild(key, suffix_node);
          if (possible_suffix != nullptr) {
            suffix_node = possible_suffix;
            break;
          }
          suffix_node = suffix_node->getSuffixLink();
        } while (suffix_node);
        if (!suffix_node) { suffix_node = trie_->root; }
        n->setSuffixLink(suffix_node);
        n->setOutputLink((suffix_node->isTerminalNode()) ? suffix_node : suffix_node->getOutputLink());
        assert((n->getOutputLink() == nullptr) || (n->getOutputLink()->isTerminalNode()));
        bfs_stack.emplace(n, node_level + 1);
      }
    }
  }
}

auto AhoCorasick::ParseText(const char *text, size_t text_len) -> OutputEmitType {
  /**
   * Output link logic
   * - In the original AhoCorasick, when following the suffix links, we meet an ART node whose has a NULL TERMINATOR
   *    => this is an output link
   * - In our implementation, we directly store all output links (the NULL TERMINATOR node) as the trie leaf
   */
  OutputEmitType result;
  auto iterate = StartIterativeParseText(text, text_len);
  for (auto idx = 0UL; idx < text_len; idx++) {
    auto next_set = ContinueParseText(iterate);
    result.merge(next_set);
  }
  return result;
}

auto AhoCorasick::StartIterativeParseText(const char *text, size_t text_len) -> IterativeParseText {
  auto ret        = IterativeParseText();
  ret.text        = text;
  ret.text_len    = text_len;
  ret.next_offset = 0;
  ret.ptr         = trie_->root;
  return ret;
}

// The caller
auto AhoCorasick::ContinueParseText(IterativeParseText &ite) -> OutputEmitType {
  OutputEmitType result;

  auto c             = ite.text[ite.next_offset];
  auto possible_next = ART::N::getChild(c, ite.ptr);
  // Three cases:
  //  1. If the next possible state is a nullptr, we go back to root
  //  2. If the next possible state is a leaf (due to lazy expansive + end all keywords as NULL terminator),
  //      we go back to root and output that pattern
  //  3. Otherwise, move forward to that state
  while (ite.ptr != trie_->root && possible_next == nullptr) {
    ite.ptr       = ite.ptr->getSuffixLink();
    possible_next = ART::N::getChild(c, ite.ptr);
  }
  assert((possible_next != nullptr) || (ite.ptr == trie_->root));  // assertion for case #1
  if (possible_next != nullptr) {
    // case #2 & #3
    ite.ptr = possible_next;
    if (ite.ptr->isTerminalNode()) {
      // case #2: matching for 2nd case
      auto leaf = ART::N::getChild(ART::NULL_TERMINATOR, possible_next);
      assert(ART::N::isLeaf(leaf));
      auto keyword_id = ART::N::getLeaf(leaf)->aux_index;
      for (auto &pattern_idx : pattern_[keyword_id]) {
        result.emplace(pattern_idx, ite.next_offset - pattern_idx.keyword_len + 1);
      }
    }
  }
  // Evaluate output links
  auto output_link = ite.ptr->getOutputLink();
  if (output_link != nullptr) {
    assert(output_link->isTerminalNode());
    auto leaf = ART::N::getChild(ART::NULL_TERMINATOR, output_link);
    assert(ART::N::isLeaf(leaf));
    auto keyword_id = ART::N::getLeaf(leaf)->aux_index;
    for (auto &pattern_idx : pattern_[keyword_id]) {
      result.emplace(pattern_idx, ite.next_offset - pattern_idx.keyword_len + 1);
    }
  }

  // Advance next offset in the text for next processing
  ite.next_offset++;
  return result;
}

}  // namespace aho_corasick