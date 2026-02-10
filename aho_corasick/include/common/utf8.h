#ifndef H_infra_util_Utf8
#define H_infra_util_Utf8
//---------------------------------------------------------------------------
// Umbra
// (c) 2016 Thomas Neumann
//---------------------------------------------------------------------------
#include <cstdint>
#include <cstdlib>

namespace umbra {
//---------------------------------------------------------------------------
/// Common logic for Utf8 handling
namespace Utf8 {
//---------------------------------------------------------------------------
[[maybe_unused]] static char *writeCodePointUnchecked(char *writer, unsigned c) noexcept
// Write a Utf8 code point without bounds checking (can write up to 6 chars)
{
  if (c <= 0x7F) {
    *writer = c;
    return writer + 1;
  } else if (c <= 0x7FF) {
    writer[0] = 0xC0 | (c >> 6);
    writer[1] = 0x80 | (c & 0x3F);
    return writer + 2;
  } else if (c <= 0xFFFF) {
    writer[0] = 0xE0 | (c >> 12);
    writer[1] = 0x80 | ((c >> 6) & 0x3F);
    writer[2] = 0x80 | (c & 0x3F);
    return writer + 3;
  } else if (c <= 0x1FFFFF) {
    writer[0] = 0xF0 | (c >> 18);
    writer[1] = 0x80 | ((c >> 12) & 0x3F);
    writer[2] = 0x80 | ((c >> 6) & 0x3F);
    writer[3] = 0x80 | (c & 0x3F);
    return writer + 4;
  } else if (c <= 0x3FFFFFF) {
    writer[0] = 0xF8 | (c >> 24);
    writer[1] = 0x80 | ((c >> 18) & 0x3F);
    writer[2] = 0x80 | ((c >> 12) & 0x3F);
    writer[3] = 0x80 | ((c >> 6) & 0x3F);
    writer[4] = 0x80 | (c & 0x3F);
    return writer + 5;
  } else if (c <= 0x7FFFFFFF) {
    writer[0] = 0xFC | (c >> 30);
    writer[1] = 0x80 | ((c >> 24) & 0x3F);
    writer[2] = 0x80 | ((c >> 18) & 0x3F);
    writer[3] = 0x80 | ((c >> 12) & 0x3F);
    writer[4] = 0x80 | ((c >> 6) & 0x3F);
    writer[5] = 0x80 | (c & 0x3F);
    return writer + 6;
  } else
    return writer;
}

//---------------------------------------------------------------------------
[[maybe_unused]] static char *writeCodePoint(char *writer, char *writerLimit, unsigned c)
// Write a Utf8 code point, including bounds checks
{
  if (c <= 0x7F) {
    if (writer + 1 > writerLimit) return writer;
    *writer = c;
    return writer + 1;
  } else if (c <= 0x7FF) {
    if (writer + 2 > writerLimit) return writer;
    writer[0] = 0xC0 | (c >> 6);
    writer[1] = 0x80 | (c & 0x3F);
    return writer + 2;
  } else if (c <= 0xFFFF) {
    if (writer + 3 > writerLimit) return writer;
    writer[0] = 0xE0 | (c >> 12);
    writer[1] = 0x80 | ((c >> 6) & 0x3F);
    writer[2] = 0x80 | (c & 0x3F);
    return writer + 3;
  } else if (c <= 0x1FFFFF) {
    if (writer + 4 > writerLimit) return writer;
    writer[0] = 0xF0 | (c >> 18);
    writer[1] = 0x80 | ((c >> 12) & 0x3F);
    writer[2] = 0x80 | ((c >> 6) & 0x3F);
    writer[3] = 0x80 | (c & 0x3F);
    return writer + 4;
  } else if (c <= 0x3FFFFFF) {
    if (writer + 5 > writerLimit) return writer;
    writer[0] = 0xF8 | (c >> 24);
    writer[1] = 0x80 | ((c >> 18) & 0x3F);
    writer[2] = 0x80 | ((c >> 12) & 0x3F);
    writer[3] = 0x80 | ((c >> 6) & 0x3F);
    writer[4] = 0x80 | (c & 0x3F);
    return writer + 5;
  } else if (c <= 0x7FFFFFFF) {
    if (writer + 6 > writerLimit) return writer;
    writer[0] = 0xFC | (c >> 30);
    writer[1] = 0x80 | ((c >> 24) & 0x3F);
    writer[2] = 0x80 | ((c >> 18) & 0x3F);
    writer[3] = 0x80 | ((c >> 12) & 0x3F);
    writer[4] = 0x80 | ((c >> 6) & 0x3F);
    writer[5] = 0x80 | (c & 0x3F);
    return writer + 6;
  } else
    return writer;
}

//---------------------------------------------------------------------------
[[maybe_unused]] static unsigned getCodePointLen(unsigned c)
// Get the length of a code point in Utf8
{
  if (c <= 0x7F) {
    return 1;
  } else if (c <= 0x7FF) {
    return 2;
  } else if (c <= 0xFFFF) {
    return 3;
  } else if (c <= 0x1FFFFF) {
    return 4;
  } else if (c <= 0x3FFFFFF) {
    return 5;
  } else if (c <= 0x7FFFFFFF) {
    return 6;
  } else
    return 0;
}

//---------------------------------------------------------------------------
[[maybe_unused]] static unsigned readMultiByteCase(const char *reader, char firstByte, unsigned byteLen) noexcept
// Handle the multi-byte slow path of Utf8 reads
{
  switch (byteLen) {
    case 2: return ((firstByte & 0x1F) << 6) | (reader[1] & 0x3F);
    case 3: return ((firstByte & 0xF) << 12) | ((reader[1] & 0x3F) << 6) | (reader[2] & 0x3F);
    case 4:
      return ((firstByte & 0x7) << 18) | ((reader[1] & 0x3F) << 12) | ((reader[2] & 0x3F) << 6) | (reader[3] & 0x3F);
    case 5:
      return ((firstByte & 0x3) << 24) | ((reader[1] & 0x3F) << 18) | ((reader[2] & 0x3F) << 12) |
             ((reader[3] & 0x3F) << 6) | (reader[4] & 0x3F);
    case 6:
      return ((firstByte & 0x1) << 30) | ((reader[1] & 0x3F) << 24) | ((reader[2] & 0x3F) << 18) |
             ((reader[3] & 0x3F) << 12) | ((reader[4] & 0x3F) << 6) | (reader[5] & 0x3F);
    default: return 0;
  }
}

//---------------------------------------------------------------------------
static inline unsigned multiByteSequenceLength(char firstByte) noexcept
// Compute the length of a multi-byte utf8 sequence from the header byte
{
  // The header has the form 1...10<bits>, where the number of 1s is the number of bytes.
  unsigned len = __builtin_clz(~firstByte);
  return len ? len : 1;
}

//---------------------------------------------------------------------------
/// The result of readCodePoint
struct readCodePointResult {
  /// The next position
  const char *next;
  /// The code point
  unsigned codePoint;
};

//---------------------------------------------------------------------------
[[maybe_unused]] static readCodePointResult readCodePoint(const char *reader, const char *readerLimit) noexcept
// Read a single code point and return the next character
{
  // Check for ASCII fast path
  char firstByte = *reader;
  if (!(firstByte & 0x80)) {
    unsigned codePoint = firstByte;
    return {reader + 1, codePoint};
  }

  // Multi-byte sequence, perform bounds check first
  unsigned multiByteLen = multiByteSequenceLength(firstByte);
  if ((reader + multiByteLen) > readerLimit) {
    unsigned codePoint = '?';
    return {readerLimit, codePoint};
  }

  // Read the sequence
  unsigned codePoint = readMultiByteCase(reader, firstByte, multiByteLen);
  return {reader + multiByteLen, codePoint};
}

//---------------------------------------------------------------------------
[[maybe_unused]] static const char *moveBackwardsToCodePointBegin(const char *reader)
// Find the begin of a code point
{
  while (((*reader) & 0xC0) == 0x80) --reader;
  return reader;
}

//---------------------------------------------------------------------------
/// Check if the string is a valid utf8 sequence
bool validate(const char *begin, const char *end);
/// Check if the string is a valid utf8 sequence, and throw otherwise
void enforceValid(const char *begin, const char *end);
/// Limit the string length and avoid cutting multi-byte characters
size_t clampLen(const char *str, size_t strLen, size_t limit);
//---------------------------------------------------------------------------
}  // namespace Utf8

//---------------------------------------------------------------------------
}  // namespace umbra

//---------------------------------------------------------------------------
#endif
