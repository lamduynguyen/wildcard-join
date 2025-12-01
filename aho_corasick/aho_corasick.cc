#include "aho_corasick/aho_corasick.h"

#include <cassert>
#include <cstring>
#include <queue>

namespace aho_corasick {

AhoCorasick::AhoCorasick(uint64_t no_patterns) : pattern_(no_patterns), pattern_indices_(no_patterns) {
  auto load_key = [&](TupleID tid, Key &key) {
    assert(pattern_.size() > tid);
    key.set(pattern_[tid].c_str(), pattern_[tid].size());
  };
  auto check_key = [&](const TupleID tid, const Key &k) -> bool {
    Key cmp_key;
    cmp_key.set(pattern_[tid].c_str(), pattern_[tid].size());
    return k == cmp_key;
  };
  trie_ = std::make_unique<ART::Tree>(load_key, check_key);
}

auto AhoCorasick::Local() -> ART::ThreadInfo { return trie_->getThreadInfo(); }

void AhoCorasick::Insert(char *keyword, uint64_t keyword_size, PatternIndexType keyword_aux_index, ART::ThreadInfo &t) {
  assert(keyword[keyword_size - 1] == '\0');  // All keywords/patterns must end with null terminator
  Key key;
  key.set(keyword, keyword_size);
  auto tid = trie_->lookup(key, t);
  if (tid == ART::Tree::INVALID_TID) {
    pattern_[keyword_aux_index.first] = std::string(keyword, keyword_size);
    pattern_indices_[keyword_aux_index.first].emplace_back(keyword_aux_index);
    trie_->insert(key, keyword_aux_index.first, t);
  } else {
    pattern_indices_[tid].emplace_back(keyword_aux_index);
  }
}

void AhoCorasick::BuildSuffixLink() {
  // Single-threaded for now. TODO: Do we need multi-threaded version?
  auto bfs_stack = std::queue<ART::N *>();
  bfs_stack.emplace(trie_->root);

  while (!bfs_stack.empty()) {
    auto node = bfs_stack.front();
    bfs_stack.pop();

    // get current node's children
    std::tuple<uint8_t, ART::N *> children[256];
    uint32_t children_cnt = 0;
    ART::N::getChildren(node, 0u, 255u, children, children_cnt);

    // children of the root node all point suffix link to the root
    if (node == trie_->root) {
      for (auto i = 0; i < children_cnt; ++i) {
        const auto n = std::get<1>(children[i]);
        n->setSuffixLink(node);
        bfs_stack.emplace(n);
      }
      continue;
    }

    // otherwise, start matching new suffix link
    for (auto i = 0; i < children_cnt; ++i) {
      const auto key   = std::get<0>(children[i]);
      const auto n     = std::get<1>(children[i]);
      auto suffix_node = node->getSuffixLink();
      while (suffix_node != trie_->root) {
        auto possible_suffix = ART::N::getChild(key, suffix_node);
        if (possible_suffix != nullptr) {
          suffix_node = possible_suffix;
          break;
        }
        suffix_node = possible_suffix->getSuffixLink();
      }
      n->setSuffixLink(suffix_node);
      n->setOutputLink((suffix_node->isTerminalNode()) ? suffix_node : suffix_node->getOutputLink());
      bfs_stack.emplace(n);
    }
  }
}

auto AhoCorasick::ParseText(std::string_view text) -> std::vector<EmitType> {
  /**
   * TODO: Output link logic
   * - If following suffix links, we go into an ART node whose has a null terminator => this is an output link
   * - The child (of that ART node's null terminator) is an index into
   */
}

}  // namespace aho_corasick