#include "aho_corasick/aho_corasick.h"

#include <cassert>
#include <cstring>

#ifdef __i386__
#include <emmintrin.h>
#else
#ifdef __amd64__
#include <emmintrin.h>
#endif
#endif

namespace aho_corasick {

ArtTree::ArtTree() : root_(nullptr) {}

ArtTree::~ArtTree() { DestroyArtNode(root_); }

auto ArtTree::TraverseTree() -> uint64_t { return TraverseTreeAux(root_); }

void ArtTree::Insert(uint8_t *keyword_data, uint64_t keyword_size, ArtNode::PatternIndexType keyword_aux_index) {
  assert(keyword_size > 0);
  auto node = RecursiveInsert(root_, &root_, keyword_data, keyword_size, 0);
  switch (node->type) {
    case ArtNode::NODE4: {
      auto p = reinterpret_cast<ArtNode4 *>(node);
      p->output_indices.emplace_back(keyword_aux_index);
    } break;
    case ArtNode::NODE16: {
      auto p = reinterpret_cast<ArtNode16 *>(node);
      p->output_indices.emplace_back(keyword_aux_index);
    } break;
    case ArtNode::NODE48: {
      auto p = reinterpret_cast<ArtNode48 *>(node);
      p->output_indices.emplace_back(keyword_aux_index);
    } break;
    case ArtNode::NODE256: {
      auto p = reinterpret_cast<ArtNode256 *>(node);
      p->output_indices.emplace_back(keyword_aux_index);
    } break;

    default: break;
  }
}

// ------------------------------------------------------------------------------------------------
auto ArtTree::NewArtNode(ArtNode::NodeType type) -> ArtNode * {
  ArtNode *ret = nullptr;
  switch (type) {
    case ArtNode::NODE4: {
      ret = (ArtNode *)calloc(1, sizeof(ArtNode4));
    } break;
    case ArtNode::NODE16: {
      ret = (ArtNode *)calloc(1, sizeof(ArtNode16));
    } break;
    case ArtNode::NODE48: {
      ret = (ArtNode *)calloc(1, sizeof(ArtNode48));
    } break;
    case ArtNode::NODE256: {
      ret = (ArtNode *)calloc(1, sizeof(ArtNode256));
    } break;
    default: break;
  }
  assert(ret != nullptr);
  ret->type = type;
  return ret;
}

void ArtTree::DestroyArtNode(ArtNode *node) {
  switch (node->type) {
    case ArtNode::NODE4: {
      auto p = reinterpret_cast<ArtNode4 *>(node);
      for (auto i = 0; i < p->n.num_children; i++) { DestroyArtNode(p->children[i]); }
    } break;
    case ArtNode::NODE16: {
      auto p = reinterpret_cast<ArtNode16 *>(node);
      for (auto i = 0; i < p->n.num_children; i++) { DestroyArtNode(p->children[i]); }
    } break;
    case ArtNode::NODE48: {
      auto p = reinterpret_cast<ArtNode48 *>(node);
      for (auto i = 0; i < p->n.num_children; i++) { DestroyArtNode(p->children[i]); }
    } break;
    case ArtNode::NODE256: {
      auto p = reinterpret_cast<ArtNode256 *>(node);
      for (auto i = 0; i < p->n.num_children; i++) { DestroyArtNode(p->children[i]); }
    } break;

    default: break;
  }
  free(node);
}

// ------------------------------------------------------------------------------------------------
auto ArtTree::FindChild(ArtNode *n, uint8_t ch) -> ArtNode ** {
  switch (n->type) {
    case ArtNode::NodeType::NODE4: {
      auto p = reinterpret_cast<ArtNode4 *>(n);
      for (auto i = 0; i < n->num_children; i++) {
        if (p->keys[i] == ch) { return &p->children[i]; }
      }
    } break;
    case ArtNode::NodeType::NODE16: {
      auto p = reinterpret_cast<ArtNode16 *>(n);
#ifdef __i386__
      // Compare the key to all 16 stored keys
      __m128i cmp;
      cmp = _mm_cmpeq_epi8(_mm_set1_epi8(ch), _mm_loadu_si128((__m128i *)p->keys));
      // Use a mask to ignore children that don't exist
      auto mask     = (1 << n->num_children) - 1;
      auto bitfield = _mm_movemask_epi8(cmp) & mask;
#else
#ifdef __amd64__
      // Compare the key to all 16 stored keys
      __m128i cmp;
      cmp = _mm_cmpeq_epi8(_mm_set1_epi8(ch), _mm_loadu_si128((__m128i *)p->keys));
      // Use a mask to ignore children that don't exist
      auto mask     = (1 << n->num_children) - 1;
      auto bitfield = _mm_movemask_epi8(cmp) & mask;
#else
      // Compare the key to all 16 stored keys
      bitfield = 0;
      for (i = 0; i < 16; ++i) {
        if (p->keys[i] == c) { bitfield |= (1 << i); }
      }
      // Use a mask to ignore children that don't exist
      auto mask = (1 << n->num_children) - 1;
      auto bitfield &= mask;
#endif
#endif
      /*
       * If we have a match (any bit set) then we can
       * return the pointer match using ctz to get
       * the index.
       */
      if (bitfield) { return &p->children[__builtin_ctz(bitfield)]; }
    } break;
    case ArtNode::NodeType::NODE48: {
      auto p = reinterpret_cast<ArtNode48 *>(n);
      auto i = p->keys[ch];
      if (i) { return &p->children[i - 1]; }
    } break;
    case ArtNode::NodeType::NODE256: {
      auto p = reinterpret_cast<ArtNode256 *>(n);
      if (p->children[ch]) { return &p->children[ch]; }
      break;
    }
    default: break;
  }
  return nullptr;
}

void ArtTree::AddChild(ArtNode *node, ArtNode **node_ref, uint8_t ch, ArtNode *child) {
  switch (node->type) {
    case ArtNode::NODE4: {
      auto p = reinterpret_cast<ArtNode4 *>(node);
      AddChild4(p, node_ref, ch, child);
    } break;
    case ArtNode::NODE16: {
      auto p = reinterpret_cast<ArtNode16 *>(node);
      AddChild16(p, node_ref, ch, child);
    } break;
    case ArtNode::NODE48: {
      auto p = reinterpret_cast<ArtNode48 *>(node);
      AddChild48(p, node_ref, ch, child);
    } break;
    case ArtNode::NODE256: {
      auto p = reinterpret_cast<ArtNode256 *>(node);
      AddChild256(p, node_ref, ch, child);
    } break;

    default: break;
  }
}

void ArtTree::AddChild4(ArtNode4 *p, ArtNode **node_ref, uint8_t ch, ArtNode *child) {
  if (p->n.num_children < 4) {
    int idx;
    for (idx = 0; idx < p->n.num_children; idx++) {
      if (ch < p->keys[idx]) { break; }
    }

    // Shift to make room
    memmove(p->keys + idx + 1, p->keys + idx, p->n.num_children - idx);
    memmove(p->children + idx + 1, p->children + idx, (p->n.num_children - idx) * sizeof(ArtNode *));

    // Insert element
    p->keys[idx]     = ch;
    p->children[idx] = child;
    p->n.num_children++;
  } else {
    auto new_node = reinterpret_cast<ArtNode16 *>(NewArtNode(ArtNode::NodeType::NODE16));
    *node_ref     = reinterpret_cast<ArtNode *>(new_node);
    memcpy(new_node->children, p->children, sizeof(ArtNode *) * p->n.num_children);
    memcpy(new_node->keys, p->keys, sizeof(uint8_t) * p->n.num_children);
    ArtNode::CopyHeader(*node_ref, reinterpret_cast<ArtNode *>(p));
    free(p);
    AddChild16(new_node, node_ref, ch, child);
  }
}

void ArtTree::AddChild16(ArtNode16 *p, ArtNode **node_ref, uint8_t ch, ArtNode *child) {
  if (p->n.num_children < 16) {
    unsigned mask = (1 << p->n.num_children) - 1;

// support non-x86 architectures
#ifdef __i386__
    __m128i cmp;
    // Compare the key to all 16 stored keys
    cmp = _mm_cmplt_epi8(_mm_set1_epi8(c), _mm_loadu_si128((__m128i *)p->keys));
    // Use a mask to ignore children that don't exist
    unsigned bitfield = _mm_movemask_epi8(cmp) & mask;
#else
#ifdef __amd64__
    __m128i cmp;
    // Compare the key to all 16 stored keys
    cmp = _mm_cmplt_epi8(_mm_set1_epi8(ch), _mm_loadu_si128((__m128i *)p->keys));
    // Use a mask to ignore children that don't exist
    unsigned bitfield = _mm_movemask_epi8(cmp) & mask;
#else
    // Compare the key to all 16 stored keys
    unsigned bitfield = 0;
    for (short i = 0; i < 16; ++i) {
      if (c < p->keys[i]) { bitfield |= (1 << i); }
    }
    // Use a mask to ignore children that don't exist
    bitfield &= mask;
#endif
#endif

    // Check if less than any
    unsigned idx;
    if (bitfield) {
      idx = __builtin_ctz(bitfield);
      memmove(p->keys + idx + 1, p->keys + idx, p->n.num_children - idx);
      memmove(p->children + idx + 1, p->children + idx, (p->n.num_children - idx) * sizeof(ArtNode *));
    } else {
      idx = p->n.num_children;
    }

    // Set the child
    p->keys[idx]     = ch;
    p->children[idx] = child;
    p->n.num_children++;
  } else {
    auto new_node = reinterpret_cast<ArtNode48 *>(NewArtNode(ArtNode::NodeType::NODE48));
    *node_ref     = reinterpret_cast<ArtNode *>(new_node);
    memcpy(new_node->children, p->children, sizeof(ArtNode *) * p->n.num_children);
    for (int i = 0; i < p->n.num_children; i++) { new_node->keys[p->keys[i]] = i + 1; }
    ArtNode::CopyHeader(*node_ref, reinterpret_cast<ArtNode *>(p));
    free(p);
    AddChild48(new_node, node_ref, ch, child);
  }
}

void ArtTree::AddChild48(ArtNode48 *p, ArtNode **node_ref, uint8_t ch, ArtNode *child) {
  if (p->n.num_children < 48) {
    int pos = 0;
    for (; p->children[pos]; pos++) {}
    p->children[pos] = child;
    p->keys[ch]      = pos + 1;
    p->n.num_children++;
  } else {
    auto new_node = reinterpret_cast<ArtNode256 *>(NewArtNode(ArtNode::NodeType::NODE256));
    *node_ref     = reinterpret_cast<ArtNode *>(new_node);
    for (int i = 0; i < 256; i++) {
      if (p->keys[i]) { new_node->children[i] = p->children[p->keys[i] - 1]; }
    }
    ArtNode::CopyHeader(*node_ref, reinterpret_cast<ArtNode *>(p));
    free(p);
    AddChild256(new_node, node_ref, ch, child);
  }
}

void ArtTree::AddChild256(ArtNode256 *p, ArtNode **node_ref, uint8_t ch, ArtNode *child) {
  p->n.num_children++;
  p->children[ch] = child;
}

// ------------------------------------------------------------------------------------------------
auto ArtTree::RecursiveInsert(ArtNode *node, ArtNode **ref, uint8_t *keyword_data, uint64_t keyword_size,
                              uint64_t depth) -> ArtNode * {
  // If we are at a nullptr node, inject the smallest node type
  if (!node) {
    *ref = NewArtNode(ArtNode::NodeType::NODE4);
    node = *ref;
    return node;
  }
  // Not sharing prefix with the current ART node, create a new node to store data
  auto p = node->CheckPrefix(keyword_data, keyword_size, depth);
  if (p != node->prefix_len) {
    auto new_node = NewArtNode(ArtNode::NodeType::NODE4);
    auto new_leaf = NewArtNode(ArtNode::NodeType::NODE4);
    *ref          = new_node;
    assert(memcmp(&keyword_data[depth], node->prefix, p) == 0);
    AddChild4(reinterpret_cast<ArtNode4 *>(new_node), ref, keyword_data[depth + p], new_leaf);
    AddChild4(reinterpret_cast<ArtNode4 *>(new_node), ref, node->prefix[p], node);
    // Copy shared prefix to new node
    new_node->prefix_len = p;
    std::memcpy(new_node->prefix, node->prefix, p);
    // Shrink prefix of current node
    node->prefix_len -= p + 1;
    std::memmove(node->prefix, node->prefix + p + 1, node->prefix_len);
    return new_leaf;
  }
  // Sharing prefix with current ART node, continue traversing the tree
  depth += node->prefix_len;
  auto next_ref = FindChild(node, keyword_data[depth]);
  if (*next_ref) {
    return RecursiveInsert(*next_ref, next_ref, keyword_data, keyword_size, depth);
  } else {
    auto new_leaf = NewArtNode(ArtNode::NodeType::NODE4);
    AddChild(*next_ref, next_ref, keyword_data[depth], new_leaf);
    return new_leaf;
  }
}

auto ArtTree::TraverseTreeAux(ArtNode *node) -> uint64_t {
  auto ret = 0ULL;
  if (!node) { return ret; }
  switch (node->type) {
    case ArtNode::NODE4: {
      auto p = reinterpret_cast<ArtNode4 *>(node);
      ret    = p->SizeInBytes();
      for (auto i = 0; i < p->n.num_children; i++) { ret += TraverseTreeAux(p->children[i]); }
    } break;
    case ArtNode::NODE16: {
      auto p = reinterpret_cast<ArtNode16 *>(node);
      ret    = p->SizeInBytes();
      for (auto i = 0; i < p->n.num_children; i++) { ret += TraverseTreeAux(p->children[i]); }
    } break;
    case ArtNode::NODE48: {
      auto p = reinterpret_cast<ArtNode48 *>(node);
      ret    = p->SizeInBytes();
      for (auto i = 0; i < p->n.num_children; i++) { ret += TraverseTreeAux(p->children[i]); }
    } break;
    case ArtNode::NODE256: {
      auto p = reinterpret_cast<ArtNode256 *>(node);
      ret    = p->SizeInBytes();
      for (auto i = 0; i < p->n.num_children; i++) { ret += TraverseTreeAux(p->children[i]); }
    } break;

    default: break;
  }
  return ret;
}

}  // namespace aho_corasick