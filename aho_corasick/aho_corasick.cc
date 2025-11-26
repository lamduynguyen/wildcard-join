#include "aho_corasick/aho_corasick.h"

#include <cassert>
#include <cstring>

namespace aho_corasick {

void loadKey(TupleID TupleID, Key &key) {
  // Store the key of the tuple into the key vector
  // Implementation is database specific
  key.setKeyLen(sizeof(TupleID));
  reinterpret_cast<uint64_t *>(&key[0])[0] = __builtin_bswap64(TupleID);
}

AhoCorasick::AhoCorasick() : trie_(std::make_unique<ART_OLC::Tree>(loadKey)) {}

AhoCorasick::~AhoCorasick() = default;

}  // namespace aho_corasick