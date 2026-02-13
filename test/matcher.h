#include "aho_corasick/aho_corasick.h"
#include "aho_corasick/skeleton.h"
#include "common/utf8.h"
#include "fmt/format.h"

#include <cstdlib>
#include <deque>
#include <queue>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

bool DuckDBMatching(const char *sdata, size_t slen, const char *pdata, size_t plen) {
  size_t pidx = 0;
  size_t sidx = 0;
  for (; pidx < plen && sidx < slen;) {
    auto pchar = umbra::Utf8::readCodePoint(&pdata[pidx], pdata + plen);
    auto schar = umbra::Utf8::readCodePoint(&sdata[sidx], sdata + slen);

    if (pchar.codePoint == aho_corasick::UNDERSCORE) {
      pidx = pchar.next - pdata;
      sidx = schar.next - sdata;
    } else if (pchar.codePoint == aho_corasick::PERCENTAGE) {
      while (pidx < plen && pchar.codePoint == aho_corasick::PERCENTAGE) {
        pidx  = pchar.next - pdata;
        pchar = umbra::Utf8::readCodePoint(&pdata[pidx], pdata + plen);
      }
      if (pidx == plen) { return true; /* tail is acceptable */ }
      for (; sidx < slen;) {
        if (DuckDBMatching(sdata + sidx, slen - sidx, pdata + pidx, plen - pidx)) { return true; }
        sidx  = schar.next - sdata;
        schar = umbra::Utf8::readCodePoint(&sdata[sidx], sdata + slen);
      }
      return false;
    } else if (pchar.codePoint == schar.codePoint) {
      pidx = pchar.next - pdata;
      sidx = schar.next - sdata;
    } else {
      return false;
    }
  }
  auto pchar = umbra::Utf8::readCodePoint(&pdata[pidx], pdata + plen);
  while (pidx < plen && pchar.codePoint == aho_corasick::PERCENTAGE) {
    pidx  = pchar.next - pdata;
    pchar = umbra::Utf8::readCodePoint(&pdata[pidx], pdata + plen);
  }
  return pidx == plen && sidx == slen;
}

bool GreedyMatching(const char *sdata, size_t slen, const char *pdata, size_t plen) {
  size_t sidx = 0;
  size_t pidx = 0;

  // backtracking positions (byte offsets)
  size_t star_pidx = (size_t)-1;
  size_t star_sidx = (size_t)-1;

  while (sidx < slen) {
    if (pidx < plen) {
      auto pchar = umbra::Utf8::readCodePoint(&pdata[pidx], pdata + plen);

      // Case 1: '_' matches exactly one Unicode codepoint
      if (pchar.codePoint == aho_corasick::UNDERSCORE) {
        auto schar = umbra::Utf8::readCodePoint(&sdata[sidx], sdata + slen);
        sidx       = schar.next - sdata;
        pidx       = pchar.next - pdata;
        continue;
      }

      // Case 2: exact Unicode codepoint match
      if (pchar.codePoint != aho_corasick::PERCENTAGE) {
        auto schar = umbra::Utf8::readCodePoint(&sdata[sidx], sdata + slen);
        if (pchar.codePoint == schar.codePoint) {
          sidx = schar.next - sdata;
          pidx = pchar.next - pdata;
          continue;
        }
      }

      // Case 3: '%' wildcard
      if (pchar.codePoint == aho_corasick::PERCENTAGE) {
        star_pidx = pchar.next - pdata;  // pattern after %
        star_sidx = sidx;
        pidx      = star_pidx;
        continue;
      }
    }

    // Case 4: mismatch — backtrack to last '%'
    if (star_pidx != (size_t)-1) {
      // consume one more Unicode codepoint from string
      auto schar = umbra::Utf8::readCodePoint(&sdata[star_sidx], sdata + slen);
      star_sidx  = schar.next - sdata;

      sidx = star_sidx;
      pidx = star_pidx;
      continue;
    }

    return false;
  }

  // Consume remaining '%' in pattern
  while (pidx < plen) {
    auto pchar = umbra::Utf8::readCodePoint(&pdata[pidx], pdata + plen);
    if (pchar.codePoint != aho_corasick::PERCENTAGE) break;
    pidx = pchar.next - pdata;
  }

  return pidx == plen;
}

