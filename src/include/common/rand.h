#pragma once

#include "common/typedef.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <random>
#include <utility>
#include <vector>

class MersenneTwister {
 private:
  static constexpr int NN       = 312;
  static constexpr int MM       = 156;
  static constexpr u64 MATRIX_A = 0xB5026F5AA96619E9ULL;
  static constexpr u64 UM       = 0xFFFFFFFF80000000ULL;
  static constexpr u64 LM       = 0x7FFFFFFFULL;
  u64 mt_[NN];
  int mti_;
  void Init(u64 seed);

 public:
  explicit MersenneTwister(u64 seed = 19650218ULL);
  auto Rand() -> u64;
};

class ZipfGenerator {
 private:
  double norm_c_;                 // Normalization constant
  int n_elements_;                // Number of elements
  std::vector<double> sum_prob_;  // pre calculate the sum probabilities
 public:
  ZipfGenerator(double theta, int n_elements);
  auto Rand() -> int;
  auto NoElements() -> int;
};

class RandomGenerator {
 public:
  // ATTENTION: interval [min, max)
  static auto GetRandU64(u64 min, u64 max) -> u64;
  static auto GetRandU64() -> u64;
  static thread_local std::minstd_rand prng;

  // ATTENTION: interval [min, max)
  template <typename T>
  static auto GetRand(T min, T max) -> T {
    const u64 rand = GetRandU64(min, max);
    return static_cast<T>(rand);
  }

  static void GetRandString(u8 *dst, u64 size);
};
