#pragma once

// Two things a randomised test needs if a failure on somebody else's machine
// is going to be a failure on yours: a draw that does not go through the
// standard library, and a digest so the log says which corpus ran.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace corpus {

// Not std::uniform_int_distribution. The engines are specified down to the
// bit, so a fixed seed gives a fixed stream everywhere. The distributions are
// not: libstdc++ and libc++ pull a different number of words out of the engine
// for the same range, so the same seed builds a different corpus on each.
//
// fuzz_differential.cc found that the hard way. Its first version used the
// distribution and the same seed reached its first disagreement after 26
// rounds on macOS and 15 on a linux box, which makes "the seed is fixed so it
// fails for everyone" false and makes a printed seed useless to anybody on the
// other toolchain.
//
// Plain modulo, so the reduction belongs to this file. It is biased: for n = 4
// against a 32 bit engine the low values are reachable by one extra multiple
// of n out of 2^32, a difference of about 1e-9 in their probability, and for a
// 64 bit engine it is smaller still. Nothing here is a statistical claim.
template <class Engine>
auto Pick(Engine &rng, size_t n) -> size_t {
  return static_cast<size_t>(rng() % n);
}

// FNV-1a over a list of strings, newline delimited so that {"ab", "c"} and
// {"a", "bc"} do not collide. No security property wanted or claimed. It has
// to be cheap and it has to be reimplementable in three lines by somebody
// checking whether their reproduction is running the same data.
inline auto Digest(const std::vector<std::string> &parts) -> uint64_t {
  constexpr uint64_t OFFSET = 0xcbf29ce484222325ULL;
  constexpr uint64_t PRIME  = 0x100000001b3ULL;

  uint64_t h = OFFSET;
  for (std::string_view s : parts) {
    for (unsigned char c : s) {
      h ^= c;
      h *= PRIME;
    }
    h ^= '\n';
    h *= PRIME;
  }
  return h;
}

}  // namespace corpus