bool AhoCorasickMatching(const char *s, size_t slen, const char *p, size_t plen) {
  // Aho-Corasick env
  auto trie = aho_corasick::AhoCorasick();
  auto t    = trie.Local();

  // Pre-processing the pattern into pattern skeleton, then insert the split literals into AhoCorasick's trie
  auto skeleton = aho_corasick::Skeleton(
    p, plen, [&](aho_corasick::Token &tok) { trie.Insert(p + tok.start, tok.len, {0, tok.start}, t); });
  if (skeleton.IsEmpty() || skeleton.OnlyWildcard()) {
    return aho_corasick::Skeleton::SpecialMatchEmptyPattern(slen, p, plen);
  }

  // Building suffix & output links
  trie.BuildSuffixLink(1);

  // Start matching text
  auto instance = skeleton.InitializeMatcher();
  std::queue<std::tuple<u64, u64, u64, u64>> queue;
  auto iterate = aho_corasick::AhoCorasick::IterativeParseText(s, slen, trie.GetRoot());

  while (iterate.CanAdvanceOneCodePoint()) {
    auto end_offset  = iterate.text_offset;
    auto ac_matchers = trie.ContinueParseText(iterate);
    assert(iterate.text_offset > end_offset);  // must advance cursor
    auto recent_cp_len = iterate.text_offset - end_offset;
    // fmt::println("Current cp idx {}", iterate.iterator_idx);

    // 1st. Check the possible matched literals
    auto &segment = skeleton[instance.CurrentSegmentIdx()];
    for (auto &match : ac_matchers) {
      // Must match within the current considerate pattern
      // Focus on the comparison: match.text_start_pos >= instance.min_text_start_pos
      if (skeleton.MayMatch(match, instance)) {
        auto underscore_cnt = 0UL;
        if (skeleton.TryMatchingLiteral(match, instance, underscore_cnt)) {
          // Now, check if we just match the last literal of the skeleton
          if (segment.IsLastLiteral(match.pattern_index.start_pos)) {
            // If the last `match` helps satisfy the whole skeleton, then we find a match
            if (skeleton.SatisfyMatcher(match, instance, s, slen)) {
              if (!skeleton.AdvanceNextSegment(end_offset + segment.suffix_underscore_cnt + 1, instance)) {
                return true;
              }
            }
          } else {
            // Otherwise, add the matching info to the queue
            // THis is to support Unicode, i.e., one Unicode character may correspond to multiple bytes;
            //  which means, one _ may match multiple bytes in the text.
            // As such, matching literals consecutively may have different positional differences between
            //      [text_byte_offset - pattern_byte_offset]
            // Therefore, we append the matching info to a queue, i.e., delay creating the expected mapping
            //      <text_byte_offset => pattern_byte_offset> for subsequent literal matching
            assert(underscore_cnt > 0);
            fmt::println("Delay matching: [diff: {}, pat_start_pos: {}]", underscore_cnt + iterate.iterator_idx,
                         underscore_cnt + match.pattern_index.start_pos + match.literal_len);
            queue.emplace(underscore_cnt + iterate.iterator_idx, instance.CurrentSegmentIdx(),
                          underscore_cnt + match.pattern_index.start_pos + match.literal_len,
                          match.pattern_index.start_pos);
          }
        }
      }
    }

    // 2nd. Check the queue to proceed with the delayed matching
    while (!queue.empty() && std::get<0>(queue.front()) <= iterate.iterator_idx) {
      auto &item    = queue.front();
      auto &segment = skeleton[std::get<1>(item)];
      fmt::println("Insert matching: [diff: {}, next_start_pos: {}, prev_start_pos: {}]",
                   iterate.text_offset - std::get<2>(item), std::get<2>(item), std::get<3>(item));
      instance.Upsert(iterate.text_offset - std::get<2>(item), std::get<3>(item));
      queue.pop();
    }
  }
  return false;
}

// template <typename StringT>
// auto AhoCorasickMultiplePatterns(const char *s, size_t slen, std::vector<StringT> patterns) -> std::vector<bool> {
//   // Aho-Corasick env
//   auto trie = aho_corasick::AhoCorasick();
//   auto t    = trie.Local();
//   std::vector<bool> result(patterns.size(), false);

//   // Pre-processing the pattern into pattern skeleton, then insert the split literals into AhoCorasick's trie
//   std::vector<aho_corasick::Skeleton> skeleton;
//   std::vector<aho_corasick::Skeleton::Matcher> instance;
//   for (auto idx = 0U; idx < patterns.size(); idx++) {
//     auto &pat = patterns[idx];
//     skeleton.emplace_back(reinterpret_cast<char *>(pat.data()), pat.size(), [&](aho_corasick::Token &tok) {
//       trie.Insert(reinterpret_cast<char *>(pat.data()) + tok.start, tok.len, {idx, tok.start}, t);
//     });
//     if (skeleton.back().IsEmpty() || skeleton.back().OnlyWildcard()) {
//       result[idx] =
//         aho_corasick::Skeleton::SpecialMatchEmptyPattern(slen, reinterpret_cast<char *>(pat.data()), pat.size());
//       instance.emplace_back(0);
//     } else {
//       instance.push_back(skeleton.back().InitializeMatcher());
//     }
//   }

//   // Building suffix & output links
//   trie.BuildSuffixLink(1);

//   auto iterate = aho_corasick::AhoCorasick::IterativeParseText(s, slen, trie.GetRoot());
//   for (auto end_offset = 0UL; end_offset < slen; end_offset++) {
//     auto ac_matchers = trie.ContinueParseText(iterate);

//     for (auto &match : ac_matchers) {
//       auto pat_id   = match.pattern_index.pattern_id;
//       auto &segment    = skeleton[pat_id];
//       auto &matcher = instance[pat_id];

//       if (!result[pat_id] && segment.MayMatch(match, matcher)) {
//         auto success = segment.TryMatchingLiteral(match, matcher);

//         if (success) {
//           // Now, check if we just insert the last match of the segment
//           if (segment[matcher.CurrentSegmentIdx()].IsLastLiteral(match.pattern_index.start_pos) &&
//               segment.SatisfyMatcher(match, matcher, s, slen)) {
//             if (!segment.AdvanceNextSegment(end_offset + segment[matcher.CurrentSegmentIdx()].suffix_underscore_cnt +
//             1,
//                                          matcher)) {
//               result[pat_id] = true;
//             }
//           }
//         }
//       }
//     }
//   }

//   return result;
// }
