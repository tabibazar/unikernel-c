// Same fault scope, but each iteration reduces the product two ways and counts
// faults for each: the compiler's software __umodti3 (a call, interruptible),
// and a single inline-asm divq (one instruction, atomic w.r.t. interrupts).
// Run WITHOUT masking interrupts. If the mechanism is "interrupt lands inside
// the software routine", the software path faults and the single instruction
// does not, in the very same run under the very same conditions.
#include <stdio.h>
#include <stdint.h>
#ifndef ITERS
#define ITERS 200000000ULL
#endif
#define A_BASE 0x9E3779B97F4A7C15ULL
#define A_MUL  0xC2B2AE3D27D4EB4FULL
#define A_MOD  0xFFFFFFFFFFFFFFC5ULL
static volatile uint64_t va = A_BASE, vb = A_MUL, vm = A_MOD;

static inline uint64_t mod_divq(uint64_t hi, uint64_t lo, uint64_t m) {
    uint64_t q, r;
    __asm__ ("divq %4" : "=a"(q), "=d"(r) : "a"(lo), "d"(hi), "r"(m));
    return r;   // one atomic instruction; quotient fits in 64 bits for these operands
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    uint64_t a = va, b = vb, m = vm;
    __uint128_t rp = (__uint128_t)a * b;
    uint64_t rrem = (uint64_t)(rp % m);
    printf("SCOPE_START iters=%llu rem=%016llx\n",
           (unsigned long long)ITERS, (unsigned long long)rrem);

    uint64_t soft_bad = 0, div_bad = 0;
    for (uint64_t i = 0; i < ITERS; i++) {
        uint64_t la = va, lb = vb, lm = vm;
        __uint128_t p = (__uint128_t)la * lb;
        uint64_t soft = (uint64_t)(p % lm);                 // call __umodti3
        uint64_t hard = mod_divq((uint64_t)(p >> 64), (uint64_t)p, lm);  // one divq
        if (soft != rrem) soft_bad++;
        if (hard != rrem) div_bad++;
    }
    printf("SCOPE_DONE iters=%llu software_umodti3_bad=%llu single_divq_bad=%llu\n",
           (unsigned long long)ITERS,
           (unsigned long long)soft_bad, (unsigned long long)div_bad);
    return 0;
}
