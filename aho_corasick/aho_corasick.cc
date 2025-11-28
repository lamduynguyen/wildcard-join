#include "aho_corasick/aho_corasick.h"

#include <cassert>
#include <cstring>
#include <stack>

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

void AhoCorasick::Insert(char *keyword, uint64_t keyword_size, PatternIndexType keyword_aux_index,
                         ART::ThreadInfo &t) {
  assert(keyword[keyword_size - 1] == '\0');  // All keywords/patterns must end with null terminator
  Key key;
  key.set(keyword, keyword_size);
  auto tid = trie_->lookup(key, t);
  if (tid == ART::Tree::INVALID_TID) {
    pattern_[keyword_aux_index.first] = std::string(keyword, keyword_size);
    pattern_indices_[keyword_aux_index.first].Append(keyword_aux_index);
    trie_->insert(key, keyword_aux_index.first, t);
  } else {
    pattern_indices_[tid].Append(keyword_aux_index);
  }
}

void AhoCorasick::BuildSuffixLink() {
  // Single-threaded for now. TODO: Do we need multi-threaded version?
  auto bfs_stack = std::stack<ART::N*>();
}

auto AhoCorasick::Contain(char *keyword_data, uint64_t keyword_size) -> bool {}

auto AhoCorasick::ParseText(std::string_view text) -> std::vector<EmitType> {
  /**
   * TODO: Output link logic
   * - If following suffix links, we go into an ART node whose has a null terminator => this is an output link
   * - The child (of that ART node's null terminator) is an index into
   */
}

}  // namespace aho_corasick