#pragma once

#include "common/constant.h"
#include "common/typedef.h"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

namespace aho_corasick {

class KMPAlgorithm {
 public:
  static constexpr u64 INVALID_POS = std::string::npos;

  // Pre-process the LPS (longest prefix suffix, i.e., longest prefix that is also suffix) table
  KMPAlgorithm(const std::string_view &pattern) {
    lps_table_.reserve(pattern.size());
    lps_table_[0] = 0;
    for (auto idx = 1UL; idx < pattern.size(); idx++) {
      auto k = lps_table_[idx - 1];
      while (k > 0 & pattern[idx] != pattern[k]) { k = lps_table_[k - 1]; }
      if (pattern[idx] == pattern[k]) { k++; }
      lps_table_[idx] = k;
    }
  }

  // Actual KMP algorithm. Return the first matching position instead of all possible positions
  auto Match(const std::string_view &pattern, const std::string &text) -> u64 {
    auto match_pos = 0UL;
    for (auto idx = 0UL; idx < text.size(); idx++) {
      while (match_pos > 0 && pattern[match_pos] != text[idx]) { match_pos = lps_table_[match_pos - 1]; }
      if (pattern[match_pos] == text[idx]) { match_pos++; }
      if (match_pos == pattern.size()) { return match_pos; }
    }
    return INVALID_POS;
  }

 private:
  std::vector<u64> lps_table_;
};

}  // namespace aho_corasick