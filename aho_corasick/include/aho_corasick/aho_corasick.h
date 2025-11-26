#pragma once

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "aho_corasick/interval.h"
#include "utils/epoch_handler.h"

namespace aho_corasick {

constexpr uint8_t MAX_PREFIX_LEN = 10U;

struct ArtNode {
  // TODO: Implement optimistic lock coupling + garbage collection with epoch-based + obsolete flag
  using PatternIndexType = uint32_t;

  enum NodeType : uint8_t {
    NODE4   = 1,
    NODE16  = 2,
    NODE48  = 3,
    NODE256 = 4,
  };

  NodeType type;
  uint8_t num_children;
  uint8_t prefix_len;
  uint8_t prefix[MAX_PREFIX_LEN];

  ArtNode()  = default;
  ~ArtNode() = default;

  inline auto CheckPrefix(const unsigned char *key, uint64_t key_len, uint64_t depth) {
    auto max_cmp = std::min(static_cast<uint64_t>(std::min(prefix_len, MAX_PREFIX_LEN)), key_len - depth);
    auto idx     = 0UL;
    for (idx = 0; idx < max_cmp; idx++) {
      if (prefix[idx] != key[depth + idx]) { return idx; }
    }
    return idx;
  }

  inline static void CopyHeader(ArtNode *dest, ArtNode *src) {
    dest->num_children = src->num_children;
    dest->prefix_len   = src->prefix_len;
    memcpy(dest->prefix, src->prefix, std::min(MAX_PREFIX_LEN, src->prefix_len));
  }
};

template <uint16_t KeyCount, uint16_t ChildrenCount>
struct ArtNodeType {
  ArtNode n;
  uint8_t keys[KeyCount];
  ArtNode *children[ChildrenCount];

  /* Aho-Corasick integration: TODO */
  ArtNode *failure_link;
  std::vector<ArtNode::PatternIndexType> output_indices;

  ArtNodeType(ArtNode::NodeType type) { n.type = type; }

  ~ArtNodeType() = default;

  inline auto SizeInBytes() { return sizeof(*this) + output_indices.capacity() * sizeof(ArtNode::PatternIndexType); }
};

using ArtNode4   = ArtNodeType<4, 4>;
using ArtNode16  = ArtNodeType<16, 16>;
using ArtNode48  = ArtNodeType<256, 48>;
using ArtNode256 = ArtNodeType<0, 256>;

class ArtTree {
 public:
  ArtTree(uint8_t no_threads);
  ~ArtTree();

  auto TraverseTree() -> uint64_t;
  void Insert(uint8_t *keyword_data, uint64_t keyword_size, ArtNode::PatternIndexType keyword_aux_index);
  auto Contain(uint8_t *keyword_data, uint64_t keyword_size) -> bool;
  auto ParseText(std::string_view text) -> std::vector<Emit>;

 private:
  ArtNode *root_;
  utils::EpochHandler epoch_; /* Epoch management */

  //--------------------------------------------
  auto NewArtNode(ArtNode::NodeType type) -> ArtNode *;
  void DestroyArtNode(ArtNode *node);

  // Various node utilities
  auto FindChild(ArtNode *node, uint8_t ch) -> ArtNode **;
  void AddChild(ArtNode *node, ArtNode **node_ref, uint8_t ch, ArtNode *child);
  void AddChild4(ArtNode4 *node, ArtNode **node_ref, uint8_t ch, ArtNode *child);
  void AddChild16(ArtNode16 *node, ArtNode **node_ref, uint8_t ch, ArtNode *child);
  void AddChild48(ArtNode48 *node, ArtNode **node_ref, uint8_t ch, ArtNode *child);
  void AddChild256(ArtNode256 *node, ArtNode **node_ref, uint8_t ch, ArtNode *child);

  // Various tree utilities
  auto RecursiveInsert(ArtNode *node, ArtNode **node_ref, uint8_t *keyword_data, uint64_t keyword_size, uint64_t depth)
    -> ArtNode *;
  void AddEmit(ArtNode *node, ArtNode::PatternIndexType aux_index);
  auto TraverseTreeAux(ArtNode *node) -> uint64_t;
};

}  // namespace aho_corasick
