#include <algorithm>
#include <bitset>
#include <climits>
#include <format>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "aho_corasick/aho_corasick.h"
#include "kmp/kmp.h"

#include "csv.h"
#include "fmt/format.h"
#include "join_strings.h"
#include "perf_event.h"
#include "roaring/roaring.hh"
#include "third_party/succinct/elias_fano.hpp"

#define SET_BIT(bv, index) (bv).add(index)
#define ITERATE_CHECK(bv)                                            \
  ({                                                                 \
    for (auto row_index : (bv)) {                                    \
      auto &row = data[row_index];                                   \
      if (row.title.contains(joinstr)) { result.emplace_back(row); } \
    }                                                                \
  })
#define GET_SIZE_IN_BYTES(bv) ((bv).getSizeInBytes())

using InvertedIndex               = std::array<roaring::Roaring, 256>;
using FingerprintType             = uint8_t;
constexpr size_t NGRAM_SIZE       = 3;
constexpr size_t FINGERPRINT_SIZE = sizeof(FingerprintType) * CHAR_BIT;

// TODO: Prototype with least-frequent characters and half-half (8 most-freq and 8 least-freq)
constexpr uint8_t init_character_value(char c) {
  if (c >= 'A' && c <= 'Z') { c = c - 'A' + 'a'; }
  switch (c) {
    case 'e': return 1 << 7;
    case 't': return 1 << 6;
    case 'a': return 1 << 5;
    case 'o': return 1 << 4;
    case 'i': return 1 << 3;
    case 'n': return 1 << 2;
    case 's': return 1 << 1;
    case 'h': return 1 << 0;
    case 'r': return 1 << 0;
    case 'd': return 1 << 1;
    case 'l': return 1 << 2;
    case 'c': return 1 << 3;
    case 'u': return 1 << 4;
    case 'm': return 1 << 5;
    case 'w': return 1 << 6;
    case 'f': return 1 << 7;
    default: return 0;
  }
}

constexpr std::array<uint8_t, 256> CHARACTER_MAPPING = []() {
  std::array<uint8_t, 256> arr = {};
  for (auto i = 0U; i < 256; ++i) { arr[i] = init_character_value(static_cast<char>(i)); }
  return arr;
}();

const std::vector<std::string> TITLE_COLUMNS = {
  "id",        "title",      "imdb_index",   "kind_id", "production_year", "imdb_id", "phonetic_code", "episode_of_id",
  "season_nr", "episode_nr", "series_years", "md5sum"};

struct Title {
  uint64_t id;                // integer NOT NULL PRIMARY KEY
  std::string title;          // character varying NOT NULL
  std::string imdb_index;     // character varying(5)
  uint64_t kind_id;           // integer NOT NULL
  uint64_t production_year;   // integer
  uint64_t imdb_id;           // integer
  std::string phonetic_code;  // character varying(5)
  uint64_t episode_of_id;     // integer
  uint64_t season_nr;         // integer
  uint64_t episode_nr;        // integer
  std::string series_years;   // character varying(49)
  std::string md5sum;         // character varying(32)
  FingerprintType fp;         // 1-byte fingerprint
  bool was_indexed;           // whether it was indexed into the inverted index or not

  auto ToString() const -> std::string {
    return std::format(
      "Title{{id={}, title='{}', imdb_index='{}', kind_id={}, production_year={}, "
      "imdb_id={}, phonetic_code='{}', episode_of_id={}, season_nr={}, episode_nr={}, "
      "series_years='{}', md5sum='{}'}}",
      id, title, imdb_index, kind_id, production_year, imdb_id, phonetic_code, episode_of_id, season_nr, episode_nr,
      series_years, md5sum);
  }
};

inline auto CalculateFingerprint(std::string_view str) -> FingerprintType {
  FingerprintType fp = 0;
  for (auto &c : str) { fp |= CHARACTER_MAPPING[static_cast<unsigned char>(c)]; }
  return fp;
}

inline auto NgramFingerprint(const char *str, size_t offset) -> FingerprintType {
  return CHARACTER_MAPPING[static_cast<unsigned char>(str[offset])] |
         CHARACTER_MAPPING[static_cast<unsigned char>(str[offset + 1])] |
         CHARACTER_MAPPING[static_cast<unsigned char>(str[offset + 2])];
}

void IndexNgram(const Title &row, size_t row_idx, InvertedIndex &invert) {
  for (auto i = 0; i + NGRAM_SIZE <= row.title.size(); ++i) {
    auto ngram_fp = NgramFingerprint(row.title.data(), i);
    SET_BIT(invert[ngram_fp], row_idx);
  }
}

auto EnvOr(const char *env, uint64_t value) -> uint64_t {
  if (getenv(env)) return atof(getenv(env));
  return value;
}

