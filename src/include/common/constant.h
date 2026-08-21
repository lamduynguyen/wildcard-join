#pragma once

#include "common/typedef.h"

namespace aho_corasick {

// -------------------------------------------------------------------------------------
static constexpr u64 KB = 1024ULL;
static constexpr u64 MB = 1024ULL * 1024;
static constexpr u64 GB = 1024ULL * 1024 * 1024;
// -------------------------------------------------------------------------------------
static constexpr u16 MAX_NUMBER_OF_WORKER = 256;
// -------------------------------------------------------------------------------------
static constexpr u32 BLK_BLOCK_SIZE     = 4096;
static constexpr u32 CPU_CACHELINE_SIZE = 64;

}  // namespace aho_corasick
