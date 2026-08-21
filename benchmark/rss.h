#pragma once

// Resident set size, two different questions with two different answers.
//
// getrusage(RUSAGE_SELF).ru_maxrss is a high water mark over the whole process
// lifetime. It is the number this benchmark reported for a long time, in a
// column sitting next to a workload name, which invited reading it as the
// memory that workload needed. It is not that. On the second workload of a run
// it can be entirely the first workload's memory, and on the last workload of
// a seven workload run it usually is.
//
// The number that answers the question the column was pretending to answer is
// the change in resident memory across a phase, so both are here. Neither one
// alone is the whole story and the caller reports both.
//
// Current RSS has no portable interface. Linux exposes it in /proc/self/statm
// as a page count, macOS through task_info. Anywhere else Available() is false
// and the caller prints nothing rather than a zero that looks like a
// measurement.

#include <cstddef>
#include <cstdint>

#include <sys/resource.h>
#include <sys/time.h>

#if defined(__APPLE__)
#include <mach/mach.h>
#elif defined(__linux__)
#include <unistd.h>
#include <cstdio>
#endif

namespace bench {

// Whether CurrentRssKb() is a measurement on this platform.
constexpr auto RssAvailable() -> bool {
#if defined(__APPLE__) || defined(__linux__)
  return true;
#else
  return false;
#endif
}

// Resident memory right now, in kilobytes. Zero if unavailable, which callers
// distinguish from a real zero by asking RssAvailable() first.
inline auto CurrentRssKb() -> size_t {
#if defined(__APPLE__)
  mach_task_basic_info info{};
  mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
  if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) {
    return 0;
  }
  return static_cast<size_t>(info.resident_size / 1024);
#elif defined(__linux__)
  // Field two of /proc/self/statm is resident pages. statm is read rather than
  // status because it is a fixed set of integers and needs no key matching.
  std::FILE *f = std::fopen("/proc/self/statm", "r");
  if (f == nullptr) { return 0; }
  long long total  = 0;
  long long rss    = 0;
  const int fields = std::fscanf(f, "%lld %lld", &total, &rss);
  std::fclose(f);
  if (fields != 2 || rss < 0) { return 0; }
  const long page = sysconf(_SC_PAGESIZE);
  return static_cast<size_t>(rss) * static_cast<size_t>(page > 0 ? page : 4096) / 1024;
#else
  return 0;
#endif
}

// High water mark over the process lifetime, in kilobytes.
inline auto PeakRssKb() -> size_t {
  rusage ru{};
  getrusage(RUSAGE_SELF, &ru);
#if defined(__APPLE__)
  return static_cast<size_t>(ru.ru_maxrss / 1024);  // macOS reports bytes
#else
  return static_cast<size_t>(ru.ru_maxrss);  // Linux reports kilobytes
#endif
}

// Resident memory across one phase, as a difference of two CurrentRssKb()
// readings.
//
// Signed, because it can legitimately go down: a phase that frees more than it
// allocates gives a negative delta and rounding a negative to zero would hide
// that. `valid` is false when the platform has no current RSS at all, which is
// not the same as a delta that happened to be zero.
struct RssDelta {
  int64_t kb       = 0;
  bool valid       = false;
  size_t before_kb = 0;
  size_t after_kb  = 0;
};

// Opens a phase. Call Close() at the end of it.
class RssScope {
 public:
  RssScope() : before_(RssAvailable() ? CurrentRssKb() : 0) {}

  [[nodiscard]] auto Close() const -> RssDelta {
    RssDelta d;
    if (!RssAvailable()) { return d; }
    d.valid     = true;
    d.before_kb = before_;
    d.after_kb  = CurrentRssKb();
    d.kb        = static_cast<int64_t>(d.after_kb) - static_cast<int64_t>(before_);
    return d;
  }

 private:
  size_t before_;
};

}  // namespace bench
