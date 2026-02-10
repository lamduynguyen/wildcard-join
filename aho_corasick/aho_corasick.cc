#include "aho_corasick/aho_corasick.h"

#include "fmt/format.h"

#include <cassert>
#include <cstring>
#include <queue>

namespace aho_corasick {

std::atomic<u64> AhoCorasick::NUMBER_OF_UNIQUE_LITERALS = 0;

AhoCorasick::AhoCorasick() : trie_(std::make_unique<ART::Tree>()) {}

auto AhoCorasick::Local() -> ART::ThreadInfo { return trie_->getThreadInfo(); }

auto AhoCorasick::GetRoot() -> ART::N * { return trie_->root; }

void AhoCorasick::Insert(const char *keyword, uint64_t keyword_len, const PatternIndexType &keyword_auxIndex,
                         ART::ThreadInfo &t) {
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
  trie_->insert(keyword, keyword_len, must_append_null, new_tid, upsert, t);
}

void AhoCorasick::BuildSuffixLink(u16 number_of_threads) {
  // Single-threaded for now. TODO: Do we need multi-threaded version?
  auto bfs_stack = std::queue<SuffixLinkQueueItem>();
  bfs_stack.emplace(trie_->root, nullptr, 0, "");

  while (!bfs_stack.empty()) {
    auto item = bfs_stack.front();
    bfs_stack.pop();

    // get current node's children
    std::tuple<uint8_t, ART::N *> children[256];
    uint32_t children_cnt = 0;
    ART::N::getChildren(item.cur, 0u, 255u, children, children_cnt);

    // children of the root node all point suffix link to the root
    if (item.cur == trie_->root) {
      assert(item.cur->isLastByteOfCodePoint());  // Root should always be valid last byte of code point
      for (auto i = 0; i < children_cnt; ++i) {
        const auto key = std::get<0>(children[i]);
        const auto n   = std::get<1>(children[i]);
        if (!ART::N::isLeaf(n)) {
          if (n->isLastByteOfCodePoint()) {
            ART::N::setSuffixLink(item.cur, n);
            fmt::println("1. Emplace {:p}", fmt::ptr(n));
            bfs_stack.emplace(n, n, item.level + 1, "");
          } else {
            fmt::println("2. Emplace {:p}", fmt::ptr(n));
            bfs_stack.emplace(n, item.cur, item.level + 1, std::string(1, key));
          }
        }
      }
      continue;
    }

    // otherwise, start matching new suffix link
    for (auto i = 0; i < children_cnt; ++i) {
      const auto key = std::get<0>(children[i]);
      const auto n   = std::get<1>(children[i]);

      if (key == NULL_TERMINATOR) {
        assert(ART::N::isLeaf(n));
        continue;
      }

      if (n->isLastByteOfCodePoint()) {
        auto suffix_node = ART::N::getSuffixLink(item.last_valid_parent);
        while (suffix_node) {
          auto possible_suffix = suffix_node;
          assert(possible_suffix->isLastByteOfCodePoint());
          bool found           = true;
          for (char &c : item.current_intermediate_str) {
            auto next = ART::N::getChild(static_cast<uint8_t>(c), possible_suffix);
            if (!next) {
              found = false;
              break;
            }
            possible_suffix = next;
          }
          if (found) {
            auto next = ART::N::getChild(key, possible_suffix);
            if (next) {
              suffix_node = next;
              break;
            }
          }
          suffix_node = ART::N::getSuffixLink(suffix_node);
        }
        if (!suffix_node) { suffix_node = trie_->root; }
        assert(suffix_node->isLastByteOfCodePoint());
        ART::N::setSuffixLink(suffix_node, n);
        ART::N::setOutputLink((suffix_node->isTerminalNode()) ? suffix_node : ART::N::getOutputLink(suffix_node), n);
        assert((ART::N::getOutputLink(n) == nullptr) || (ART::N::getOutputLink(n)->isTerminalNode()));
        fmt::println("3. Emplace {:p}", fmt::ptr(n));
        bfs_stack.emplace(n, n, item.level + 1, "");
      } else {
        fmt::println("4. Emplace {:p}", fmt::ptr(n));
        bfs_stack.emplace(n, item.last_valid_parent, item.level + 1,
                          item.current_intermediate_str + static_cast<char>(key));
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
    ite.ptr       = ART::N::getSuffixLink(ite.ptr);
    possible_next = ART::N::getChild(c, ite.ptr);
  }
  assert((possible_next != nullptr) || (ite.ptr == trie_->root));  // assertion for case #1
  if (possible_next != nullptr) {
    // case #2 & #3
    ite.ptr = possible_next;
    if (ite.ptr->isTerminalNode()) {
      // case #2: matching for 2nd case
      auto leaf = ART::N::getChild(NULL_TERMINATOR, possible_next);
      AppendResult(ite, leaf, result);
    }
  }
  // Evaluate output links
  auto output_link = ART::N::getOutputLink(ite.ptr);
  while (output_link != nullptr) {
    if (output_link->isTerminalNode()) {
      auto leaf = ART::N::getChild(NULL_TERMINATOR, output_link);
      AppendResult(ite, leaf, result);
    }
    output_link = ART::N::getOutputLink(output_link);  // follow the suffix-link chain
  }

  // Advance next offset in the text for next processing
  ite.next_offset++;
  return result;
}

void AhoCorasick::AppendResult(const IterativeParseText &ite, ART::N *leaf, OutputEmitType &out_result) {
  assert(ART::N::isLeaf(leaf));
  auto keyword_id  = ART::N::getLeaf(leaf)->auxIndex;
  auto literal_len = ART::N::getLeaf(leaf)->keyLenWithoutNullTerminator();
  auto match_pos   = ite.next_offset - literal_len + 1;
  auto &bitmap     = literal_map_[keyword_id];
  for (auto value : bitmap) {
    auto pattern_idx = PatternIndexType::FromUint(value);
    out_result.emplace(pattern_idx, literal_len, match_pos);
  }
}

}  // namespace aho_corasick