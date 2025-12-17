#pragma once

#include "aho_corasick/aho_corasick.h"
#include "art/epoche.h"
#include "art/tree.h"
#include "common/flat_map.h"
#include "common/typedef.h"
#include "common/util.h"

#include "gtest/gtest_prod.h"
#include "tbb/concurrent_vector.h"

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <unordered_set>
#include <vector>

namespace aho_corasick {

static constexpr auto PERCENTAGE = '%';
static constexpr auto UNDERSCORE = '_';

struct Token {
  u64 start;  // offset in the original string
  u64 len;

  static inline auto IsDelim(char c) { return c == UNDERSCORE || c == PERCENTAGE; };

  static auto NextToken(const char *s, size_t slen, std::size_t &pos) -> Token;
  static auto NextSegment(const char *s, size_t slen, std::size_t &pos) -> Token;
};

struct Skeleton {
  struct Segment {
    ska::flat_hash_map<u64, u64> prev_literal_offset;  // Map from literal's start pos to that of previous literal
    u64 last_literal_start_pos;
    u64 suffix_underscore_cnt;
    bool has_prefix_percent;
    bool has_suffix_percent;

    auto Contain(u64 start_pos) -> bool;
    auto IsFirstLiteral(u64 start_pos) -> bool;
    auto IsPreviousLiteral(u64 prev_start_pos, u64 start_pos) -> bool;
  };

  bool only_wildcard;
  std::vector<Segment> seg;

  Skeleton(const char *p, size_t plen, const std::function<void(Token &)> &literal_fn);
  ~Skeleton() = default;

  inline auto operator[](int idx) -> Segment & { return seg[idx]; }

  inline auto IsEmpty() -> bool { return seg.empty(); }

  inline auto Size() { return seg.size(); }

  inline auto Last() -> Segment & { return seg.back(); }

  static auto SpecialMatchEmptyPattern(size_t slen, const char *p, size_t plen) -> bool;
};

}  // namespace aho_corasick