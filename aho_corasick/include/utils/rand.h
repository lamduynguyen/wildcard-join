#pragma once

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <random>
#include <utility>
#include <vector>

namespace utils {

class MersenneTwister {
 private:
  static constexpr int NN            = 312;
  static constexpr int MM            = 156;
  static constexpr uint64_t MATRIX_A = 0xB5026F5AA96619E9ULL;
  static constexpr uint64_t UM       = 0xFFFFFFFF80000000ULL;
  static constexpr uint64_t LM       = 0x7FFFFFFFULL;
  uint64_t mt_[NN];
  int mti_;
  void Init(uint64_t seed);

 public:
  explicit MersenneTwister(uint64_t seed = 19650218ULL);
  auto Rand() -> uint64_t;
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
  static auto GetRanduint64_t(uint64_t min, uint64_t max) -> uint64_t;
  static auto GetRanduint64_t() -> uint64_t;
  static thread_local std::minstd_rand prng;

  // ATTENTION: interval [min, max)
  template <typename T>
  static auto GetRand(T min, T max) -> T {
    uint64_t rand = GetRanduint64_t(min, max);
    return static_cast<T>(rand);
  }

  static void GetRandString(uint8_t *dst, uint64_t size);
  static void GetRandRepetitiveString(uint8_t *dst, uint64_t rep_size, uint64_t size);
};

auto RandBool() -> bool;
auto Rand(int n) -> int;                                  // [0, n)
auto UniformRand(int low, int high) -> int;               // [low, high]
auto UniformRandExcept(int low, int high, int v) -> int;  // [low, high]
auto NonUniformRand(int a, int x, int y, int c = 42) -> int;

}  // namespace utils