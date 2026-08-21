// Perf guard: two ratios that have to hold for the probe to be the algorithm
// the paper describes, checked as an exit status rather than as a number a
// human reads off a table.
//
// The two claims:
//
//   scale_M = probe_ms(sweep_M=10k) / probe_ms(sweep_M=10)
//
//     Both workloads split about 3.5 MB of text. sweep_M=10 is 10000 rows
//     against 10 patterns, sweep_M=10k is 2000 rows against 10000 patterns,
//     so a probe whose per row cost is linear in the pattern count lands on
//     (2000*10000)/(10000*10) = 20. An automaton is supposed to visit the
//     text once and let the trie sort out which patterns are live, so this
//     ratio is the difference between having built one and having built a
//     loop with a trie in it.
//
//   shape = probe_ms(short_text_M=1k) / probe_ms(sweep_M=1k)
//
//     Same pattern count, same total bytes to within 2 percent, different
//     row shape: 50000 rows of 10 words against 10000 rows of 50 words. A
//     probe that costs what the text costs lands near 1. A probe that costs
//     what the rows cost lands near 5, because there are 5 times as many of
//     them, and that means there is a fixed per row setup swamping the scan.
//
// Ratios and not absolute times, so this can run somewhere we do not control
// the neighbours, and process CPU time rather than wall time, which turned out
// to matter more.
//
// Wall time was the first attempt and it does not survive a busy machine. Ten
// runs on server3 at load 24 on 8 cores read shape between 1.02 and 1.89, so
// five of the ten failed a 1.5 limit and none of the failures said anything
// about the code. The probe is one thread doing arithmetic, so time it spends
// descheduled while a neighbour runs is not a property of the probe, and
// CLOCK_PROCESS_CPUTIME_ID does not count it. The same ten runs on CPU time
// read 1.09 to 1.65, eight of ten passing.
//
// The repeats are also interleaved, one pass over every workload and then
// another, so each workload's best is drawn from the same stretch of the run
// rather than from whatever the machine was doing during its turn.
//
// More repeats do not close the rest of the gap, which is worth writing down
// because it is the obvious next thing to try. Twenty of them on server3 cost
// 55 seconds a run and still failed two of nine. Whatever is left is variance
// in the CPU time itself, memory system contention rather than scheduling, and
// every pass pays it, so the minimum has nothing quieter to converge on.

#include "benchmark/probe.h"
#include "benchmark/workload.h"

#include "aho_corasick/parser.h"
#include "fmt/format.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using aho_corasick::TextParserIterator;
using workload::Workload;

namespace {

// Process CPU time, not wall time. See the note at the top of the file.
// bench::bench_clock is steady_clock and is what ac_bench reports,
// because a reader of a benchmark table wants the time the query took. A
// threshold that has to hold on a shared runner wants the time the query
// spent running.
auto CpuNanos() -> std::chrono::nanoseconds {
  timespec ts{};
  clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
  return std::chrono::seconds(ts.tv_sec) + std::chrono::nanoseconds(ts.tv_nsec);
}

// The thresholds. Changing one of these is changing what the repository claims
// about itself, so they are named and they are here and not inline in the
// comparison.
//
// SHAPE_MAX is 2.0 where a tighter reading of the claim would say 1.5. That
// is a deliberate relaxation and it is not the code failing to reach 1.5.
// Measured:
//
//   macbook, arm64, load 16 on 10 cores    10 runs, shape 1.00 to 1.11
//   server3, x86-64, load 22 on 8 cores    31 runs, shape 0.98 to 1.75
//
// The probe sits near 1.05 where the measurement is trustworthy. On a box
// oversubscribed nearly three to one the same binary reached 1.75, so a 1.5
// limit there is testing the neighbours. 2.0 clears the worst of those 31 by
// 14 percent, which is not a lot of headroom and is the reason those numbers
// are written down here rather than summarised.
//
// It is still far inside the half of the range that means the cost follows the
// bytes. A probe that had gone back to costing what the rows cost would read
// near 5, and one that had merely doubled its per row constant would read near
// 2.1 and trip this.
//
// GitHub's hosted runners are not oversubscribed the way server3 is and would
// probably hold 1.5. Probably is not a measurement, and Actions is billing
// blocked on this fork so there is no run to point at, so the number here is
// the one backed by the two machines above.
constexpr double SCALE_M_MAX = 8.0;
constexpr double SHAPE_MAX   = 2.0;

// Five passes over four workloads is a few seconds on an idle machine. See the
// note at the top of the file for why this is not 20.
constexpr int REPEATS = 5;

// The four the ratios are built from. Order is the order they are measured in
// within a pass, and it does not matter, which is the point of interleaving.
constexpr std::array<std::string_view, 4> NEEDED = {"sweep_M=10", "sweep_M=10k", "sweep_M=1k", "short_text_M=1k"};

auto SpecByName(std::string_view name) -> const workload::Spec & {
  for (const auto &s : workload::BASELINE_SPECS) {
    if (s.name == name) { return s; }
  }
  // A typo here is a build side mistake, not a runtime condition worth a
  // return code that the caller then has to thread through.
  fmt::print(stderr, "!! no workload named '{}' in BASELINE_SPECS\n", name);
  std::exit(2);
}

struct Measured {
  std::string_view name;
  Workload w;
  std::chrono::nanoseconds best{std::chrono::nanoseconds::max()};
  size_t matches = 0;

