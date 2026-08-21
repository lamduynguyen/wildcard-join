// ac_bench: time the prototype-string Aho-Corasick LIKE join against two
// baselines, on either the synthetic sweeps or a corpus on disk.
//
// This is baseline_bench and hn_bench merged. They were separate binaries
// because they started from different data, and everything downstream of that
// difference got written twice: two probe loops, two verification routines,
// two console tables, two sets of environment variables standing in for a
// command line. The numbers they produced were not comparable, which is a
// strange property for two benchmarks of the same kernel.
//
// The baselines:
//
//   NLJ-recursive  a nested loop over the textbook recursive matcher in
//                  benchmark/reference.h. Same algorithm as DuckDB's
//                  TemplatedLikeOperator, but a loop around it is not DuckDB
//                  and a speedup against it is not a speedup against DuckDB.
//                  See #7.
//   DuckDB-engine  the real in-process engine. This is the one a speedup can
//                  honestly be quoted against. Optional at configure time,
//                  which is an M5 item to fix.
//
// The exit status is the point. Every number here is worthless if the three
// implementations do not produce the same set of (row, pattern) pairs, so a
// disagreement is a non-zero exit and not a line on stderr that a script will
// scroll past.

#include "benchmark/corpus.h"
#include "benchmark/engine.h"
#include "benchmark/probe.h"
#include "benchmark/reference.h"
#include "benchmark/rss.h"
#include "benchmark/workload.h"

#include "aho_corasick/parser.h"
#include "fmt/format.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef HN_BENCH_HAVE_DUCKDB
#include <duckdb.h>
#endif

using aho_corasick::TextParserIterator;
using bench::bench_clock;
using bench::Corpus;
namespace fs = std::filesystem;

namespace {

// ---------------------------------------------------------------------------
// Options

enum class Verify : uint8_t { kNone, kRecursive, kEngine, kAll };

struct Options {
  std::vector<std::string> workloads;  // built in names, empty means all
  std::vector<fs::path> dirs;          // corpora on disk
  // Which implementations to time. All three by default. This is separate
  // from --verify, which says what to check the answers against: asking for
  // only the AC timing is not a reason to stop checking that it is right.
  bool time_ac       = true;
  bool time_nlj      = true;
  bool time_engine   = true;
  int repeats        = 3;
  int engine_repeats = 1;

  // Untimed repetitions before the timed ones. Zero by default, so nothing is
  // discarded unless it was asked for, and when it is asked for the discarded
  // repetitions still appear in the JSON Lines output with warmup set.
  int warmup    = 0;
  Verify verify = Verify::kAll;
  fs::path csv_path;
  fs::path jsonl_path;
  bool quick = false;
};

constexpr std::string_view USAGE = R"(usage: ac_bench [options] [corpus_dir ...]

Runs the built in synthetic workloads when given no corpus directory, and the
directories otherwise. Both can be asked for in one invocation.

  --workload NAME     run one built in workload, repeatable. Default is all.
  --list              print the built in workload names and exit
  --impl NAME         time only these, repeatable: ac, nljrec, engine
  --repeats N         timed repetitions of AC and NLJ-recursive, best wins
  --engine-repeats N  timed repetitions of the DuckDB engine baseline
  --warmup N          untimed repetitions first. Default 0. Recorded, not hidden.
  --verify MODE       none, recursive, engine or all. Default all.
  --csv PATH          also write the reduced timing table there
  --jsonl PATH        write one record per repetition there, warm ups included
  --quick             one repetition, and only the three small workloads
  -h, --help          this

The console and the CSV report the fastest timed repetition. --jsonl is every
repetition, before the reduction, which is where the spread and the discarded
warm ups are.
)";

[[noreturn]] auto Die(std::string_view msg) -> void {
  fmt::print(stderr, "!! {}\n", msg);
  std::exit(2);
}

// strtol and not atoi. atoi returns 0 for anything it cannot parse and there
// is no way to tell that apart from a real 0, so a typo would silently clamp
// to one repetition and the run would look like it did what you asked.
auto ParsePositiveInt(std::string_view flag, const char *arg) -> int {
  char *end      = nullptr;
  const long val = std::strtol(arg, &end, 10);
  if (end == arg || *end != '\0' || val < 1 || val > 10000) {
    Die(fmt::format("{} wants a positive integer, got '{}'", flag, arg));
  }
  return static_cast<int>(val);
}

// Same parse with the lower bound moved, for the one flag where zero means
// something. Kept separate rather than passing a minimum, because everything
// else in here really does want to reject zero and a default argument would
// make that the easy thing to forget.
auto ParseNonNegativeInt(std::string_view flag, const char *arg) -> int {
  char *end      = nullptr;
  const long val = std::strtol(arg, &end, 10);
  if (end == arg || *end != '\0' || val < 0 || val > 10000) {
    Die(fmt::format("{} wants a non negative integer, got '{}'", flag, arg));
  }
  return static_cast<int>(val);
}

