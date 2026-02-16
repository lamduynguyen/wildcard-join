#pragma once

#include "common/typedef.h"

#include <cstdio>

namespace aho_corasick {

struct Tokenizer {
  static constexpr u32 WRONG_OFFSET = -1U;
  static constexpr auto PERCENTAGE  = '%';
  static constexpr auto UNDERSCORE  = '_';

  struct TextUnit {
    u32 start;
    u32 len;
  };

  static auto NextLiteral(const char *s, size_t slen, u32 &pos, u32 &max_underscore_cnt) -> TextUnit;
  static auto NextSegment(const char *s, size_t slen, u32 &pos) -> TextUnit;
};

}  // namespace aho_corasick