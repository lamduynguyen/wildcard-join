#pragma once

#include "aho_corasick/aho_corasick.h"
#include "aho_corasick/delay_queue.h"

namespace aho_corasick {

struct TextParserIterator {
  const char *text;              // The text being matched
  const size_t text_len;         // Above
  const AhoCorasick *automaton;  // The AhoCorasick automaton
  size_t text_offset;            // Current byte offset within text
  size_t codepoint_idx;          // The current codepoint index within text
  ART::N *ptr;                   // AhoCorasick automaton

  TextParserIterator(const char *text, size_t text_len, AhoCorasick *automaton)
      : text(text),
        text_len(text_len),
        text_offset(0),
        codepoint_idx(0),
        automaton(automaton),
        ptr(automaton->GetRoot()) {}

  inline auto CanAdvanceOneCodePoint() { return text_offset < text_len; }

  // Two way to parse a text: One-round or iteratively
  auto ParseText() -> OutputEmitType;

  auto ContinueParseText() -> OutputEmitType;

 private:
  void AppendResult(ART::N *leaf, size_t cp_len, OutputEmitType &out_result) const;
};

}  // namespace aho_corasick