auto ListWorkloads() -> void {
  fmt::print("{:<20} {:>8} {:>10} {:>8}\n", "name", "rows", "patterns", "words");
  for (const auto &s : workload::BASELINE_SPECS) {
    fmt::print("{:<20} {:>8} {:>10} {:>8}\n", s.name, s.n_texts, s.n_patterns + s.n_wildcard_only, s.words_per_text);
  }
}

auto ParseArgs(int argc, char **argv) -> Options {
  Options o;
  int i         = 1;
  bool saw_impl = false;

  // Consumes the next argument, or dies naming the flag that wanted one.
  // Declared out here rather than inside the loop because it advances `i`,
  // which the loop then steps past.
  auto next = [&](std::string_view flag) -> const char * {
    if (i + 1 >= argc) { Die(fmt::format("{} wants a value", flag)); }
    return argv[++i];
  };

  for (; i < argc; i++) {
    const std::string_view a = argv[i];
    if (a == "-h" || a == "--help") {
      fmt::print("{}", USAGE);
      std::exit(0);
    } else if (a == "--list") {
      ListWorkloads();
      std::exit(0);
    } else if (a == "--workload") {
      o.workloads.emplace_back(next(a));
    } else if (a == "--impl") {
      // The first --impl clears the default of all three, so repeating the
      // flag adds rather than the last one winning.
      if (!saw_impl) {
        o.time_ac = o.time_nlj = o.time_engine = false;
        saw_impl                               = true;
      }
      const std::string_view impl = next(a);
      if (impl == "ac") {
        o.time_ac = true;
      } else if (impl == "nljrec") {
        o.time_nlj = true;
      } else if (impl == "engine") {
        o.time_engine = true;
      } else {
        Die(fmt::format("--impl wants ac, nljrec or engine, got '{}'", impl));
      }
    } else if (a == "--repeats") {
      o.repeats = ParsePositiveInt(a, next(a));
    } else if (a == "--engine-repeats") {
      o.engine_repeats = ParsePositiveInt(a, next(a));
    } else if (a == "--warmup") {
      // Zero is a legal answer here and it is the default, so this one cannot
      // go through ParsePositiveInt.
      o.warmup = ParseNonNegativeInt(a, next(a));
    } else if (a == "--jsonl") {
      o.jsonl_path = next(a);
    } else if (a == "--csv") {
      o.csv_path = next(a);
    } else if (a == "--quick") {
      o.quick = true;
    } else if (a == "--verify") {
      const std::string_view mode = next(a);
      if (mode == "none") {
        o.verify = Verify::kNone;
      } else if (mode == "recursive") {
        o.verify = Verify::kRecursive;
      } else if (mode == "engine") {
        o.verify = Verify::kEngine;
      } else if (mode == "all") {
        o.verify = Verify::kAll;
      } else {
        Die(fmt::format("--verify wants none, recursive, engine or all, got '{}'", mode));
      }
    } else if (a.starts_with("-")) {
      Die(fmt::format("unknown option '{}', try --help", a));
    } else {
      // Checked here rather than left to the CSV reader, which throws, and an
      // uncaught throw out of main is a terminate and an abort rather than the
      // exit code a script is reading.
      if (!fs::is_directory(a)) { Die(fmt::format("'{}' is not a directory", a)); }
      for (const auto *needed : {"texts.csv", "patterns.csv"}) {
        if (!fs::exists(fs::path(a) / needed)) { Die(fmt::format("'{}' has no {}", a, needed)); }
      }
      o.dirs.emplace_back(a);
    }
  }
  if (o.quick) {
    o.repeats        = 1;
    o.engine_repeats = 1;
    o.warmup         = 0;
  }

  // Both output files are opened once here and thrown away, so an unwritable
  // path fails now rather than after the run it was supposed to record. The
  // engine baseline on the full workload set is minutes, and finding out at
  // the end of it that the directory does not exist is a bad way to spend
  // them.
  for (const auto &p : {o.csv_path, o.jsonl_path}) {
    if (p.empty()) { continue; }
    const std::ofstream probe(p);
    if (!probe) { Die(fmt::format("cannot write {}", p.string())); }
  }
  return o;
}

// ---------------------------------------------------------------------------
// Timing

// How many times to run a phase, and how many of those to believe.
struct Reps {
  int warmup = 0;
  int timed  = 1;

  [[nodiscard]] auto total() const -> int { return warmup + timed; }
};

