// hn_bench — drive prototype-string AC vs DuckDB-recursive on workloads
// emitted by `hackernews-processing` (sibling repo).
//
// Each workload directory contains:
//   texts.csv     (text_id BIGINT, text VARCHAR)
//   patterns.csv  (pattern_id BIGINT, pattern VARCHAR)
//   labels.csv    (text_id, pattern_id)   -- ground truth from DuckDB
//   meta.json     -- workload metadata
//
// Output (written to <workload_dir>/timing.csv):
//   workload,impl,rows,patterns,total_text_bytes,build_ms,probe_ms,
//   match_count,rows_per_s,mb_per_s
//
// This binary makes NO changes to the AC core. It just ingests CSV and
// runs the same kernels the unit tests already exercise.

#include "aho_corasick/aho_corasick.h"
#include "aho_corasick/parser.h"
#include "aho_corasick/skeleton.h"
#include "common/utf8.h"
#include "csv.h"
#include "fmt/format.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <utility>
#include <vector>

#ifdef HN_BENCH_HAVE_DUCKDB
#include <duckdb.h>
#endif

using aho_corasick::AhoCorasick;
using aho_corasick::PatternAnalyzer;
using aho_corasick::Skeleton;
using aho_corasick::TextParserIterator;
using aho_corasick::Tokenizer;
using namespace std::string_view_literals;
namespace fs = std::filesystem;
using bench_clock = std::chrono::steady_clock;

// ---------------------------------------------------------------------------
// Reference matcher — extends test/matcher.h::DuckDBMatching with an optional
// single-codepoint escape character (SQL ESCAPE clause). When `esc == 0`,
// behaviour is identical to the LIKE-only original. When `esc != 0`, a
// pattern codepoint equal to `esc` makes the *next* codepoint literal —
// stripping any wildcard meaning from `%`, `_`, or the escape character
// itself.
// ---------------------------------------------------------------------------