  auto cpu_ms() const -> double { return static_cast<double>(best.count()) / 1.0e6; }
};

// One timed pass over one workload. Build outside the clock, iterator
// construction inside it, because a query pays for that once and hiding it
// would flatter the number.
//
// The match count is not in here, which is the one place this program and
// ac_bench deliberately disagree. Counting set bits in the result is a
// walk of M per row, and iterating a vector<bool> does it a proxy at a time,
// so on short_text_M=1k that is 50 million proxy dereferences charged to the
// probe. It is the harness reading the answer rather than the probe producing
// it, and a per row constant is exactly what this program exists to detect, so
// adding one of its own would be measuring the wrong thing on purpose. It is
// still counted, once, in Verify below.
auto OnePass(Measured &m) -> void {
  auto b      = bench::BuildAc(m.w.patterns);
  auto result = std::vector<bool>(m.w.patterns.size(), false);

  auto t0 = CpuNanos();
  TextParserIterator iter(b.build_side.get(), b.trie.get());
  for (const auto &text : m.w.texts) { bench::AcProbe(b, iter, text, result); }
  m.best = std::min(m.best, CpuNanos() - t0);
}

// Untimed. Every workload here is generated with a hit probability well above
// zero, so no matches means the probe answered nothing and the timings are
// timings of that. The counts are the `pairs` column of
// reproducibility/expected/baseline_workloads.tsv if you want the stronger
// check, which is test/test_golden.cc's job and not this one's.
auto Verify(Measured &m) -> void {
  auto b      = bench::BuildAc(m.w.patterns);
  auto result = std::vector<bool>(m.w.patterns.size(), false);
  TextParserIterator iter(b.build_side.get(), b.trie.get());
  for (const auto &text : m.w.texts) {
    bench::AcProbe(b, iter, text, result);
    for (auto v : result) m.matches += v ? 1 : 0;
  }
  if (m.matches == 0) {
    fmt::print(stderr, "!! {} produced no matches, there is nothing here to time\n", m.name);
    std::exit(2);
  }
}

auto Find(const std::vector<Measured> &all, std::string_view name) -> const Measured & {
  for (const auto &m : all) {
    if (m.name == name) { return m; }
  }
  fmt::print(stderr, "!! {} was not measured\n", name);
  std::exit(2);
}

// Prints the ratio either way. A guard that only speaks when it fails leaves
// nobody able to say how much headroom there was the day before it did.
auto Check(std::string_view label, double got, double limit) -> bool {
  const bool ok = got < limit;
  fmt::print("{:<10} {:>8.2f} {} {:.2f}   {}\n", label, got, ok ? "<" : ">=", limit, ok ? "ok" : "FAIL");
  return ok;
}

}  // namespace

int main() {
  fmt::print("perf guard, best of {} interleaved repeats, process CPU time\n\n", REPEATS);

  std::vector<Measured> all;
  all.reserve(NEEDED.size());
  for (auto name : NEEDED) {
    // Field at a time rather than a braced list. `best` starts at
    // nanoseconds::max() so the first pass wins the min, and a braced list
    // that names it value initialises it to zero instead, which leaves every
    // best at zero and every ratio at nan.
    Measured m;
    m.name = name;
    m.w    = workload::MakeWorkload(SpecByName(name));
    all.push_back(std::move(m));
  }

  for (int r = 0; r < REPEATS; r++) {
    for (auto &m : all) OnePass(m);
  }

  for (auto &m : all) {
    Verify(m);
    fmt::print("{:<20} {:>8} rows {:>8} patterns {:>7.2f} MB {:>11.3f} cpu_ms {:>10} matches\n", m.name,
               m.w.texts.size(), m.w.patterns.size(), static_cast<double>(m.w.total_text_bytes) / 1.0e6, m.cpu_ms(),
               m.matches);
  }

  const double scale_m = Find(all, "sweep_M=10k").cpu_ms() / Find(all, "sweep_M=10").cpu_ms();
  const double shape   = Find(all, "short_text_M=1k").cpu_ms() / Find(all, "sweep_M=1k").cpu_ms();

  fmt::print("\n");
  const bool scale_ok = Check("scale_M", scale_m, SCALE_M_MAX);
  const bool shape_ok = Check("shape", shape, SHAPE_MAX);

  if (scale_ok && shape_ok) { return 0; }
  fmt::print(stderr, "\n!! the probe is not behaving like an automaton\n");
  return 1;
}