void ProcessIndexedRows(std::vector<Title> &data, std::vector<Title> &result, std::string_view joinstr,
                        InvertedIndex &hashtable, FingerprintType substring_fp) {
  auto prefix_fp        = NgramFingerprint(joinstr.data(), 0);
  auto &prefix_row_sets = hashtable[prefix_fp];
  if (joinstr.length() > NGRAM_SIZE) {
    auto new_set = prefix_row_sets;
    for (auto idx = 1; idx <= joinstr.length() - NGRAM_SIZE; idx++) {
      new_set &= hashtable[NgramFingerprint(joinstr.data(), idx)];
    }
    ITERATE_CHECK(new_set);
  } else {
    ITERATE_CHECK(prefix_row_sets);
  }
}

void ProcessNonindexedRows(std::vector<Title> &data, std::vector<Title> &result, std::string_view joinstr,
                           InvertedIndex &hashtable, FingerprintType substring_fp) {
  auto data_size = data.size();
  for (auto row_index = 0; row_index < data_size; row_index++) {
    auto &row = data[row_index];
    if (!row.was_indexed) {
      if ((row.fp & substring_fp) == substring_fp) {
        row.was_indexed = true;
        auto found      = false;
        for (auto ngram_i = 0; ngram_i < row.title.size() - NGRAM_SIZE + 1; ngram_i++) {
          auto fp = NgramFingerprint(row.title.data(), ngram_i);
          SET_BIT(hashtable[fp], row_index);
          if (!found && ngram_i + joinstr.size() <= row.title.size()) {
            if (std::memcmp(row.title.data() + ngram_i, joinstr.data(), joinstr.size()) == 0) { found = true; }
          }
        }
        if (found) { result.emplace_back(row); }
      }
    }
  }
}

/**
 * Implemented variant:
 * 0: Naive join: scan every row, check if row.title contains the join substring normally
 * 1: Fingerprint join: scan every row, check if row.fp match join substring'fp.
 *      If the match is true, then proceed with substring check similarly to naive join
 * 2: Lazy inverted index join, as explained in README.md
 * 3: Aho-Corasick-based
 *
 * TODO:
 * - Implement a KMP variant, a SIMD-substring-search variant.
 * - Refactor this prototype to wildcard instead of substring
 */
enum BenchmarkVariant : u8 {
  NESTED_LOOP_JOIN         = 0,  // Naive nested loop join
  NESTED_LOOP_JOIN_WITH_FP = 1,  // Naive nested loop join, using 1B fingerprint as a cheap filter before actual join
  INVERTED_INDEX_FP        = 2,  // Bitmap-based inverted index approach, using 1B fingerprint per trigram
  AHO_CORASICK             = 3,  // MAIN: Aho-Corasick-based idea
  KMP                      = 4,  // Knuth-Morris-Pratt: Similar to Aho-Corasick, but with per-string KMP
  KMP_WITH_FP              = 5,  // TODO
  SIMD                     = 6,  // TODO
  SIMD_WITH_FP             = 7,  // TODO
};