// One repetition, kept whole.
//
// The console and the CSV report a reduction over these, and any reduction
// throws information away by definition. A min hides the spread, so a workload
// whose repetitions ran 40 ms, 41 ms and 900 ms looks exactly like one that ran
// 40 ms three times. A warm up that is dropped from the min leaves no trace of
// having happened at all, which is the worse of the two, because the reader
// cannot tell a run that discarded a repetition from one that never did.
//
// So the repetitions are kept and --jsonl writes them out before any of it.
struct RepStats {
  int rep     = 0;
  bool warmup = false;
  std::chrono::nanoseconds build_ns{0};
  std::chrono::nanoseconds probe_ns{0};
  size_t matches = 0;
  bench::RssDelta build_rss;
  bench::RssDelta probe_rss;
};

struct RunStats {
  // Every repetition in the order it ran, warm ups included.
  std::vector<RepStats> reps;

  std::chrono::nanoseconds build_ns{0};
  std::chrono::nanoseconds probe_ns{std::chrono::nanoseconds::max()};
  size_t matches = 0;

  // Resident memory across each phase, from the first repetition and only the
  // first. See the comment above the runners for why not the best one.
  bench::RssDelta build_rss;
  bench::RssDelta probe_rss;

  // Process lifetime high water mark, taken at the end. Kept alongside the
  // deltas rather than replaced by them because it is the one number that
  // cannot undercount, and a delta of a phase that hands memory back to the
  // allocator can.
  size_t proc_rss_kb = 0;
  bool measured      = false;

  [[nodiscard]] auto build_ms() const -> double { return static_cast<double>(build_ns.count()) / 1.0e6; }

  [[nodiscard]] auto probe_ms() const -> double { return static_cast<double>(probe_ns.count()) / 1.0e6; }
};

// Min probe over the repetitions that were not warm ups, with build_ns and the
// match count taken from that same repetition rather than from whichever
// repetition was best at each of them separately.
//
// The memory deltas come from reps.front(), which is the first repetition that
// ran and is a warm up if there were any. That is on purpose and is the same
// argument as the comment below: the first one is the only one where the
// allocator had not already grown the heap, so it is the only one where the
// delta is about the automaton rather than about malloc. A warm up repetition
// is still a real build.
auto Reduce(std::vector<RepStats> reps) -> RunStats {
  RunStats out;
  for (const auto &r : reps) {
    if (r.warmup || r.probe_ns >= out.probe_ns) { continue; }
    out.probe_ns = r.probe_ns;
    out.build_ns = r.build_ns;
    out.matches  = r.matches;
  }
  out.build_rss   = reps.front().build_rss;
  out.probe_rss   = reps.front().probe_rss;
  out.proc_rss_kb = bench::PeakRssKb();
  out.measured    = true;
  out.reps        = std::move(reps);
  return out;
}

// The phase deltas below are taken on repetition zero and no other, while the
// timings are taken from whichever repetition was fastest. That is a real
// inconsistency and it is the deliberate one.
//
// Repetition zero is the only one that measures anything. By repetition one
// the allocator has already grown the heap to hold an automaton of this size
// and given none of it back, so building a second one reads as close to zero
// no matter how large it is. Reporting the delta from the fastest repetition
// would therefore report the memory of whichever repetition happened to win a
// stopwatch race, which is not a property of anything.
//
// A build phase whose delta is small on repetition zero is still telling the
// truth. A build phase whose delta is small on repetition four is telling you
// about malloc.

auto RunAc(Corpus &c, Reps reps) -> RunStats {
  std::vector<RepStats> out;
  for (int r = 0; r < reps.total(); r++) {
    RepStats rec{.rep = r, .warmup = r < reps.warmup};

    const bench::RssScope build_rss;
    auto b        = bench::BuildAc(c.patterns, c.escape);
    rec.build_rss = build_rss.Close();
    rec.build_ns  = b.build_ns;

    const bench::RssScope probe_rss;
    auto result = std::vector<bool>(c.patterns.size(), false);

    // The iterator is constructed inside the timed region on purpose. A query
    // pays for it once, so hiding it outside the clock would flatter the
    // number. The iterator is reused across rows inside it.
    size_t matches = 0;
    auto t0        = bench_clock::now();
    TextParserIterator iter(b.build_side.get(), b.trie.get());
    for (const auto &t : c.texts) {
      bench::AcProbe(b, iter, t, result);
      for (auto v : result) { matches += v ? 1 : 0; }
    }
    rec.probe_ns  = bench_clock::now() - t0;
    rec.probe_rss = probe_rss.Close();
    rec.matches   = matches;
    out.push_back(rec);
  }
  return Reduce(std::move(out));
}

