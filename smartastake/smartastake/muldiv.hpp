#pragma once
// 256-bit (a * b) / den for reward-per-token math. SCALE is 1e18, so
// stake * accumulator can exceed 2^128. Truncates toward zero.
#include <cstdint>

#ifndef SMARTASTAKE_CHECK
#define SMARTASTAKE_CHECK(cond, msg) eosio::check(static_cast<bool>(cond), msg)
#endif

inline uint128_t muldiv(uint128_t a, uint128_t b, uint128_t den) {
  SMARTASTAKE_CHECK(den != 0, "div0");
  uint64_t a0 = static_cast<uint64_t>(a);
  uint64_t a1 = static_cast<uint64_t>(a >> 64);
  uint64_t b0 = static_cast<uint64_t>(b);
  uint64_t b1 = static_cast<uint64_t>(b >> 64);

  uint128_t p00 = static_cast<uint128_t>(a0) * b0;
  uint128_t p01 = static_cast<uint128_t>(a0) * b1;
  uint128_t p10 = static_cast<uint128_t>(a1) * b0;
  uint128_t p11 = static_cast<uint128_t>(a1) * b1;

  uint64_t w0 = static_cast<uint64_t>(p00);
  uint128_t mid = (p00 >> 64) + static_cast<uint64_t>(p01) + static_cast<uint64_t>(p10);
  uint64_t w1 = static_cast<uint64_t>(mid);
  uint128_t hi = (p01 >> 64) + (p10 >> 64) + p11 + (mid >> 64);
  uint64_t w2 = static_cast<uint64_t>(hi);
  uint64_t w3 = static_cast<uint64_t>(hi >> 64);

  if (w3 == 0 && w2 == 0) {
    uint128_t prod = (static_cast<uint128_t>(w1) << 64) | w0;
    return prod / den;
  }

  uint64_t num[4] = {w0, w1, w2, w3};
  uint64_t rem[4] = {0, 0, 0, 0};
  uint64_t quo[4] = {0, 0, 0, 0};

  for (int i = 255; i >= 0; --i) {
    uint64_t carry = 0;
    for (int k = 0; k < 4; ++k) {
      uint64_t next = rem[k] >> 63;
      rem[k] = (rem[k] << 1) | carry;
      carry = next;
    }
    SMARTASTAKE_CHECK(carry == 0, "muldiv rem overflow");
    if ((num[i >> 6] >> (i & 63)) & 1ull) {
      rem[0] |= 1ull;
    }
    uint128_t rem_lo = (static_cast<uint128_t>(rem[1]) << 64) | rem[0];
    bool ge = (rem[2] | rem[3]) != 0 || rem_lo >= den;
    if (!ge) continue;
    if (rem_lo >= den) {
      rem_lo -= den;
      rem[0] = static_cast<uint64_t>(rem_lo);
      rem[1] = static_cast<uint64_t>(rem_lo >> 64);
    } else {
      rem_lo -= den;
      rem[0] = static_cast<uint64_t>(rem_lo);
      rem[1] = static_cast<uint64_t>(rem_lo >> 64);
      if (rem[2] == 0) {
        rem[2] = ~0ull;
        rem[3] -= 1;
      } else {
        rem[2] -= 1;
      }
    }
    quo[i >> 6] |= 1ull << (i & 63);
  }
  SMARTASTAKE_CHECK(quo[2] == 0 && quo[3] == 0, "muldiv overflow");
  return (static_cast<uint128_t>(quo[1]) << 64) | quo[0];
}
