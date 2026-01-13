#ifndef ART_KEY_H
#define ART_KEY_H

#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>

using KeyLen                             = uint32_t;
static constexpr uint8_t NULL_TERMINATOR = '\0';

class Key {
 public:
  static constexpr uint32_t stackLen = 128;
  uint32_t len                       = 0;
  uint8_t *data;
  uint8_t stackKey[stackLen];

  Key(uint64_t k) { setInt(k); }

  void setInt(uint64_t k) {
    data                                    = stackKey;
    len                                     = 8;
    *reinterpret_cast<uint64_t *>(stackKey) = __builtin_bswap64(k);
  }

  Key() = default;
  ~Key();

  Key(const Key &key) = delete;
  Key(Key &&key);

  void set(const char bytes[], std::size_t length);

  bool operator==(const Key &k) const {
    if (k.getKeyLen() != getKeyLen()) { return false; }
    return std::memcmp(&k[0], data, getKeyLen()) == 0;
  }

  uint8_t &operator[](std::size_t i);
  const uint8_t &operator[](std::size_t i) const;
  KeyLen getKeyLen() const;
};

inline uint8_t &Key::operator[](std::size_t i) {
  assert(i < len);
  return data[i];
}

inline const uint8_t &Key::operator[](std::size_t i) const {
  assert(i < len);
  return data[i];
}

inline KeyLen Key::getKeyLen() const { return len; }

inline Key::~Key() {
  if (len > stackLen) {
    delete[] data;
    data = nullptr;
  }
}

inline Key::Key(Key &&key) {
  len = key.len;
  if (len > stackLen) {
    data     = key.data;
    key.data = nullptr;
  } else {
    memcpy(stackKey, key.stackKey, key.len);
    data = stackKey;
  }
}

// All keys must end with null terminator
inline void Key::set(const char bytes[], std::size_t length) {
  if (len > stackLen) { delete[] data; }
  assert(length > 0);
  auto mustAppendTerminatedNull = bytes[length - 1] != NULL_TERMINATOR;
  length += static_cast<size_t>(mustAppendTerminatedNull);
  if (length <= stackLen) {
    memcpy(stackKey, bytes, length);
    data = stackKey;
  } else {
    data = new uint8_t[length];
    memcpy(data, bytes, length);
  }
  if (mustAppendTerminatedNull) { data[length - 1] = NULL_TERMINATOR; }
  len = length;
}

#endif  // ART_KEY_H
