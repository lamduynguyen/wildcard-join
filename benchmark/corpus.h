#pragma once

// One shape of workload for the benchmark to run, whichever end it came from.
//
// Two ends exist. `benchmark/workload.h` generates the synthetic sweeps from a
// seed, and the golden test in test/test_golden.cc checks the answers against
// reproducibility/expected. A directory emitted by the sibling
// hackernews-processing repo holds a real corpus, real patterns, and a
// labels.csv of ground truth produced by DuckDB.
//
// They used to be two structs in two binaries, so everything downstream of
// them was written twice: two probe loops, two verification routines, two
// console tables, two ideas about what a pair even is. A directory corpus has
// ids of its own and a synthetic one does not, and that is the only difference
// the code below the loaders should have to know about.

#include "benchmark/workload.h"

// Out of order and in a block of its own on purpose. common/utf8.h calls
// assert() without including <cassert>, and gets away with it everywhere else
// in the tree because something earlier in the include list happened to pull
// it in. That header is vendored, so the fix belongs upstream rather than in a
// local edit to a file we are trying to keep byte identical.
#include <cassert>

#include "common/utf8.h"
#include "csv.h"
#include "fmt/format.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bench {

struct Corpus {
  std::string name;
  std::vector<std::string> texts;     // probe side
  std::vector<std::string> patterns;  // build side, non-const for the ART insert

  // Empty means the row's position is its id, which is what the synthetic
  // workloads want. A directory corpus carries the ids the upstream query used
  // and they have to survive, because labels.csv is written in terms of them.
  std::vector<int64_t> text_ids;
  std::vector<int64_t> pattern_ids;

  // Ground truth, if the directory shipped any.
  std::vector<std::pair<int64_t, int64_t>> labels;
  bool has_labels = false;

  // SQL flavour. Synthetic workloads are always plain LIKE with no ESCAPE.
  bool ilike = false;
  std::string escape;   // UTF-8, empty for none
  uint32_t esc_cp = 0;  // the same thing decoded, 0 for none

  // ILIKE is answered by folding both sides and running LIKE, so `texts` and
  // `patterns` above are the folded ones. These hold what was on disk, and are
  // empty unless a fold happened. The engine baseline reads them and issues a
  // real ILIKE, which makes the AC against engine comparison a check on the
  // fold as well as on the automaton. Handing the engine the folded text and
  // asking it for ILIKE would have made the fold agree with itself.
  std::vector<std::string> raw_texts;
  std::vector<std::string> raw_patterns;

  [[nodiscard]] auto engine_texts() const -> const std::vector<std::string> & {
    return raw_texts.empty() ? texts : raw_texts;
  }

  [[nodiscard]] auto engine_patterns() const -> const std::vector<std::string> & {
    return raw_patterns.empty() ? patterns : raw_patterns;
  }

  size_t total_text_bytes = 0;
  // FNV-1a over both sides, non-zero only for the synthetic workloads, where
  // it is the thing reproducibility/expected is keyed on. A directory corpus
  // is identified by its path and its own meta.json.
  uint64_t digest = 0;

  [[nodiscard]] auto rows() const -> size_t { return texts.size(); }

  [[nodiscard]] auto m() const -> size_t { return patterns.size(); }

  [[nodiscard]] auto text_id(size_t i) const -> int64_t {
    return text_ids.empty() ? static_cast<int64_t>(i) : text_ids[i];
  }

  [[nodiscard]] auto pattern_id(size_t i) const -> int64_t {
    return pattern_ids.empty() ? static_cast<int64_t>(i) : pattern_ids[i];
  }
};

inline auto FromSpec(const workload::Spec &s) -> Corpus {
  auto w = workload::MakeWorkload(s);
  Corpus c;
  c.name             = std::move(w.name);
  c.texts            = std::move(w.texts);
  c.patterns         = std::move(w.patterns);
  c.total_text_bytes = w.total_text_bytes;
  c.digest           = w.digest;
  return c;
}

