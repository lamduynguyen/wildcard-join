#include "aho_corasick/tokenizer.h"

#include <cassert>
#include <cstring>

namespace aho_corasick {

auto Tokenizer::NextSegment() -> std::optional<LiteralSpan> {
  const u32 before = pos_;

  while (pos_ < plen_ && IsSegmentDelim(pos_)) { pos_++; }

  if (pos_ >= plen_) {
    pos_ = before;  // preserve pos_ at end of last segment, not past trailing `%`
    return std::nullopt;
  }

  const u32 start = pos_;
  while (pos_ < plen_ && !IsSegmentDelim(pos_)) { pos_++; }

  lit_pos_       = start;
  accum_esc_len_ = 0;
  return LiteralSpan{start, pos_ - start};
}

auto Tokenizer::NextLiteral(const LiteralSpan &seg) -> std::optional<LiteralSpan> {
  const u32 seg_end = seg.start + seg.len;

  while (lit_pos_ < seg_end && IsLiteralDelim(lit_pos_)) { lit_pos_++; }

  if (lit_pos_ >= seg_end) { return std::nullopt; }

  const u32 start               = lit_pos_;
  const u32 escape_before_start = accum_esc_len_;  // snapshot: escape bytes from prior spans only

  while (lit_pos_ < seg_end && !IsLiteralDelim(lit_pos_) && !IsSegmentDelim(lit_pos_)) {
    if (has_escape_ && IsEscaped(lit_pos_) && IsEscapable(lit_pos_)) { accum_esc_len_++; }
    lit_pos_++;
  }

  return LiteralSpan{start, lit_pos_ - start, escape_before_start};
}

auto Tokenizer::StripEscapes(const LiteralSpan &span, char *out) const -> std::string_view {
  if (!has_escape_) {
    return {p_ + span.start, span.len};  // zero-copy, points into original pattern
  }
  const char *src = p_ + span.start;
  const char *end = src + span.len;
  char *dst       = out;

  while (src < end) {
    if (*src == esc_char_) {
      src++;                               // skip escape char
      if (src < end) { *dst++ = *src++; }  // copy next verbatim
    } else {
      *dst++ = *src++;
    }
  }

  return {out, static_cast<size_t>(dst - out)};  // points into caller's buf
}

}  // namespace aho_corasick
