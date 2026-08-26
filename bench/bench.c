// bench.c — fixed-work benchmark, identical source for Linux and BareMetal.
//
// Purpose: measure the cost of running a compute workload as a BareMetal
// unikernel under Firecracker versus the same code as an ordinary Linux
// process, ON THE SAME PHYSICAL HOST. Any comparison across different machines
// measures the machines, not the runtimes.
//
// No network, no libcurl, no TLS: this isolates compute so the number means one
// thing. The workload is the swarm's own inner loop — Miller-Rabin primality
// over a fixed candidate range — so it is representative rather than synthetic.
//
// It reports its own elapsed time from the guest clock, and prints unambiguous
// markers so a harness can also time it from outside. The two are compared:
// a guest clock that disagreed with the wall clock would invalidate the result.
//
//   linux:      gcc -O2 -o bench bench.c && ./bench
//   baremetal:  cp bench.c BareMetal-App/ && ./1-build.sh bench.c && ./2-run.sh

#include <stdio.h>
#include <stdint.h>
#include <time.h>

// Fixed work: candidates START + 2j for j in [0, WORK). Chosen so a run lasts
// roughly a minute, keeping the 1-second clock resolution to ~2% error.
#ifndef WORK
#define WORK   40000000ULL
#endif
#ifndef START
#define START  100000001ULL
#endif
// Repeating the identical range inside one process is a determinism check: the
// arithmetic is fixed, so every pass must return the same counts. Two passes
// that disagree indicate the runtime is corrupting the computation, not that
// the program is wrong.
#ifndef REPEAT
#define REPEAT 1
#endif

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

static int is_prime(uint64_t n) {
    if (n < 2) return 0;
    for (uint64_t p = 2; p < 38; p++) {
        if (p * p > n) return 1;
        if (n % p == 0) return n == p;
    }
    uint64_t d = n - 1;
    int r = 0;
    while ((d & 1) == 0) { d >>= 1; r++; }

    static const uint64_t witnesses[] = {2,3,5,7,11,13,17,19,23,29,31,37};
    for (size_t i = 0; i < sizeof(witnesses)/sizeof(witnesses[0]); i++) {
        uint64_t a = witnesses[i] % n;
        if (a == 0) continue;
        uint64_t x = powmod(a, d, n);
        if (x == 1 || x == n - 1) continue;
        int composite = 1;
        for (int j = 1; j < r; j++) {
            x = mulmod(x, x, n);
            if (x == n - 1) { composite = 0; break; }
        }
        if (composite) return 0;
    }
    return 1;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);

    printf("BENCH_START work=%llu start=%llu\n",
           (unsigned long long)WORK, (unsigned long long)START);

    for (int pass = 1; pass <= REPEAT; pass++) {
        time_t t0 = time(NULL);

        // Count primes and chain heads so the optimiser cannot discard the work.
        uint64_t primes = 0, sophie = 0;
        for (uint64_t j = 0; j < WORK; j++) {
            uint64_t p = START + 2 * j;
            if (is_prime(p)) {
                primes++;
                if (is_prime(2 * p + 1)) sophie++;   // a chain of at least 2
            }
        }

        time_t t1 = time(NULL);
        long elapsed = (long)(t1 - t0);
        if (elapsed <= 0) elapsed = 1;           // guard a coarse or stalled clock

        printf("BENCH_DONE pass=%d candidates=%llu primes=%llu sophie=%llu elapsed_s=%ld rate=%llu\n",
               pass, (unsigned long long)WORK, (unsigned long long)primes,
               (unsigned long long)sophie, elapsed,
               (unsigned long long)(WORK / (uint64_t)elapsed));
    }
    return 0;
}
