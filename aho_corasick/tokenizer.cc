#include "aho_corasick/tokenizer.h"

#include <algorithm>

namespace aho_corasick {

auto Tokenizer::NextLiteral(const char *s, size_t slen, u32 &pos, u32 &max_underscore_cnt) -> TextUnit {
  auto prev_pos = pos;
  while (pos < slen && s[pos] == UNDERSCORE) { pos++; }
  max_underscore_cnt = std::max(max_underscore_cnt, pos - prev_pos);
  if (pos >= slen) {
    // no more tokens; resetting pos back to suffix processing
    pos = prev_pos;
    return {WRONG_OFFSET, 0};
  }
  auto start = pos;
  while (pos < slen && s[pos] != UNDERSCORE) { pos++; }
  return {start, pos - start};
};

auto Tokenizer::NextSegment(const char *s, size_t slen, u32 &pos) -> TextUnit {
  auto prev_pos = pos;
  while (pos < slen && s[pos] == PERCENTAGE) { pos++; }
  if (pos >= slen) {
    // no more tokens; resetting pos back to suffix processing
    pos = prev_pos;
    return {WRONG_OFFSET, 0};
  }
  auto start = pos;
  while (pos < slen && s[pos] != PERCENTAGE) { pos++; }
  return {start, pos - start};
};

}  // namespace aho_corasick