// There is no build phase here. The pattern strings are the index, which is
// the whole point of the baseline, so build_rss stays at a valid zero and is
// not a gap in the measurement.
auto RunNlj(const Corpus &c, Reps reps) -> RunStats {
  std::vector<RepStats> out;
  for (int r = 0; r < reps.total(); r++) {
    RepStats rec{.rep = r, .warmup = r < reps.warmup};

    // Opened and closed with nothing between it, so the before and after
    // readings in the CSV are real and the zero is a measured zero.
    const bench::RssScope build_rss;
    rec.build_rss = build_rss.Close();

    const bench::RssScope probe_rss;
    auto result    = std::vector<bool>(c.patterns.size(), false);
    size_t matches = 0;
    auto t0        = bench_clock::now();
    for (const auto &t : c.texts) {
      bench::NljProbe(c.patterns, t, result, c.esc_cp);
      for (auto v : result) { matches += v ? 1 : 0; }
    }
    rec.probe_ns  = bench_clock::now() - t0;
    rec.probe_rss = probe_rss.Close();
    rec.matches   = matches;
    out.push_back(rec);
  }
  return Reduce(std::move(out));
}

#ifdef HN_BENCH_HAVE_DUCKDB
// build_ns is connect plus create plus bulk load, probe_ns is the join end to
// end including the aggregation. Repeats default to 1 because the load
// dominates and it is the load, not the join, that a second repetition warms.
auto RunEngine(const Corpus &c, Reps reps) -> RunStats {
  std::vector<RepStats> out;
  const auto sql = bench::JoinSql("COUNT(*)", c.ilike, c.escape);
  for (int r = 0; r < reps.total(); r++) {
    RepStats rec{.rep = r, .warmup = r < reps.warmup};

    const bench::RssScope build_rss;
    auto build_t0 = bench_clock::now();
    bench::EngineDb db;
    bench::LoadTables(db.con(), c.engine_texts(), c.text_ids, c.engine_patterns(), c.pattern_ids);
    rec.build_ns  = bench_clock::now() - build_t0;
    rec.build_rss = build_rss.Close();

    const bench::RssScope probe_rss;
    auto probe_t0 = bench_clock::now();
    duckdb_result res{};
    bench::DuckCheck(duckdb_query(db.con(), sql.c_str(), &res), "join query");
    rec.probe_ns  = bench_clock::now() - probe_t0;
    rec.probe_rss = probe_rss.Close();
    rec.matches   = static_cast<size_t>(duckdb_value_int64(&res, 0, 0));
    duckdb_destroy_result(&res);
    out.push_back(rec);
  }
  return Reduce(std::move(out));
}
#endif

// ---------------------------------------------------------------------------
// Verification
//
// What used to be here compared one number per workload, the total match count
// summed over every row and every pattern. Two errors of opposite sign on
// different rows cancel in that sum, so a run could report agreement while
// disagreeing on half its rows.
//
// These compare the pairs. A pair is a row id and a pattern id that matched,
// and two implementations agree only when they produce the same set of them.
// The pair total is reported alongside the mismatch count for the same reason:
// a verification that checked nothing would otherwise look exactly like one
// that checked everything.

struct VerifyStats {
  size_t pairs      = 0;
  size_t mismatches = 0;
  bool ran          = false;
  std::string note;  // shown instead of the mismatch count when set
};

// Enough to see the shape of a disagreement without burying a run whose every
// row is wrong.
constexpr size_t MAX_REPORTED = 20;

auto ReportPair(const Corpus &c, size_t r, size_t p, std::string_view other, bool ac, bool them) -> void {
  fmt::print(stderr, "   {} row {} pattern {} '{}': AC says {}, {} says {}\n", c.name, c.text_id(r), c.pattern_id(p),
             c.patterns[p], ac, other, them);
}

// The iterator is reused across rows here exactly as it is in the timed path,
// because reuse is the thing most likely to introduce a row order dependence
// and a verification that skipped it would miss that whole class.
auto VerifyVsRecursive(Corpus &c) -> VerifyStats {
  VerifyStats v;
  v.ran        = true;
  auto b       = bench::BuildAc(c.patterns, c.escape);
  auto ac_res  = std::vector<bool>(c.patterns.size(), false);
  auto ref_res = std::vector<bool>(c.patterns.size(), false);
  TextParserIterator iter(b.build_side.get(), b.trie.get());

  for (size_t r = 0; r < c.texts.size(); r++) {
    bench::AcProbe(b, iter, c.texts[r], ac_res);
    bench::NljProbe(c.patterns, c.texts[r], ref_res, c.esc_cp);
    for (size_t p = 0; p < c.patterns.size(); p++) {
      if (ref_res[p]) { v.pairs++; }
      if (ac_res[p] == ref_res[p]) { continue; }
      v.mismatches++;
      // The casts are for fmt, which cannot format the proxy reference a
      // vector<bool> hands back.
      if (v.mismatches <= MAX_REPORTED) {
        ReportPair(c, r, p, "recursive", static_cast<bool>(ac_res[p]), static_cast<bool>(ref_res[p]));
      }
    }
  }
  return v;
}

