#include "core/rng.h"

namespace rng {
static uint64_t S = 1;
uint64_t drawCount = 0;

uint32_t R() {
    S = S * 0x000015A400004E35ULL + 1;
    drawCount++;
    return (uint32_t)(S >> 32) & 0x7FFFFFFFu;
}

int rnd(int n) {
    if (n == 0) return 0;
    uint32_t r = R();
    if (n < 0) {
        long long m = -(long long)n;
        return (int)(r % m);
    }
    return (int)(r % (uint32_t)n);
}

uint64_t state() { return S; }
void setState(uint64_t s) { S = s; }
}  // namespace rng
