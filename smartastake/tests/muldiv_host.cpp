#include <cassert>
#include <cstdint>
#include <iostream>
#include <stdexcept>
using uint128_t = unsigned __int128;
#define SMARTASTAKE_CHECK(cond, msg) do { if (!(cond)) throw std::runtime_error(msg); } while (0)
#include "../smartastake/muldiv.hpp"

int main() {
  assert(muldiv(0, 5, 3) == 0);
  assert(muldiv(7, 5, 3) == 11);
  assert(muldiv(10, 1, 3) == 3);
  const uint128_t SCALE = 1000000000000000000ull;
  assert(muldiv(1000000, SCALE, 40000000) == 25000000000000000ull);
  assert(muldiv(30000000, 25000000000000000ull, SCALE) == 750000);
  assert(muldiv(10000000, 25000000000000000ull, SCALE) == 250000);
  uint128_t a = (uint128_t)1 << 100;
  uint128_t b = (uint128_t)1 << 40;
  uint128_t den = (uint128_t)1 << 20;
  assert(muldiv(a, b, den) == ((uint128_t)1 << 120));
  assert(muldiv(5, 5, 3) == 8);
  std::cout << "muldiv host ok\n";
}