static bool DuckDBMatching(const char *sdata, size_t slen,
                           const char *pdata, size_t plen,
                           uint32_t esc = 0) {
  size_t pidx = 0;
  size_t sidx = 0;
  for (; pidx < plen && sidx < slen;) {
    auto pchar = umbra::Utf8::readCodePoint(&pdata[pidx], pdata + plen);
    auto schar = umbra::Utf8::readCodePoint(&sdata[sidx], sdata + slen);
    bool is_literal = false;

    if (esc != 0 && pchar.codePoint == esc) {
      // SQL ESCAPE: drop the escape, take the next codepoint as literal.
      size_t next_pidx = pchar.next - pdata;
      if (next_pidx >= plen) { return false; }  // trailing escape: malformed
      pchar = umbra::Utf8::readCodePoint(&pdata[next_pidx], pdata + plen);
      pidx = next_pidx;
      is_literal = true;
    }

    if (!is_literal && pchar.codePoint == Tokenizer::UNDERSCORE) {
      pidx = pchar.next - pdata;
      sidx = schar.next - sdata;
    } else if (!is_literal && pchar.codePoint == Tokenizer::PERCENTAGE) {
      while (pidx < plen && pchar.codePoint == Tokenizer::PERCENTAGE) {
        pidx  = pchar.next - pdata;
        pchar = umbra::Utf8::readCodePoint(&pdata[pidx], pdata + plen);
      }
      if (pidx == plen) { return true; }
      for (; sidx < slen;) {
        if (DuckDBMatching(sdata + sidx, slen - sidx, pdata + pidx, plen - pidx, esc)) { return true; }
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
  // Tail must be either consumed or contain only `%` (after optional escapes).
  while (pidx < plen) {
    auto pchar = umbra::Utf8::readCodePoint(&pdata[pidx], pdata + plen);
    if (esc != 0 && pchar.codePoint == esc) { return false; }  // escape + missing tail char
    if (pchar.codePoint != Tokenizer::PERCENTAGE) { return false; }
    pidx = pchar.next - pdata;
  }
  return pidx == plen && sidx == slen;
}

// ---------------------------------------------------------------------------
// CSV ingest (vincentlaucsb/csv-parser, header-only in csv.h)
// ---------------------------------------------------------------------------

struct Workload {
  std::string                          name;
  std::vector<int64_t>                 text_ids;
  std::vector<std::string>             texts;     // probe side
  std::vector<int64_t>                 pattern_ids;
  std::vector<std::string>             patterns;  // build side (non-const for ART insert)
  std::vector<std::pair<int64_t,int64_t>> labels;  // (text_id, pattern_id) ground truth
  size_t                               total_text_bytes = 0;
  bool                                 has_labels = false;
  // meta.json fields
  std::string                          op;        // "LIKE" or "ILIKE" (upper)
  std::string                          escape;    // empty or single codepoint (UTF-8)
  uint32_t                             esc_cp = 0;  // decoded escape codepoint (0 = none)
};

// ---------------------------------------------------------------------------
// Tiny meta.json field extractor — pulls a single top-level "key": "value"
// string field. Handles JSON `\\` → `\` and `\"` → `"` unescapes. Returns
// empty when the key is absent.
// ---------------------------------------------------------------------------
static auto json_field(std::string_view doc, std::string_view key) -> std::string {
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
  for (size_t i = qopen + 1; i < doc.size(); ++i) {
    char c = doc[i];
    if (c == '\\' && i + 1 < doc.size()) {
      char n = doc[i + 1];
      if (n == '\\') { out.push_back('\\'); ++i; }
      else if (n == '"') { out.push_back('"'); ++i; }
      else if (n == 'n') { out.push_back('\n'); ++i; }
      else if (n == 't') { out.push_back('\t'); ++i; }
      else { out.push_back(c); }
    } else if (c == '"') {
      return out;
    } else {
      out.push_back(c);
    }
  }
  return out;
}

static auto load_meta(const fs::path &dir, Workload &w) -> void {
  auto path = dir / "meta.json";
  if (!fs::exists(path)) { return; }
  std::ifstream f(path);
  std::string doc((std::istreambuf_iterator<char>(f)), {});
  w.op     = json_field(doc, "operator");
  w.escape = json_field(doc, "escape_char");
  // Upper-case op for case-insensitive checks.
  for (auto &c : w.op) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  if (!w.escape.empty()) {
    auto cp = umbra::Utf8::readCodePoint(w.escape.data(),
                                         w.escape.data() + w.escape.size());
    w.esc_cp = cp.codePoint;
  }
}

// ASCII-only in-place lowercase. Q6 patterns are all ASCII; for full Unicode
// case folding we'd need ICU. Marked as a known limitation.
static auto ascii_lower_inplace(std::string &s) -> void {
  for (auto &c : s) {
    auto u = static_cast<unsigned char>(c);
    if (u >= 'A' && u <= 'Z') c = static_cast<char>(u + 32);
  }
}

static auto load_texts(const fs::path &csv_path,
                       std::vector<int64_t> &ids,
                       std::vector<std::string> &out) -> size_t {
  csv::CSVReader reader(csv_path.string());
  size_t total = 0;
  for (auto &row : reader) {
    ids.push_back(row["text_id"].get<int64_t>());
    auto text = row["text"].get<std::string>();
    total += text.size();
    out.push_back(std::move(text));
  }
  return total;
}

static void load_patterns(const fs::path &csv_path,
                          std::vector<int64_t> &ids,
                          std::vector<std::string> &out) {
  csv::CSVReader reader(csv_path.string());
  for (auto &row : reader) {
    ids.push_back(row["pattern_id"].get<int64_t>());
    out.push_back(row["pattern"].get<std::string>());
  }
}

static void load_labels(const fs::path &csv_path,
                        std::vector<std::pair<int64_t,int64_t>> &out) {
  csv::CSVReader reader(csv_path.string());
  for (auto &row : reader) {
    out.emplace_back(row["text_id"].get<int64_t>(), row["pattern_id"].get<int64_t>());
  }
}

static auto load_workload(const fs::path &dir) -> Workload {
  Workload w;
  w.name = dir.filename().string();
  w.total_text_bytes = load_texts(dir / "texts.csv", w.text_ids, w.texts);
  load_patterns(dir / "patterns.csv", w.pattern_ids, w.patterns);
  auto labels_csv = dir / "labels.csv";
  if (fs::exists(labels_csv)) {
    load_labels(labels_csv, w.labels);
    w.has_labels = true;
  }
  load_meta(dir, w);
  // ILIKE: lower-case patterns and texts (ASCII-only fold). All current
  // Q6 patterns are ASCII; document the limitation.
  if (w.op == "ILIKE") {
    for (auto &p : w.patterns) ascii_lower_inplace(p);
    for (auto &t : w.texts) ascii_lower_inplace(t);
    w.total_text_bytes = 0;
    for (const auto &t : w.texts) w.total_text_bytes += t.size();
  }
  return w;
}

// ---------------------------------------------------------------------------
// AC build + probe
// ---------------------------------------------------------------------------

struct AcBuild {
  std::unique_ptr<AhoCorasick>     trie;
  std::unique_ptr<PatternAnalyzer> build_side;
  std::chrono::nanoseconds         build_ns{0};
};

static auto build_ac(std::vector<std::string> &patterns,
                     std::string_view escape_str) -> AcBuild {
  AcBuild b;
  b.trie = std::make_unique<AhoCorasick>();
  auto t0 = bench_clock::now();
  b.build_side = std::make_unique<PatternAnalyzer>(patterns, escape_str, *b.trie);
  b.trie->BuildSuffixLink();
  b.build_ns = bench_clock::now() - t0;
  return b;
}

static auto ac_probe_one(const AcBuild &b, const std::string &text,
                         std::vector<bool> &result) -> void {
  std::fill(result.begin(), result.end(), false);

  // Special branch: wildcard-only patterns (rare; covers `%` / `%%`).
  for (size_t i = 0; i < b.build_side->Size(); i++) {
    const auto &sk = b.build_side->GetSkeleton(i);
    if (sk.IsEmpty() || sk.OnlyWildcard()) {
      // Wildcard-only: matches non-empty text iff text non-empty.
      result[i] = !text.empty();
    }
  }

  TextParserIterator iter(text.data(), text.size(),
                          b.build_side.get(), b.trie.get());
  while (iter.CanAdvanceOneCodePoint()) {
    iter.IterateOneCodePoint(result);
    iter.ProcessDelayedMatching();
  }
}

static auto duckdb_probe_one(const std::vector<std::string> &patterns,
                             const std::string &text,
                             std::vector<bool> &result,
                             uint32_t esc_cp) -> void {
  for (size_t i = 0; i < patterns.size(); i++) {
    result[i] = DuckDBMatching(text.data(), text.size(),
                               patterns[i].data(), patterns[i].size(), esc_cp);
  }
}

// ---------------------------------------------------------------------------
// Runner: best-of-N timing, match-count tally
// ---------------------------------------------------------------------------

struct RunStats {
  std::chrono::nanoseconds build_ns{0};
  std::chrono::nanoseconds probe_ns{0};
  size_t                   matches = 0;
  size_t                   max_rss_kb = 0;
};

static auto rss_kb() -> size_t {
  rusage ru{};
  getrusage(RUSAGE_SELF, &ru);
#ifdef __APPLE__
  // macOS reports ru_maxrss in bytes.
  return static_cast<size_t>(ru.ru_maxrss / 1024);
#else
  // Linux reports kilobytes.
  return static_cast<size_t>(ru.ru_maxrss);
#endif
}

static auto run_ac(Workload &w, int repeats) -> RunStats {
  RunStats best{};
  best.probe_ns = std::chrono::nanoseconds::max();
  std::string_view escape_str = w.escape;
  for (int r = 0; r < repeats; r++) {
    auto b = build_ac(w.patterns, escape_str);
    std::vector<bool> result(w.patterns.size(), false);
    size_t matches = 0;
    auto t0 = bench_clock::now();
    for (const auto &t : w.texts) {
      ac_probe_one(b, t, result);
      for (auto v : result) matches += v ? 1 : 0;
    }
    auto probe = bench_clock::now() - t0;
    if (probe < best.probe_ns) {
      best.probe_ns = probe;
      best.build_ns = b.build_ns;
      best.matches  = matches;
      best.max_rss_kb = rss_kb();
    }
  }
  return best;
}

// ---------------------------------------------------------------------------
// DuckDB-engine baseline (real SQL engine, in-process)
//
// Builds an in-memory DuckDB instance, bulk-loads texts and patterns via the
// Appender API, then times a single LIKE-join query:
//
//     SELECT COUNT(*)
//       FROM texts t JOIN patterns p
//         ON t.text LIKE p.pattern [ESCAPE '<c>']
//
// (ILIKE workloads use ILIKE; ESCAPE workloads pass the escape character.)
// build_ns covers connection + table create + bulk-load; probe_ns covers the
// join query end-to-end (including aggregation).
// ---------------------------------------------------------------------------

#ifdef HN_BENCH_HAVE_DUCKDB

static auto duckdb_check(duckdb_state s, const char *what) -> void {
  if (s != DuckDBSuccess) {
    fmt::print(stderr, "DuckDB error in {}\n", what);
    std::exit(3);
  }
}

static auto sql_string_escape(std::string_view in) -> std::string {
  std::string out;
  out.reserve(in.size() + 2);
  out.push_back('\'');
  for (char c : in) {
    if (c == '\'') out.push_back('\'');
    out.push_back(c);
  }
  out.push_back('\'');
  return out;
}

static auto run_duckdb_engine(const Workload &w, int repeats) -> RunStats {
  RunStats best{};
  best.probe_ns = std::chrono::nanoseconds::max();

  const bool is_ilike  = (w.op == "ILIKE");
  const bool has_esc   = !w.escape.empty();
  const std::string op = is_ilike ? "ILIKE" : "LIKE";

  std::string join_sql =
      fmt::format("SELECT COUNT(*) FROM texts t JOIN patterns p ON t.text {} p.pattern",
                  op);
  if (has_esc) {
    join_sql += " ESCAPE ";
    join_sql += sql_string_escape(w.escape);
  }

  for (int r = 0; r < repeats; r++) {
    duckdb_database db{};
    duckdb_connection con{};
    duckdb_check(duckdb_open(nullptr, &db), "open");
    duckdb_check(duckdb_connect(db, &con), "connect");

    auto build_t0 = bench_clock::now();
    duckdb_result tmp{};
    duckdb_check(duckdb_query(con,
                              "CREATE TABLE texts(text_id BIGINT, text VARCHAR);"
                              "CREATE TABLE patterns(pattern_id BIGINT, pattern VARCHAR);",
                              &tmp),
                 "create tables");
    duckdb_destroy_result(&tmp);

    duckdb_appender a_texts{}, a_pats{};
    duckdb_check(duckdb_appender_create(con, nullptr, "texts", &a_texts), "appender texts");
    for (size_t i = 0; i < w.texts.size(); ++i) {
      duckdb_check(duckdb_append_int64(a_texts, w.text_ids[i]), "append text_id");
      duckdb_check(duckdb_append_varchar_length(a_texts, w.texts[i].data(),
                                                static_cast<idx_t>(w.texts[i].size())),
                   "append text");
      duckdb_check(duckdb_appender_end_row(a_texts), "end_row text");
    }
    duckdb_check(duckdb_appender_destroy(&a_texts), "destroy appender texts");

    duckdb_check(duckdb_appender_create(con, nullptr, "patterns", &a_pats), "appender patterns");
    for (size_t i = 0; i < w.patterns.size(); ++i) {
      duckdb_check(duckdb_append_int64(a_pats, w.pattern_ids[i]), "append pat_id");
      duckdb_check(duckdb_append_varchar_length(a_pats, w.patterns[i].data(),
                                                static_cast<idx_t>(w.patterns[i].size())),
                   "append pat");
      duckdb_check(duckdb_appender_end_row(a_pats), "end_row pat");
    }
    duckdb_check(duckdb_appender_destroy(&a_pats), "destroy appender patterns");
    auto build_ns = bench_clock::now() - build_t0;

    auto probe_t0 = bench_clock::now();
    duckdb_result res{};
    duckdb_check(duckdb_query(con, join_sql.c_str(), &res), "join query");
    auto probe_ns = bench_clock::now() - probe_t0;

    size_t matches = static_cast<size_t>(duckdb_value_int64(&res, 0, 0));
    duckdb_destroy_result(&res);
    duckdb_disconnect(&con);
    duckdb_close(&db);

    if (probe_ns < best.probe_ns) {
      best.probe_ns   = probe_ns;
      best.build_ns   = build_ns;
      best.matches    = matches;
      best.max_rss_kb = rss_kb();
    }
  }
  return best;
}

#endif  // HN_BENCH_HAVE_DUCKDB

static auto run_duckdb(Workload &w, int repeats) -> RunStats {
  RunStats best{};
  best.probe_ns = std::chrono::nanoseconds::max();
  for (int r = 0; r < repeats; r++) {
    std::vector<bool> result(w.patterns.size(), false);
    size_t matches = 0;
    auto t0 = bench_clock::now();
    for (const auto &t : w.texts) {
      duckdb_probe_one(w.patterns, t, result, w.esc_cp);
      for (auto v : result) matches += v ? 1 : 0;
    }
    auto probe = bench_clock::now() - t0;
    if (probe < best.probe_ns) {
      best.probe_ns = probe;
      best.matches  = matches;
      best.max_rss_kb = rss_kb();
    }
  }
  return best;
}

// ---------------------------------------------------------------------------
// Output
// ---------------------------------------------------------------------------

static auto ms_of(std::chrono::nanoseconds ns) -> double {
  return static_cast<double>(ns.count()) / 1.0e6;
}

static auto write_timing(const fs::path &out_path, const Workload &w,
                         const RunStats &ac, const RunStats &dd,
                         const RunStats *de) -> void {
  std::ofstream f(out_path);
  f << "workload,impl,rows,patterns,total_text_bytes,build_ms,probe_ms,"
       "match_count,rows_per_s,mb_per_s,max_rss_kb\n";
  auto emit_row = [&](const char *impl, const RunStats &s) {
    auto rps = static_cast<double>(w.texts.size()) /
               (static_cast<double>(s.probe_ns.count()) / 1.0e9);
    auto mbps = (static_cast<double>(w.total_text_bytes) / 1.0e6) /
                (static_cast<double>(s.probe_ns.count()) / 1.0e9);
    f << w.name << ',' << impl << ',' << w.texts.size() << ','
      << w.patterns.size() << ',' << w.total_text_bytes << ','
      << fmt::format("{:.3f}", ms_of(s.build_ns)) << ','
      << fmt::format("{:.3f}", ms_of(s.probe_ns)) << ','
      << s.matches << ','
      << fmt::format("{:.1f}", rps) << ','
      << fmt::format("{:.3f}", mbps) << ','
      << s.max_rss_kb << '\n';
  };
  emit_row("AC", ac);
  emit_row("DuckDB-recursive", dd);
  if (de != nullptr) emit_row("DuckDB-engine", *de);
}

static auto print_console(const Workload &w, const RunStats &ac, const RunStats &dd,
                          const RunStats *de) -> void {
  fmt::print("{:<28} {:>8} {:>8} {:>10} {:>10} {:>12} {:>12} {:>12} {:>10} {:>10} {:>10} {:>8} {:>8}\n",
             "workload", "rows", "M", "MB", "build_ms",
             "AC_probe_ms", "DDrec_ms", "DDeng_ms",
             "AC_match", "DDrec_match", "DDeng_match",
             "vs_rec", "vs_eng");
  double mb = static_cast<double>(w.total_text_bytes) / 1.0e6;
  double sp_rec = static_cast<double>(dd.probe_ns.count()) /
                  static_cast<double>(ac.probe_ns.count());
  double sp_eng = (de != nullptr)
                  ? static_cast<double>(de->probe_ns.count()) /
                    static_cast<double>(ac.probe_ns.count())
                  : 0.0;
  fmt::print("{:<28} {:>8} {:>8} {:>10.2f} {:>10.3f} {:>12.3f} {:>12.3f} {:>12} "
             "{:>10} {:>10} {:>10} {:>7.2f}x {:>7}\n",
             w.name, w.texts.size(), w.patterns.size(), mb,
             ms_of(ac.build_ns), ms_of(ac.probe_ns), ms_of(dd.probe_ns),
             (de != nullptr) ? fmt::format("{:.3f}", ms_of(de->probe_ns)) : "n/a",
             ac.matches, dd.matches,
             (de != nullptr) ? fmt::format("{}", de->matches) : "n/a",
             sp_rec,
             (de != nullptr) ? fmt::format("{:.2f}x", sp_eng) : "n/a");
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char **argv) {
  if (argc < 2) {
    fmt::print(stderr,
               "usage: hn_bench <workload_dir> [<workload_dir> ...]\n");
    return 2;
  }
  int repeats = 3;
  if (const char *r = std::getenv("HN_BENCH_REPEATS")) {
    repeats = std::atoi(r);
    if (repeats < 1) repeats = 1;
  }

  for (int i = 1; i < argc; i++) {
    fs::path dir = argv[i];
    if (!fs::is_directory(dir)) {
      fmt::print(stderr, "skip {}: not a directory\n", dir.string());
      continue;
    }
    auto w = load_workload(dir);
    if (w.texts.empty() || w.patterns.empty()) {
      fmt::print(stderr, "skip {}: empty texts or patterns\n", dir.string());
      continue;
    }
    auto ac = run_ac(w, repeats);
    auto dd = run_duckdb(w, repeats);
#ifdef HN_BENCH_HAVE_DUCKDB
    RunStats de = run_duckdb_engine(w, repeats);
    const RunStats *de_ptr = &de;
#else
    const RunStats *de_ptr = nullptr;
#endif
    print_console(w, ac, dd, de_ptr);
    write_timing(dir / "timing.csv", w, ac, dd, de_ptr);
    if (w.has_labels) {
      // Cross-check: AC match count must equal DuckDB-engine label count.
      size_t expected = w.labels.size();
      bool mismatch = (ac.matches != expected) || (dd.matches != expected) ||
                      (de_ptr != nullptr && de_ptr->matches != expected);
      if (mismatch) {
        fmt::print(stderr,
                   "!! label mismatch on {}: labels.csv={}, AC={}, DDrec={}, DDeng={}\n",
                   w.name, expected, ac.matches, dd.matches,
                   (de_ptr != nullptr) ? fmt::format("{}", de_ptr->matches) : "n/a");
      }
    }
    // Optional: dump AC-vs-DDrec disagreement pairs to <dir>/disagree.csv.
    if (std::getenv("HN_BENCH_DUMP_DIFF") != nullptr) {
      std::ofstream df(dir / "disagree.csv");
      df << "text_id,pattern_id,pattern,ac,dd,text_excerpt\n";
      auto build = build_ac(w.patterns, w.escape);
      std::vector<bool> ac_r(w.patterns.size(), false);
      std::vector<bool> dd_r(w.patterns.size(), false);
      for (size_t ti = 0; ti < w.texts.size(); ++ti) {
        ac_probe_one(build, w.texts[ti], ac_r);
        duckdb_probe_one(w.patterns, w.texts[ti], dd_r, w.esc_cp);
        for (size_t pi = 0; pi < w.patterns.size(); ++pi) {
          if (ac_r[pi] != dd_r[pi]) {
            auto excerpt = w.texts[ti].substr(0, std::min<size_t>(160, w.texts[ti].size()));
            std::replace(excerpt.begin(), excerpt.end(), '\n', ' ');
            std::replace(excerpt.begin(), excerpt.end(), '"', '\'');
            df << w.text_ids[ti] << ',' << w.pattern_ids[pi] << ",\""
               << w.patterns[pi] << "\"," << ac_r[pi] << ',' << dd_r[pi]
               << ",\"" << excerpt << "\"\n";
          }
        }
      }
    }
  }
  return 0;
}
