#pragma once
#include <cmath>
#include <cstdint>

namespace tb {

// xoshiro256** seeded through splitmix64. Deterministic across platforms.
class Rng {
 public:
  explicit Rng(uint64_t seed) {
    uint64_t x = seed;
    for (auto& v : s_) v = splitmix(x);
  }
  uint64_t next() {
    const uint64_t r = rotl(s_[1] * 5, 7) * 9;
    const uint64_t t = s_[1] << 17;
    s_[2] ^= s_[0];
    s_[3] ^= s_[1];
    s_[1] ^= s_[2];
    s_[0] ^= s_[3];
    s_[2] ^= t;
    s_[3] = rotl(s_[3], 45);
    return r;
  }
  double uni() { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }
  double uni(double a, double b) { return a + (b - a) * uni(); }
  int64_t range(int64_t a, int64_t b) { return a + static_cast<int64_t>(uni() * static_cast<double>(b - a + 1)); }
  bool chance(double p) { return uni() < p; }
  double normal() {
    const double u1 = std::fmax(uni(), 1e-12), u2 = uni();
    return std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
  }
  // Multiplicative noise with mean 1.
  double lognormal(double sigma) { return std::exp(sigma * normal() - sigma * sigma / 2); }
  double expo(double rate) { return -std::log(std::fmax(uni(), 1e-12)) / rate; }

 private:
  static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
  static uint64_t splitmix(uint64_t& x) {
    uint64_t z = (x += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
  }
  uint64_t s_[4];
};

}  // namespace tb
