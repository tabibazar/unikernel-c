// faultscope.c -- catch the arithmetic fault at the level of one operation.
//
// selfcheck chains ~100 modular multiplies per exponentiation, so a single
// corrupted intermediate diffuses into a final value that looks unrelated to
// the true one. This repeats ONE fixed mulmod and splits it into its two
// primitives -- the 64x64->128 multiply and the 128%64 reduction -- so a
// mismatch can be blamed on one or the other, and the exact bits that changed
// can be read off.
//
// Inputs are volatile so the compiler reloads and recomputes every iteration
// rather than hoisting the constant answer out of the loop.

#include <stdio.h>
#include <stdint.h>

#ifndef ITERS
#define ITERS 200000000ULL
#endif
#define A_BASE 0x9E3779B97F4A7C15ULL
#define A_MUL  0xC2B2AE3D27D4EB4FULL
#define A_MOD  0xFFFFFFFFFFFFFFC5ULL

static volatile uint64_t va = A_BASE, vb = A_MUL, vm = A_MOD;

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);

    uint64_t a = va, b = vb, m = vm;
    __uint128_t rp = (__uint128_t)a * b;              // reference product
    uint64_t rlo = (uint64_t)rp, rhi = (uint64_t)(rp >> 64);
    uint64_t rrem = (uint64_t)(rp % m);               // reference remainder
    printf("SCOPE_START iters=%llu prod=%016llx:%016llx rem=%016llx\n",
           (unsigned long long)ITERS,
           (unsigned long long)rhi, (unsigned long long)rlo,
           (unsigned long long)rrem);

    uint64_t mul_bad = 0, mod_bad = 0, both = 0;
    int shown = 0;

    for (uint64_t i = 0; i < ITERS; i++) {
        uint64_t la = va, lb = vb, lm = vm;
        __uint128_t p = (__uint128_t)la * lb;
        uint64_t plo = (uint64_t)p, phi = (uint64_t)(p >> 64);
        uint64_t rem = (uint64_t)(p % lm);

        int mb = (plo != rlo) || (phi != rhi);
        int db = (rem != rrem);
        if (mb) mul_bad++;
        if (db) mod_bad++;
        if (mb && db) both++;

        if ((mb || db) && shown < 12) {
            shown++;
            printf("EVENT i=%llu mul=%d mod=%d  prod=%016llx:%016llx (xor %016llx:%016llx)  rem=%016llx (xor %016llx)\n",
                   (unsigned long long)i, mb, db,
                   (unsigned long long)phi, (unsigned long long)plo,
                   (unsigned long long)(phi ^ rhi), (unsigned long long)(plo ^ rlo),
                   (unsigned long long)rem, (unsigned long long)(rem ^ rrem));
        }
    }

    printf("SCOPE_DONE iters=%llu mul_bad=%llu mod_bad=%llu both=%llu mul_rate=%.3e mod_rate=%.3e\n",
           (unsigned long long)ITERS,
           (unsigned long long)mul_bad, (unsigned long long)mod_bad, (unsigned long long)both,
           (double)mul_bad / (double)ITERS, (double)mod_bad / (double)ITERS);
    return 0;
}