#ifdef HN_BENCH_HAVE_DUCKDB
// The engine is asked for the pairs themselves rather than a count, sorted,
// and the whole result is materialised. That is affordable because these
// workloads are sparse, a few matching patterns per row, so the result is tens
// of thousands of pairs and not rows times patterns.
auto VerifyVsEngine(Corpus &c) -> VerifyStats {
  VerifyStats v;
  v.ran = true;
  bench::EngineDb db;
  bench::LoadTables(db.con(), c.engine_texts(), c.text_ids, c.engine_patterns(), c.pattern_ids);

  // The same join the timed run issues, differing only in the select list.
  const auto sql = bench::JoinSql("t.text_id, p.pattern_id", c.ilike, c.escape, "ORDER BY t.text_id, p.pattern_id");
  duckdb_result res{};
  if (duckdb_query(db.con(), sql.c_str(), &res) != DuckDBSuccess) {
    fmt::print(stderr, "!! engine verification query failed: {}\n", duckdb_result_error(&res));
    std::exit(3);
  }

  auto b      = bench::BuildAc(c.patterns, c.escape);
  auto ac_res = std::vector<bool>(c.patterns.size(), false);
  TextParserIterator iter(b.build_side.get(), b.trie.get());

  const auto n_pairs = static_cast<size_t>(duckdb_row_count(&res));
  v.pairs            = n_pairs;

  // Indexed rather than merged. A merge down both sides at once is cheaper and
  // was what this did first, but it is only correct while texts.csv and
  // patterns.csv are sorted by their own id columns. The synthetic workloads
  // satisfy that because their id is their position, and the corpora we have
  // happen to as well, and neither is a promise. A corpus that arrived in some
  // other order would have made this report disagreements that were an
  // artefact of the row order and nothing else. Sorting the per row pattern
  // ids costs a log factor over the pairs, which are sparse.
  std::unordered_map<int64_t, std::vector<int64_t>> engine_by_row;
  for (size_t i = 0; i < n_pairs; i++) {
    engine_by_row[duckdb_value_int64(&res, 0, i)].push_back(duckdb_value_int64(&res, 1, i));
  }
  duckdb_destroy_result(&res);
  for (auto &[row, pats] : engine_by_row) { std::sort(pats.begin(), pats.end()); }

  static const std::vector<int64_t> EMPTY;
  size_t placed = 0;
  for (size_t r = 0; r < c.texts.size(); r++) {
    bench::AcProbe(b, iter, c.texts[r], ac_res);

    auto it                 = engine_by_row.find(c.text_id(r));
    const auto &engine_pats = it == engine_by_row.end() ? EMPTY : it->second;
    placed += engine_pats.size();

    for (size_t p = 0; p < c.patterns.size(); p++) {
      const bool engine_says = std::binary_search(engine_pats.begin(), engine_pats.end(), c.pattern_id(p));
      if (ac_res[p] == engine_says) { continue; }
      v.mismatches++;
      if (v.mismatches <= MAX_REPORTED) { ReportPair(c, r, p, "engine", static_cast<bool>(ac_res[p]), engine_says); }
    }
  }

  // Anything left over names a row id the corpus does not have, which would be
  // a bug in this function or in the loader rather than in either matcher, and
  // is not something to let pass as agreement.
  if (placed != n_pairs) {
    fmt::print(stderr, "!! {}: engine returned {} pairs and only {} were placed against a row\n", c.name, n_pairs,
               placed);
    v.mismatches += n_pairs - placed;
  }
  return v;
}
#endif

// A directory corpus ships labels.csv, the answer the upstream pipeline got
// out of DuckDB on a machine that is not this one. Comparing against it is
// weaker than the pair checks above, because it is one total against another
// and two errors of opposite sign would cancel. It is still worth doing: it is
// the only check here that spans two machines and two DuckDB versions, and a
// count that is off by thousands is a count that is off.
//
// It counts as one disagreement rather than as the size of the gap, so a
// corpus whose labels were generated wrong does not drown out the pair level
// checks that did run.
auto VerifyVsLabels(const Corpus &c, const RunStats &ac) -> VerifyStats {
  VerifyStats v;
  v.ran   = true;
  v.pairs = c.labels.size();
  if (ac.measured && ac.matches != c.labels.size()) {
    v.mismatches = 1;
    v.note       = fmt::format("{} pairs, AC found {}", c.labels.size(), ac.matches);
  }
  return v;
}