// A single top level "key": "value" string field out of meta.json, unescaping
// the four sequences that turn up in one. Not a JSON parser and not trying to
// be: the file has half a dozen flat string fields and pulling in a dependency
// to read them would be the larger change.
inline auto JsonField(std::string_view doc, std::string_view key) -> std::string {
  std::string needle = "\"";
  needle.append(key);
  needle.append("\"");
  auto k = doc.find(needle);
  if (k == std::string_view::npos) { return ""; }
  auto colon = doc.find(':', k + needle.size());
  if (colon == std::string_view::npos) { return ""; }
  auto qopen = doc.find('"', colon + 1);
  if (qopen == std::string_view::npos) { return ""; }
  std::string out;
  for (size_t i = qopen + 1; i < doc.size(); i++) {
    const char c = doc[i];
    if (c == '"') { return out; }
    if (c != '\\' || i + 1 >= doc.size()) {
      out.push_back(c);
      continue;
    }
    const char n = doc[i + 1];
    if (n == '\\' || n == '"') {
      out.push_back(n);
      i++;
    } else if (n == 'n') {
      out.push_back('\n');
      i++;
    } else if (n == 't') {
      out.push_back('\t');
      i++;
    } else {
      out.push_back(c);
    }
  }
  return out;
}

// ASCII only, in place. Full Unicode case folding needs ICU and the patterns
// these corpora carry are ASCII, so this is a documented limitation rather
// than an oversight: an ILIKE workload with non-ASCII patterns would fold
// wrong here and would fold right in DuckDB, and the verification would say so.
inline auto AsciiLowerInPlace(std::string &s) -> void {
  for (auto &c : s) {
    auto u = static_cast<unsigned char>(c);
    if (u >= 'A' && u <= 'Z') { c = static_cast<char>(u + 32); }
  }
}

inline auto FromDirectory(const std::filesystem::path &dir) -> Corpus {
  namespace fs = std::filesystem;
  Corpus c;
  c.name = dir.filename().string();

  csv::CSVReader texts((dir / "texts.csv").string());
  for (auto &row : texts) {
    c.text_ids.push_back(row["text_id"].get<int64_t>());
    c.texts.push_back(row["text"].get<std::string>());
  }
  csv::CSVReader patterns((dir / "patterns.csv").string());
  for (auto &row : patterns) {
    c.pattern_ids.push_back(row["pattern_id"].get<int64_t>());
    c.patterns.push_back(row["pattern"].get<std::string>());
  }
  const auto labels_csv = dir / "labels.csv";
  if (fs::exists(labels_csv)) {
    csv::CSVReader labels(labels_csv.string());
    for (auto &row : labels) { c.labels.emplace_back(row["text_id"].get<int64_t>(), row["pattern_id"].get<int64_t>()); }
    c.has_labels = true;
  }

  const auto meta = dir / "meta.json";
  if (fs::exists(meta)) {
    std::ifstream f(meta);
    const std::string doc((std::istreambuf_iterator<char>(f)), {});
    auto op = JsonField(doc, "operator");
    for (auto &ch : op) { ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch))); }
    c.ilike  = (op == "ILIKE");
    c.escape = JsonField(doc, "escape_char");
    if (!c.escape.empty()) {
      c.esc_cp = umbra::Utf8::readCodePoint(c.escape.data(), c.escape.data() + c.escape.size()).codePoint;
    }
  }

  // The fold happens once here rather than per comparison. Keeping the
  // original is what lets the engine baseline be an independent check on it.
  if (c.ilike) {
    c.raw_texts    = c.texts;
    c.raw_patterns = c.patterns;
    for (auto &p : c.patterns) { AsciiLowerInPlace(p); }
    for (auto &t : c.texts) { AsciiLowerInPlace(t); }
  }

  // Folding is ASCII only and does not change any length, so this is the same
  // number either way.
  for (const auto &t : c.texts) { c.total_text_bytes += t.size(); }
  return c;
}

}  // namespace bench
