#include "aho_corasick/aho_corasick.h"

#include <cassert>
#include <cstring>
#include <queue>
#include "fmt/format.h"

namespace aho_corasick {

AhoCorasick::AhoCorasick() {
  auto load_key = [&](TupleID tid, Key &key) {
    assert(pattern_.size() > tid);
    key.set(pattern_[tid].first.c_str(), pattern_[tid].first.size());
  };
  trie_ = std::make_unique<ART::Tree>(load_key);
}

auto AhoCorasick::Local() -> ART::ThreadInfo { return trie_->getThreadInfo(); }

void AhoCorasick::Insert(const char *keyword, uint64_t keyword_size, PatternIndexType keyword_aux_index,
                         ART::ThreadInfo &t) {
  assert(keyword[keyword_size - 1] == ART::NULL_TERMINATOR);  // All keywords/patterns must end with null terminator
  Key key;
  key.set(keyword, keyword_size);
  auto new_tid = [&]() {
    auto it =
      pattern_.emplace_back(std::string(keyword, keyword_size), std::vector<PatternIndexType>{keyword_aux_index});
    return it - pattern_.begin();
  };
  auto upsert = [this, keyword_aux_index](TupleID tid) { pattern_[tid].second.emplace_back(keyword_aux_index); };
  trie_->insert(key, new_tid, upsert, t);
}

void AhoCorasick::BuildSuffixLink(uint32_t until_level) {
  // Single-threaded for now. TODO: Do we need multi-threaded version?
  auto bfs_stack = std::queue<std::pair<ART::N *, uint32_t>>();
  bfs_stack.emplace(trie_->root, 0);

  while (!bfs_stack.empty()) {
    auto [node, node_level] = bfs_stack.front();
    bfs_stack.pop();
    if (node_level >= until_level) { break; }

    // get current node's children
    std::tuple<uint8_t, ART::N *> children[256];
    uint32_t children_cnt = 0;
    ART::N::getChildren(node, 0u, 255u, children, children_cnt);

    // children of the root node all point suffix link to the root
    if (node == trie_->root) {
      for (auto i = 0; i < children_cnt; ++i) {
        const auto key = std::get<0>(children[i]);
        const auto n   = std::get<1>(children[i]);
        if (!ART::N::isLeaf(n->getOutputLink())) {
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
        while (suffix_node != trie_->root) {
          auto possible_suffix = ART::N::getChild(key, suffix_node);
          if (possible_suffix != nullptr) {
            suffix_node = possible_suffix;
            break;
          }
          suffix_node = suffix_node->getSuffixLink();
        }
        n->setSuffixLink(suffix_node);
        n->setOutputLink((suffix_node->isTerminalNode()) ? suffix_node : suffix_node->getOutputLink());
        assert((n->getOutputLink() == nullptr) || (ART::N::isLeaf(n->getOutputLink())));
        bfs_stack.emplace(n, node_level + 1);
      }
    }
  }
}

auto AhoCorasick::ParseText(std::string_view text) -> OutputEmitType {
  /**
   * Output link logic
   * - In the original AhoCorasick, when following the suffix links, we meet an ART node whose has a NULL TERMINATOR
   *    => this is an output link
   * - In our implementation, we directly store all output links (the NULL TERMINATOR node) as the trie leaf
   */
  OutputEmitType result;
  auto ptr = trie_->root;
  auto pos = 0UL;
  for (auto c : text) {
    auto possible_next = ART::N::getChild(c, ptr);
    // Three cases:
    //  1. If the next possible state is a nullptr, we go back to root
    //  2. If the next possible state is a leaf (due to lazy expansive + end all keywords as NULL terminator),
    //      we go back to root and output that pattern
    //  3. Otherwise, move forward to that state
    while (ptr != trie_->root && possible_next == nullptr) {
      ptr           = ptr->getSuffixLink();
      possible_next = ART::N::getChild(c, ptr);
    }
    assert((possible_next == nullptr) || (ptr == trie_->root));  // assertion for case #1
    if (possible_next != nullptr) {
      // case #2 & #3
      ptr = possible_next;
      if (ptr->isTerminalNode()) {
        // case #2: matching for 2nd case
        auto tid = ART::N::getChild(ART::NULL_TERMINATOR, possible_next);
        assert(ART::N::isLeaf(tid));
        auto keyword_id = ART::N::getLeaf(tid);
        result.insert(pattern_[keyword_id].second.begin(), pattern_[keyword_id].second.end());
      }
    }
    // Evaluate output links
    auto output_link = ptr->getOutputLink();
    if (output_link != nullptr) {
      assert(output_link->isTerminalNode());
      auto tid = ART::N::getChild(ART::NULL_TERMINATOR, possible_next);
      assert(ART::N::isLeaf(tid));
      auto keyword_id = ART::N::getLeaf(tid);
      result.insert(pattern_[keyword_id].second.begin(), pattern_[keyword_id].second.end());
    }
    pos++;
  }
  return result;
}

}  // namespace aho_corasick