// ---------------------------------------------------------------------------
// Output

struct Row {
  std::string workload;
  std::string impl;
  const Corpus *corpus = nullptr;
  RunStats stats;
};

auto RowsPerSec(const Corpus &c, const RunStats &s) -> double {
  return static_cast<double>(c.rows()) / (static_cast<double>(s.probe_ns.count()) / 1.0e9);
}

auto MbPerSec(const Corpus &c, const RunStats &s) -> double {
  return (static_cast<double>(c.total_text_bytes) / 1.0e6) / (static_cast<double>(s.probe_ns.count()) / 1.0e9);
}

// An invalid delta prints as a dash and not as a zero, because a platform with
// no current RSS interface and a phase that allocated nothing should not look
// like the same result.
auto RssCell(const bench::RssDelta &d) -> std::string { return d.valid ? fmt::format("{}", d.kb) : std::string("-"); }

auto PrintHeader() -> void {
  fmt::print("{:<24} {:<14} {:>8} {:>10} {:>8} {:>12} {:>12} {:>12} {:>10} {:>12}\n", "workload", "impl", "rows",
             "patterns", "MB", "build_ms", "probe_ms", "rows/s", "MB/s", "build_rss_kB");
}

auto PrintRow(const Row &row) -> void {
  const auto &c = *row.corpus;
  fmt::print("{:<24} {:<14} {:>8} {:>10} {:>8.2f} {:>12.3f} {:>12.3f} {:>12.0f} {:>10.2f} {:>12}\n", row.workload,
             row.impl, c.rows(), c.m(), static_cast<double>(c.total_text_bytes) / 1.0e6, row.stats.build_ms(),
             row.stats.probe_ms(), RowsPerSec(c, row.stats), MbPerSec(c, row.stats), RssCell(row.stats.build_rss));
}

auto WriteCsv(const fs::path &path, const std::vector<Row> &rows) -> void {
  std::ofstream f(path);
  if (!f) { Die(fmt::format("cannot write {}", path.string())); }
  f << "workload,impl,rows,patterns,total_text_bytes,build_ms,probe_ms,match_count,rows_per_s,mb_per_s,"
       "build_rss_kb,probe_rss_kb,rss_before_build_kb,rss_after_probe_kb,proc_peak_rss_kb\n";
  for (const auto &row : rows) {
    const auto &c = *row.corpus;
    f << row.workload << ',' << row.impl << ',' << c.rows() << ',' << c.m() << ',' << c.total_text_bytes << ','
      << fmt::format("{:.3f}", row.stats.build_ms()) << ',' << fmt::format("{:.3f}", row.stats.probe_ms()) << ','
      << row.stats.matches << ',' << fmt::format("{:.1f}", RowsPerSec(c, row.stats)) << ','
      << fmt::format("{:.3f}", MbPerSec(c, row.stats)) << ',' << RssCell(row.stats.build_rss) << ','
      << RssCell(row.stats.probe_rss) << ','
      << (row.stats.build_rss.valid ? fmt::format("{}", row.stats.build_rss.before_kb) : "-") << ','
      << (row.stats.probe_rss.valid ? fmt::format("{}", row.stats.probe_rss.after_kb) : "-") << ','
      << row.stats.proc_rss_kb << '\n';
  }
}

// Only the two characters JSON forbids in a string plus the control range.
// Workload names cannot contain any of them, but a corpus directory name can
// contain anything a filesystem allows and it goes in the record verbatim.
auto JsonEscape(std::string_view in) -> std::string {
  std::string out;
  for (const char c : in) {
    if (c == '"' || c == '\\') {
      out.push_back('\\');
      out.push_back(c);
    } else if (static_cast<unsigned char>(c) < 0x20) {
      out.append(fmt::format("\\u{:04x}", static_cast<unsigned>(c)));
    } else {
      out.push_back(c);
    }
  }
  return out;
}

// null and not 0 when the platform has no current RSS, for the same reason the
// console prints a dash: a field that was never measured and a field that
// measured zero have to look different or the reader cannot tell them apart.
auto JsonRss(const bench::RssDelta &d, std::string_view key_delta, std::string_view key_before,
             std::string_view key_after) -> std::string {
  if (!d.valid) { return fmt::format(R"("{}":null,"{}":null,"{}":null)", key_delta, key_before, key_after); }
  return fmt::format(R"("{}":{},"{}":{},"{}":{})", key_delta, d.kb, key_before, d.before_kb, key_after, d.after_kb);
}

