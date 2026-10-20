// The single global pseudo-random generator (01-runtime.md §6).
#pragma once
#include <cstdint>

namespace rng {
// One draw: a number 0 .. 2^31-1.
uint32_t R();
// random(n) = R % n with C semantics; no draw (result 0) when n == 0.
int rnd(int n);
uint64_t state();
void setState(uint64_t s);
extern uint64_t drawCount;
}  // namespace rng