int main() {
  csv::CSVFormat format;
  format.delimiter(',').quote('"').column_names(TITLE_COLUMNS);
  auto input = csv::CSVReader("../imdb/title.csv", format);

  std::vector<Title> data;
  size_t row_count = 0;
  for (auto &row : input) {
    if (++row_count % 100000 == 0) { std::cout << "Processed " << row_count << " rows" << std::endl; }
    Title next_row{row["id"].get<uint64_t>(),
                   row["title"].get<std::string>(),
                   row["imdb_index"].get<std::string>(),
                   row["kind_id"].get<uint64_t>(),
                   row["production_year"].is_null() ? 0 : row["production_year"].get<uint64_t>(),
                   row["imdb_id"].is_null() ? 0 : row["imdb_id"].get<uint64_t>(),
                   row["phonetic_code"].get<std::string>(),
                   row["episode_of_id"].is_null() ? 0 : row["episode_of_id"].get<uint64_t>(),
                   row["season_nr"].is_null() ? 0 : row["season_nr"].get<uint64_t>(),
                   row["episode_nr"].is_null() ? 0 : row["episode_nr"].get<uint64_t>(),
                   row["series_years"].get<std::string>(),
                   row["md5sum"].get<std::string>(),
                   0,
                   false};
    next_row.fp = CalculateFingerprint(next_row.title);
    if (next_row.title.length() >= NGRAM_SIZE) { data.emplace_back(std::move(next_row)); }
  }
  fmt::println("Total number of rows: {}", row_count);

  // Calculate join substrings' fingerprint
  auto to_join_substrings =
    std::span(JOIN_SUBSTRINGS).subspan(0, std::min(EnvOr("NO_SUBSTR", JOIN_SUBSTRINGS.size()), JOIN_SUBSTRINGS.size()));
  std::vector<FingerprintType> join_fps             = {};
  std::unordered_map<FingerprintType, u64> grouping = {};
  for (auto &joinstr : to_join_substrings) {
    join_fps.emplace_back(CalculateFingerprint(joinstr));
    grouping[join_fps.back()] += 1;
  }

  // Variant InvertedIndex env
  InvertedIndex hashtable;
  std::generate(hashtable.begin(), hashtable.end(), [] { return roaring::Roaring(); });

  // Variant AhoCorasick env
  auto trie = aho_corasick::AhoCorasick();
  std::unordered_set<size_t> trie_result[to_join_substrings.size()];
  auto trie_local = trie.Local();

  // Benchmark env
  const auto variant     = EnvOr("VARIANT", 0);
  const auto num_threads = EnvOr("THREADS", 1);
  {
    PerfEventBlock perf(row_count);
    for (auto idx = 0; idx < to_join_substrings.size(); idx++) {
      auto joinstr = std::string_view(to_join_substrings[idx]);
      assert(joinstr.length() >= NGRAM_SIZE);
      auto substring_fp = join_fps[idx];
      std::vector<Title> result;

      switch (variant) {
        case BenchmarkVariant::NESTED_LOOP_JOIN: {
          for (auto &row : data) {
            if (row.title.contains(joinstr)) { result.emplace_back(row); }
          }
        } break;
        case BenchmarkVariant::NESTED_LOOP_JOIN_WITH_FP: {
          for (auto &row : data) {
            if ((row.fp & substring_fp) == substring_fp) {
              if (row.title.contains(joinstr)) { result.emplace_back(row); }
            }
          }
        } break;
        case BenchmarkVariant::INVERTED_INDEX_FP: {
          // Process all indexed rows
          ProcessIndexedRows(data, result, joinstr, hashtable, substring_fp);
          // Process all un-indexed rows
          ProcessNonindexedRows(data, result, joinstr, hashtable, substring_fp);
        } break;
        case BenchmarkVariant::AHO_CORASICK: {
          auto real_join_str = std::string(joinstr) + static_cast<char>(ART::NULL_TERMINATOR);
          trie.Insert(real_join_str.data(), real_join_str.size(),
                      aho_corasick::PatternIndexType(idx, 0, joinstr.size()), trie_local);
        } break;
        case BenchmarkVariant::KMP: {
          aho_corasick::KMPAlgorithm kmp(joinstr);
          for (auto &row : data) {
            if (kmp.Match(joinstr, row.title) != aho_corasick::KMPAlgorithm::INVALID_POS) { result.emplace_back(row); }
          }
        } break;
        case BenchmarkVariant::KMP_WITH_FP: {
          auto kmp = aho_corasick::KMPAlgorithm(joinstr);
          for (auto &row : data) {
            if ((row.fp & substring_fp) == substring_fp) {
              if (kmp.Match(joinstr, row.title) != aho_corasick::KMPAlgorithm::INVALID_POS) {
                result.emplace_back(row);
              }
            }
          }
        } break;
        default: throw std::runtime_error("Not yet supported");
      }

      if (variant != BenchmarkVariant::AHO_CORASICK) {
        if (EnvOr("DEBUG", 0)) { fmt::println("Join on string '{}' -- Number of rows: {}", joinstr, result.size()); }
      }
    }

    // Special handling for some variants
    if (variant == BenchmarkVariant::AHO_CORASICK) {
      trie.BuildSuffixLink(num_threads);
      for (auto row_index = 0; row_index < data.size(); row_index++) {
        auto &row             = data[row_index];
        auto per_row_matching = trie.ParseText(row.title.c_str(), row.title.size());
        for (auto &emit_pattern : per_row_matching) {
          trie_result[emit_pattern.pattern_index.pattern_id].insert(row_index);
        }
      }
    }
  }

  // Result report
  switch (variant) {
    case BenchmarkVariant::INVERTED_INDEX_FP: {
      auto non_indexed = 0UL;
      for (auto &row : data) { non_indexed += 1 - static_cast<uint64_t>(row.was_indexed); }
      auto memory_usage = 0UL;
      for (auto fp = 0; fp < 256; fp++) { memory_usage += GET_SIZE_IN_BYTES(hashtable[fp]); }
      fmt::println("Number of non-indexed rows: {} -- Total memory usage: {} MB", non_indexed,
                   memory_usage / (1024 * 1024));
    } break;
    case BenchmarkVariant::AHO_CORASICK: {
      if (EnvOr("DEBUG", 0)) {
        for (auto idx = 0; idx < to_join_substrings.size(); idx++) {
          fmt::println("Join on string '{}' -- Number of rows: {}", std::string_view(to_join_substrings[idx]),
                       trie_result[idx].size());
        }
      }
    }
    default: break;
  }
}