// One JSON object per line, one line per repetition. Not an array, so a run
// that dies halfway leaves a file that still parses line by line, and so
// appending a second run to it is concatenation.
//
// Nanoseconds and not milliseconds. The reduction to a human column happens in
// the console table and this is the other thing, the record that analysis reads.
auto WriteJsonl(const fs::path &path, const std::vector<Row> &rows, const Options &o) -> void {
  std::ofstream f(path);
  if (!f) { Die(fmt::format("cannot write {}", path.string())); }
  for (const auto &row : rows) {
    const auto &c = *row.corpus;
    for (const auto &rep : row.stats.reps) {
      f << fmt::format(
             R"({{"workload":"{}","impl":"{}","rep":{},"warmup":{},"rows":{},"patterns":{},"total_text_bytes":{},)"
             R"("digest":"{:#018x}","build_ns":{},"probe_ns":{},"match_count":{},{},{},"proc_peak_rss_kb":{},)"
             R"("repeats_requested":{},"warmup_requested":{}}})",
             JsonEscape(row.workload), JsonEscape(row.impl), rep.rep, rep.warmup ? "true" : "false", c.rows(), c.m(),
             c.total_text_bytes, c.digest, rep.build_ns.count(), rep.probe_ns.count(), rep.matches,
             JsonRss(rep.build_rss, "build_rss_kb", "rss_before_build_kb", "rss_after_build_kb"),
             JsonRss(rep.probe_rss, "probe_rss_kb", "rss_before_probe_kb", "rss_after_probe_kb"), row.stats.proc_rss_kb,
             row.impl == "DuckDB-engine" ? o.engine_repeats : o.repeats, o.warmup)
        << '\n';
    }
  }
}

// ---------------------------------------------------------------------------
// One workload, end to end

struct Outcome {
  size_t mismatches = 0;
};

auto RunOne(Corpus &c, const Options &o, std::vector<Row> &rows) -> Outcome {
  Outcome out;

  if (c.digest != 0) {
    fmt::print("digest {:<18} {:#018x} rows={} patterns={} bytes={}\n", c.name, c.digest, c.rows(), c.m(),
               c.total_text_bytes);
  }
  fflush(stdout);

  auto note = [&](std::string_view what) {
    fmt::print(stderr, "[{}] {}...\n", c.name, what);
    fflush(stderr);
  };

  RunStats ac;
  if (o.time_ac) {
    note("AC");
    ac = RunAc(c, {.warmup = o.warmup, .timed = o.repeats});
    rows.push_back({c.name, "AC", &c, ac});
    PrintRow(rows.back());
    fflush(stdout);
  }

  if (o.time_nlj) {
    note("NLJ-recursive");
    rows.push_back({c.name, "NLJ-recursive", &c, RunNlj(c, {.warmup = o.warmup, .timed = o.repeats})});
    PrintRow(rows.back());
    fflush(stdout);
  }

#ifdef HN_BENCH_HAVE_DUCKDB
  if (o.time_engine) {
    note("DuckDB-engine");
    rows.push_back({c.name, "DuckDB-engine", &c, RunEngine(c, {.warmup = o.warmup, .timed = o.engine_repeats})});
    PrintRow(rows.back());
    fflush(stdout);
  }
#endif

  const bool want_rec = o.verify == Verify::kAll || o.verify == Verify::kRecursive;
  const bool want_eng = o.verify == Verify::kAll || o.verify == Verify::kEngine;

  VerifyStats vr, ve, vl;
  if (want_rec) {
    note("verify vs recursive");
    vr = VerifyVsRecursive(c);
    out.mismatches += vr.mismatches;
  }
#ifdef HN_BENCH_HAVE_DUCKDB
  if (want_eng) {
    note("verify vs engine");
    ve = VerifyVsEngine(c);
    out.mismatches += ve.mismatches;
  }
#endif
  // Only meaningful against a timed AC run, since that is where the match
  // count comes from. With --impl nljrec there is nothing to compare.
  if (c.has_labels && o.verify != Verify::kNone && ac.measured) {
    vl = VerifyVsLabels(c, ac);
    out.mismatches += vl.mismatches;
  }

  if (o.verify != Verify::kNone) {
    auto say = [](const VerifyStats &v) -> std::string {
      if (!v.ran) { return "not run"; }
      if (!v.note.empty()) { return v.note; }
      return fmt::format("{} pairs, {} mismatches", v.pairs, v.mismatches);
    };
    fmt::print("verify {:<18} recursive: {}   engine: {}", c.name, say(vr), say(ve));
    if (c.has_labels && vl.ran) { fmt::print("   labels: {}", say(vl)); }
    fmt::print("\n");
    fflush(stdout);
  }
  return out;
}

// The speedup table, printed once at the end so a long run does not interleave
// it with progress. Only against the probe phase: build time is a different
// question and the engine's build is a bulk load, which is not comparable to
// compiling a trie.
auto PrintSpeedups(const std::vector<Row> &rows) -> void {
  // Nothing to divide by if --impl left the AC probe out.
  if (std::none_of(rows.begin(), rows.end(), [](const Row &r) { return r.impl == "AC"; })) { return; }

  fmt::print("\nSpeedup of the AC probe against the baselines, probe phase only:\n");
  fmt::print("{:<24} {:>14} {:>14}\n", "workload", "vs_NLJ-rec", "vs_DuckDB-eng");
  for (const auto &row : rows) {
    if (row.impl != "AC") { continue; }
    auto other = [&](std::string_view impl) -> std::string {
      for (const auto &o : rows) {
        if (o.workload == row.workload && o.impl == impl && o.stats.measured) {
          return fmt::format(
            "{:.2f}x", static_cast<double>(o.stats.probe_ns.count()) / static_cast<double>(row.stats.probe_ns.count()));
        }
      }
      return "n/a";
    };
    fmt::print("{:<24} {:>14} {:>14}\n", row.workload, other("NLJ-recursive"), other("DuckDB-engine"));
  }
}

}  // namespace

