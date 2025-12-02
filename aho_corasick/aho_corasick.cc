#include "aho_corasick/aho_corasick.h"

#include <cassert>
#include <cstring>
#include <queue>

namespace aho_corasick {

AhoCorasick::AhoCorasick() {
  auto load_key = [&](TupleID tid, Key &key) {
    assert(pattern_.size() > tid);
    key.set(pattern_[tid].first.c_str(), pattern_[tid].first.size());
  };
  auto check_key = [&](const TupleID tid, const Key &k) -> bool {
    Key cmp_key;
    cmp_key.set(pattern_[tid].first.c_str(), pattern_[tid].first.size());
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
    auto it =
      pattern_.emplace_back(std::string(keyword, keyword_size), std::vector<PatternIndexType>{keyword_aux_index});
    auto keyword_id = it - pattern_.begin();
    trie_->insert(key, keyword_id, t);
  } else {
    pattern_[tid].second.emplace_back(keyword_aux_index);
  }
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
        const auto n = std::get<1>(children[i]);
        n->setSuffixLink(node);
        bfs_stack.emplace(n, node_level + 1);
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
      n->setOutputLink((suffix_node->isTerminalNode()) ? ART::N::getChild(NULL_TERMINATOR, suffix_node)
                                                       : suffix_node->getOutputLink());
      assert((n->getOutputLink() != nullptr) && (ART::N::isLeaf(n->getOutputLink())));
      if (!n->isTerminalNode()) { bfs_stack.emplace(n, node_level + 1); }
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
  for (auto c : text) {
    ptr              = ART::N::getChild(c, ptr);
    auto output_link = ptr->getOutputLink();
    assert((output_link != nullptr) && (ART::N::isLeaf(output_link)));
    auto keyword_id = ART::N::getLeaf(output_link);
    result.insert(pattern_[keyword_id].second.begin(), pattern_[keyword_id].second.end());
  }
  return result;
}

}  // namespace aho_corasick