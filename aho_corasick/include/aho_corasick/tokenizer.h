#pragma once

#include "common/typedef.h"

#include <optional>
#include <string_view>

namespace aho_corasick {

/**
 * @brief A contiguous span of non-wildcard bytes within a pattern.
 *
 * `start` and `len` are byte offsets into the original pattern string.
 * `escape_prefix_before_start` is the total escape-prefix bytes consumed by escaped characters in
 *  all prior spans of this segment; subtract from `start` to get the clean offset:
 *   clean_start = start - escape_prefix_before_start
 *
 * Call StripEscapes() to obtain the clean content (i.e., without escape chars) of this span.
 */
struct LiteralSpan {
  u32 start;
  u32 len;
  u32 escape_prefix_before_start = 0;
};

/**
 * @brief Parses a SQL LIKE pattern into percent-segments and literal runs.
 *
 * Two-level tokenization:
 *   Level 1 -- NextSegment(): splits on unescaped `%`.
 *   Level 2 -- NextLiteral(): within a segment, splits on unescaped `_`.
 *
 * Escape (SQL ESCAPE clause, single character):
 *   The escape character immediately before `%`, `_`, or itself makes that
 *   character literal.
 *   Example (escape='!'):  "50!% off" → "50% off",  "a!!b" → "a!b"
 *
 * Typical usage:
 *   Tokenizer tok(pattern, plen, "!");
 *   while (auto seg = tok.NextSegment()) {
 *     while (auto lit = tok.NextLiteral(*seg)) {
 *       char buf[lit->len + 1]{};
 *       u32 clean_len   = tok.StripEscapes(*lit, buf);
 *       u32 clean_start = lit->start - lit->escape_prefix_before_start;
 *       // use buf[0..clean_len) at clean_start
 *     }
 *   }
 */
class Tokenizer {
 public:
  static constexpr auto PERCENTAGE = '%';
  static constexpr auto UNDERSCORE = '_';

  Tokenizer(const char *pattern, u32 plen, std::string_view escape_str = "") noexcept
      : p_(pattern),
        plen_(plen),
        esc_char_(escape_str.empty() ? '\0' : escape_str[0]),
        has_escape_(!escape_str.empty()),
        pos_(0),
        lit_pos_(0),
        accum_esc_len_(0) {}

  /** Advance to the next `%`-delimited segment; returns nullopt when exhausted. */
  auto NextSegment() -> std::optional<LiteralSpan>;

  /**
   * Advance to the next literal run within `seg` (split on unescaped `_`).
   * Returns nullopt when the segment is exhausted.
   */
  auto NextLiteral(const LiteralSpan &seg) -> std::optional<LiteralSpan>;

  /**
   * Copy `span`'s bytes into `out` with escape prefixes removed.
   * `out` must have capacity >= span.len. Returns bytes written (<= span.len).
   */
  auto StripEscapes(const LiteralSpan &span, char *out) const -> std::string_view;

  /** Byte position after the last completed segment (before any trailing `%`). */
  inline auto Pos() const noexcept -> u32 { return pos_; }

  /** True if the character at `i` is immediately preceded by the escape character. */
  inline auto IsEscaped(u32 i) const noexcept -> bool { return has_escape_ && i > 0 && p_[i - 1] == esc_char_; }

 private:
  inline auto IsSegmentDelim(u32 i) const noexcept -> bool { return p_[i] == PERCENTAGE && !IsEscaped(i); }

  inline auto IsLiteralDelim(u32 i) const noexcept -> bool { return p_[i] == UNDERSCORE && !IsEscaped(i); }

  // `%`, `_`, or the escape character itself -- the three escapable characters.
  inline auto IsEscapable(u32 i) const noexcept -> bool {
    return p_[i] == PERCENTAGE || p_[i] == UNDERSCORE || p_[i] == esc_char_;
  }

  const char *p_;      // pattern bytes
  u32 plen_;           // pattern length in bytes
  char esc_char_;      // escape character ('\0' if none)
  bool has_escape_;    // false when no escape string was provided
  u32 pos_;            // segment-level cursor
  u32 lit_pos_;        // literal-level cursor
  u32 accum_esc_len_;  // running escape-prefix byte count for the current segment
};

}  // namespace aho_corasick
