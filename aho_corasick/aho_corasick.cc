#include "aho_corasick/aho_corasick.h"

#include <cassert>
#include <cstring>

namespace aho_corasick {

void ThreadLocalInfo::LoadKey(TupleID tid, Key &key) {
  assert(pattern.size() > tid);
  key.set(pattern[tid].c_str(), pattern[tid].size());
}

auto ThreadLocalInfo::CheckKey(const TupleID tid, const Key &k) -> bool {
  Key cmp_key;
  cmp_key.set(pattern[tid].c_str(), pattern[tid].size());
  return k == cmp_key;
}

AhoCorasick::AhoCorasick() : trie_(std::make_unique<ART_OLC::Tree>()) {}

auto AhoCorasick::Local() -> ThreadLocalInfo {
  return ThreadLocalInfo(pattern_.local(), pattern_indices_.local(), trie_->getThreadInfo());
}

void AhoCorasick::Insert(char *keyword, uint64_t keyword_size, PatternIndexType keyword_aux_index, ThreadLocalInfo &t) {
  assert(keyword[keyword_size - 1] == '\0');  // All keywords/patterns must end with null terminator
  Key key;
  key.set(keyword, keyword_size);
  auto load_key = [&](TupleID tid, Key &key) { t.LoadKey(tid, key); };
  auto tid      = trie_->lookup(
    key, load_key, [&](const TupleID &tid, const Key &key) { return t.CheckKey(tid, key); }, t.art_tlocal);
  if (tid == ART_OLC::Tree::INVALID_TID) {
    t.pattern.emplace_back(keyword, keyword_size);
    t.pattern_indices.push_back(EmitType{keyword_aux_index});
    trie_->insert(key, pattern_.size(), load_key, t.art_tlocal);
  } else {
    t.pattern_indices[tid].emplace_back(keyword_aux_index);
  }
}

}  // namespace aho_corasick