int main(int argc, char **argv) {
  const auto o = ParseArgs(argc, argv);

  // Which DuckDB this was linked against belongs in the log next to the
  // numbers. The machines we build on ship different versions, and a speedup
  // quoted against an unnamed "DuckDB" is not reproducible.
#ifdef HN_BENCH_HAVE_DUCKDB
  fmt::print(stderr, "ac_bench: duckdb {} (configure), {} (runtime)\n", HN_BENCH_DUCKDB_VERSION,
             duckdb_library_version());
#else
  fmt::print(stderr, "ac_bench: built without the DuckDB engine baseline\n");
#endif

  // Held by value for the whole run because Row points into them.
  std::vector<Corpus> corpora;

  // Names are checked against the spec table before anything is generated, so
  // a typo fails in the first second rather than after the run it was meant to
  // narrow has already happened. Checked here and not by counting what came
  // out of the loop below, because --quick legitimately drops workloads and
  // that count would then accuse the caller of a typo they did not make.
  for (const auto &want : o.workloads) {
    const auto *found = std::find_if(workload::BASELINE_SPECS.begin(), workload::BASELINE_SPECS.end(),
                                     [&](const workload::Spec &s) { return s.name == want; });
    if (found == workload::BASELINE_SPECS.end()) {
      Die(fmt::format("no built in workload called '{}', try --list", want));
    }
  }

  if (o.dirs.empty() || !o.workloads.empty()) {
    for (const auto &s : workload::BASELINE_SPECS) {
      if (!o.workloads.empty() &&
          std::find(o.workloads.begin(), o.workloads.end(), std::string(s.name)) == o.workloads.end()) {
        continue;
      }
      // What costs time here is the NLJ-recursive baseline, and its cost is
      // rows times patterns times the length of a row, not rows times
      // patterns. Leaving the length out put long_text_M=1k on the quick side
      // of the line, where it took a minute on its own. The threshold keeps
      // the three cheapest and drops four that are minutes each.
      if (o.quick && s.n_texts * s.n_patterns * s.words_per_text > 100'000'000) {
        fmt::print(stderr, "quick: skipping {}\n", s.name);
        continue;
      }
      corpora.push_back(bench::FromSpec(s));
    }
  }
  for (const auto &d : o.dirs) {
    auto c = bench::FromDirectory(d);
    if (c.texts.empty() || c.patterns.empty()) { Die(fmt::format("{}: empty texts or patterns", d.string())); }
    corpora.push_back(std::move(c));
  }
  if (corpora.empty()) { Die("nothing to run"); }

  std::vector<Row> rows;
  rows.reserve(corpora.size() * 3);
  size_t mismatches = 0;

  PrintHeader();
  for (auto &c : corpora) { mismatches += RunOne(c, o, rows).mismatches; }

  PrintSpeedups(rows);
  if (!o.csv_path.empty()) { WriteCsv(o.csv_path, rows); }
  if (!o.jsonl_path.empty()) { WriteJsonl(o.jsonl_path, rows, o); }

  // So that the complaint lands after the table rather than in the middle of
  // it, which is where stdio buffering would otherwise put it.
  fflush(stdout);
  if (mismatches != 0) {
    fmt::print(stderr, "\n!! {} disagreeing pairs across all workloads, the timings above are not meaningful\n",
               mismatches);
    return 1;
  }
  return 0;
}
