// selfcheck.c — minimal reproducer for nondeterministic integer arithmetic.
//
// Computes the SAME modular exponentiation several million times and counts how
// often the answer differs from the first one. There is no primality logic, no
// allocation, no I/O inside the loop, and no libc call except the final printf:
// just 64-bit multiply, modulo and shift, via __uint128_t.
//
// On any correct runtime the mismatch count is exactly zero, because the inputs
// never change. A non-zero count means the machine returned two different
// answers to the same arithmetic question.
//
//   linux:     gcc -O2 -o selfcheck selfcheck.c && ./selfcheck
//   baremetal: cp selfcheck.c BareMetal-App/ && ./1-build.sh selfcheck.c && ./2-run.sh

#include <stdio.h>
#include <stdint.h>

#ifndef ITERS
#define ITERS 20000000ULL
#endif

// Fixed operands, chosen to exercise the full 64-bit range.
#define A_BASE 0x9E3779B97F4A7C15ULL
#define A_EXP  0x7FFFFFFFFFFFFFFDULL
#define A_MOD  0xFFFFFFFFFFFFFFC5ULL   // largest prime below 2^64

static uint64_t mulmod(uint64_t a, uint64_t b, uint64_t m) {
    return (uint64_t)((__uint128_t)a * b % m);
}

static uint64_t powmod(uint64_t base, uint64_t exp, uint64_t m) {
    uint64_t r = 1;
    base %= m;
    while (exp) {
        if (exp & 1) r = mulmod(r, base, m);
        base = mulmod(base, base, m);
        exp >>= 1;
    }
    return r;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);

    const uint64_t expected = powmod(A_BASE, A_EXP, A_MOD);
    printf("SELFCHECK_START iters=%llu expected=%llu\n",
           (unsigned long long)ITERS, (unsigned long long)expected);

    uint64_t mismatches = 0, first_bad = 0, first_bad_iter = 0;
    for (uint64_t i = 0; i < ITERS; i++) {
        uint64_t got = powmod(A_BASE, A_EXP, A_MOD);
        if (got != expected) {
            if (!mismatches) { first_bad = got; first_bad_iter = i; }
            mismatches++;
        }
    }

    printf("SELFCHECK_DONE iters=%llu mismatches=%llu rate=%.3e first_bad=%llu at_iter=%llu\n",
           (unsigned long long)ITERS, (unsigned long long)mismatches,
           ITERS ? (double)mismatches / (double)ITERS : 0.0,
           (unsigned long long)first_bad, (unsigned long long)first_bad_iter);
    return 0;
}
