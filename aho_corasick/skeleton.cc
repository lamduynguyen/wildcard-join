#include "aho_corasick/skeleton.h"

#include "fmt/format.h"

namespace aho_corasick {

auto Token::NextToken(const char *s, size_t slen, std::size_t &pos) -> Token {
  auto prev_pos = pos;
  while (pos < slen && IsDelim(s[pos])) { pos++; }
  if (pos >= slen) {
    // no more tokens; resetting pos back to suffix processing
    pos = prev_pos;
    return {std::string_view::npos, 0};
  }
  std::size_t start = pos;
  while (pos < slen && !IsDelim(s[pos])) { pos++; }
  return {start, pos - start};
};

auto Token::NextSegment(const char *s, size_t slen, std::size_t &pos) -> Token {
  auto prev_pos = pos;
  while (pos < slen && s[pos] == PERCENTAGE) { pos++; }
  if (pos >= slen) {
    // no more tokens; resetting pos back to suffix processing
    pos = prev_pos;
    return {std::string_view::npos, 0};
  }
  std::size_t start = pos;
  while (pos < slen && s[pos] != PERCENTAGE) { pos++; }
  return {start, pos - start};
};

auto Skeleton::Segment::Contain(u64 start_pos) -> bool { return prev_literal_offset.contains(start_pos); }

auto Skeleton::Segment::IsFirstLiteral(u64 start_pos) -> bool {
  return IsPreviousLiteral(std::numeric_limits<u64>::max(), start_pos);
}

auto Skeleton::Segment::IsPreviousLiteral(u64 prev_start_pos, u64 start_pos) -> bool {
  assert(Contain(start_pos));
  return prev_literal_offset[start_pos] == prev_start_pos;
}

Skeleton::Skeleton(const char *p, u64 plen, const std::function<void(Token &)> &literal_fn) : only_wildcard(true) {
  size_t last_end_pos = 0;
  while (true) {
    Segment match = {};

    // Extract tokens by PERCENTAGE only
    auto prev_pos = last_end_pos;
    auto tok      = Token::NextSegment(p, plen, last_end_pos);
    if (tok.start == std::string::npos) { break; }
    if (prev_pos < tok.start) { match.has_prefix_percent = true; };

    // Build between-PERCENTAGE skeleton based on the extracted token -- p[tok.start : last_end_pos]
    auto prev_start = std::numeric_limits<u64>::max();
    auto pos        = tok.start;
    for (auto pos = tok.start; pos < last_end_pos;) {
      auto literal = Token::NextToken(p, plen, pos);
      if (literal.start == std::string::npos) {
        // this means we have a suffix _ scenario
        match.suffix_underscore_cnt = last_end_pos - pos;
        break;
      }
      match.prev_literal_offset.emplace(literal.start, prev_start);
      prev_start                   = literal.start;
      match.last_literal_start_pos = literal.start;
      literal_fn(literal);
      only_wildcard = false;
    }
    seg.emplace_back(std::move(match));
  }
  // Fill in `has_suffix_percent` info for all segment
  if (!seg.empty()) {
    if (last_end_pos != plen) { seg.back().has_suffix_percent = true; }
    for (int idx = seg.size() - 2; idx >= 0; idx--) { seg[idx].has_suffix_percent = seg[idx + 1].has_prefix_percent; }
  }
}

// This should only be called if the pattern only contains % and _
auto Skeleton::SpecialMatchEmptyPattern(size_t slen, const char *p, size_t plen) -> bool {
  // Count how many _ and check if slen >= number of underscores
  auto underscore_cnt = 0UL;
  auto has_percent    = false;
  for (auto idx = 0UL; idx < plen; idx++) {
    if (p[idx] == UNDERSCORE) {
      underscore_cnt++;
    } else if (p[idx] == PERCENTAGE) {
      has_percent = true;
    }
  }
  return (slen == underscore_cnt) || (slen > underscore_cnt && has_percent);
}

}  // namespace aho_corasick