#pragma once

// The textbook recursive LIKE matcher, one text against one pattern. It is the
// same algorithm DuckDB's TemplatedLikeOperator uses in
// src/function/scalar/string/like.cpp, kept here so everything that needs an
// oracle has one that is not the automaton under test.
//
// It used to be called DuckDBMatching, and the benchmarks reported speedups
// against it under the label "DuckDB-recursive". That was misleading. Driving
// this function from a nested loop over M patterns is not DuckDB: the real
// engine runs the same per pair algorithm inside a vectorised executor with
// its own scan, filter pushdown, short circuiting and thread pool. Which of
// the two comes out ahead at M=100 turns out to depend on how many cores the
// engine is given, and measurements on server2 put it on both sides of this
// loop on that basis alone. Whichever way it lands on a given machine, a
// speedup quoted against this loop under DuckDB's name is not a statement
// about DuckDB. The reference is called NLJ-recursive now, and the real
// in-process engine is a separate baseline in the benchmarks. See #7.
//
// There were three copies of this: test/matcher.h, benchmark/baseline_bench.cc
// and benchmark/hn_bench.cc. Two of them were byte for byte identical and the
// third had grown SQL ESCAPE support that the other two never got. An oracle
// that exists three times is three oracles, and only one of them was ever the
// one a given failure had been checked against.
//
// It lives under benchmark/ rather than test/ because the benchmarks must not
// pull in gtest, and test/ already includes from here (test/probe.h does).

#include "aho_corasick/parser.h"
#include "common/utf8.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bench {

// `esc` is a single code point, or 0 for no ESCAPE clause. When set, a pattern
// code point equal to it makes the next one literal, stripping the wildcard
// meaning from `%`, `_` and the escape character itself. With esc == 0 every
// branch that mentions it is dead and this is the LIKE-only matcher the tests
// have always used.
inline auto NljRecursiveMatch(const char *sdata, size_t slen, const char *pdata, size_t plen,
                              uint32_t esc = 0) -> bool {
  using aho_corasick::Tokenizer;
  size_t pidx = 0;
  size_t sidx = 0;
  for (; pidx < plen && sidx < slen;) {
    auto pchar      = umbra::Utf8::readCodePoint(&pdata[pidx], pdata + plen);
    auto schar      = umbra::Utf8::readCodePoint(&sdata[sidx], sdata + slen);
    bool is_literal = false;

    if (esc != 0 && pchar.codePoint == esc) {
      size_t next_pidx = pchar.next - pdata;
      // A pattern ending in a bare escape is malformed. DuckDB raises on it;
      // here it just fails to match, because this is an oracle for the
      // matching answer and not for the parse error.
      if (next_pidx >= plen) { return false; }
      pchar      = umbra::Utf8::readCodePoint(&pdata[next_pidx], pdata + plen);
      pidx       = next_pidx;
      is_literal = true;
    }

    // The `_` arm and the literal arm are identical bodies on purpose and
    // cannot be merged across the `%` arm between them: `%` returns, so
    // falling through from `_` into it would change the answer.
    // NOLINTNEXTLINE(bugprone-branch-clone)
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
        if (NljRecursiveMatch(sdata + sidx, slen - sidx, pdata + pidx, plen - pidx, esc)) { return true; }
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

  // Whatever is left of the pattern has to be `%` all the way down. The bound
  // is checked before the read rather than after, which is the one place the
  // three old copies differed: the other two read `pdata[plen]` once on every
  // fully consumed pattern. Every caller passes a std::string, so that read
  // landed on the NUL terminator and was both in bounds and never equal to
  // `%`. It was harmless and it did not have to be.
  while (pidx < plen) {
    auto pchar = umbra::Utf8::readCodePoint(&pdata[pidx], pdata + plen);
    if (esc != 0 && pchar.codePoint == esc) { return false; }
    if (pchar.codePoint != Tokenizer::PERCENTAGE) { return false; }
    pidx = pchar.next - pdata;
  }
  return pidx == plen && sidx == slen;
}

// One row against every pattern, which is what a nested loop join does and is
// the baseline the automaton is measured against.
inline auto NljProbe(const std::vector<std::string> &patterns, const std::string &text, std::vector<bool> &result,
                     uint32_t esc = 0) -> void {
  for (size_t i = 0; i < patterns.size(); i++) {
    result[i] = NljRecursiveMatch(text.data(), text.size(), patterns[i].data(), patterns[i].size(), esc);
  }
}

}  // namespace bench
