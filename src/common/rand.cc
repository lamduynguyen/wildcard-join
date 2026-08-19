#include "common/rand.h"

#include <atomic>
#include <cassert>

static std::atomic<u64> mt_counter = 0;
static thread_local MersenneTwister mt_generator;
thread_local std::minstd_rand RandomGenerator::prng = std::minstd_rand(std::random_device()());

MersenneTwister::MersenneTwister(u64 seed) : mti_(NN + 1) { Init(seed + (mt_counter++)); }

void MersenneTwister::Init(u64 seed) {
  mt_[0] = seed;
  for (mti_ = 1; mti_ < NN; mti_++) {
    mt_[mti_] = (6364136223846793005ULL * (mt_[mti_ - 1] ^ (mt_[mti_ - 1] >> 62)) + mti_);
  }
}

auto MersenneTwister::Rand() -> u64 {
  int i;
  u64 x;
  static const u64 mag01[2] = {0ULL, MATRIX_A};

  if (mti_ >= NN) { /* generate NN words at one time */
    for (i = 0; i < NN - MM; i++) {
      x      = (mt_[i] & UM) | (mt_[i + 1] & LM);
      mt_[i] = mt_[i + MM] ^ (x >> 1) ^ mag01[static_cast<int>(x & 1ULL)];
    }
    for (; i < NN - 1; i++) {
      x      = (mt_[i] & UM) | (mt_[i + 1] & LM);
      mt_[i] = mt_[i + (MM - NN)] ^ (x >> 1) ^ mag01[static_cast<int>(x & 1ULL)];
    }
    x           = (mt_[NN - 1] & UM) | (mt_[0] & LM);
    mt_[NN - 1] = mt_[MM - 1] ^ (x >> 1) ^ mag01[static_cast<int>(x & 1ULL)];
    mti_        = 0;
  }

  x = mt_[mti_++];
  x ^= (x >> 29) & 0x5555555555555555ULL;
  x ^= (x << 17) & 0x71D67FFFEDA60000ULL;
  x ^= (x << 37) & 0xFFF7EEE000000000ULL;
  x ^= (x >> 43);

  return x;
}

auto RandomGenerator::GetRandU64(u64 min, u64 max) -> u64 {
  const u64 rand = min + (mt_generator.Rand() % (max - min));
  assert(rand < max);
  assert(rand >= min);
  return rand;
}

auto RandomGenerator::GetRandU64() -> u64 { return mt_generator.Rand(); }

void RandomGenerator::GetRandString(u8 *dst, u64 size) {
  for (u64 t_i = 0; t_i < size; t_i++) { dst[t_i] = GetRand(48, 123); }
}
