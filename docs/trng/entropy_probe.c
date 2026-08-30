// entropy_probe.c -- what sources of randomness does this machine actually have?
//
// A unikernel has no operating system, so no /dev/random and no kernel entropy
// pool. Randomness has to come from the hardware directly. Three candidates:
//
//   RDRAND  -- the CPU's random generator (a DRBG reseeded from a real source)
//   RDSEED  -- the raw conditioned entropy behind RDRAND
//   RDTSC   -- timestamp-counter jitter, an indirect physical source
//
// This reports which exist, whether they actually return varying values (a
// masked instruction returns a constant), and how much jitter the timestamp
// counter carries.

#include <stdio.h>
#include <stdint.h>

static void cpuid(uint32_t leaf, uint32_t sub, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(sub));
}
static int rdrand64(uint64_t *out) {
    unsigned char ok;
    __asm__ volatile("rdrand %0; setc %1" : "=r"(*out), "=qm"(ok));
    return ok;
}
static int rdseed64(uint64_t *out) {
    unsigned char ok;
    __asm__ volatile("rdseed %0; setc %1" : "=r"(*out), "=qm"(ok));
    return ok;
}
static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static int popcount64(uint64_t x) { int c = 0; while (x) { c += x & 1; x >>= 1; } return c; }

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    uint32_t a, b, c, d;

    cpuid(1, 0, &a, &b, &c, &d);
    int has_rdrand = (c >> 30) & 1;                 // CPUID.1:ECX[30]
    cpuid(7, 0, &a, &b, &c, &d);
    int has_rdseed = (b >> 18) & 1;                 // CPUID.7:EBX[18]
    printf("PROBE cpuid: rdrand=%d rdseed=%d\n", has_rdrand, has_rdseed);

    // --- RDRAND: does it succeed, and does it vary?
    {
        int ok = 0, ones = 0; uint64_t first = 0, orv = 0, andv = ~0ULL, prev = 0, stuck = 0;
        for (int i = 0; i < 4096; i++) {
            uint64_t v;
            int s = rdrand64(&v);
            if (s) { ok++; ones += popcount64(v); orv |= v; andv &= v; if (i && v == prev) stuck++; prev = v; if (!first) first = v; }
        }
        printf("PROBE rdrand: success=%d/4096 bit_ones=%d/%d stuck_repeats=%llu or=%016llx and=%016llx first=%016llx\n",
               ok, ones, ok*64, (unsigned long long)stuck,
               (unsigned long long)orv, (unsigned long long)andv, (unsigned long long)first);
    }

    // --- RDSEED: the raw entropy source
    {
        int ok = 0, ones = 0; uint64_t orv = 0, andv = ~0ULL, first = 0;
        for (int i = 0; i < 4096; i++) {
            uint64_t v;
            if (rdseed64(&v)) { ok++; ones += popcount64(v); orv |= v; andv &= v; if (!first) first = v; }
        }
        printf("PROBE rdseed: success=%d/4096 bit_ones=%d/%d or=%016llx and=%016llx first=%016llx\n",
               ok, ones, ok ? ok*64 : 1, (unsigned long long)orv, (unsigned long long)andv, (unsigned long long)first);
    }

    // --- RDTSC jitter: LSB balance of the delta across a fixed tiny workload.
    // If the low bit of the timing of identical work is roughly balanced, there
    // is harvestable jitter; if it is constant, the counter is too coarse or
    // the work too deterministic.
    {
        int lsb_ones = 0, samples = 8192, nonzero_low = 0;
        uint64_t histo[16] = {0};
        volatile uint64_t sink = 0;
        for (int i = 0; i < samples; i++) {
            uint64_t t0 = rdtsc();
            for (volatile int k = 0; k < 64; k++) sink += k;   // fixed work
            uint64_t dt = rdtsc() - t0;
            lsb_ones += (int)(dt & 1);
            if (dt & 0xF) nonzero_low++;
            histo[dt & 0xF]++;
        }
        printf("PROBE rdtsc_jitter: samples=%d lsb_ones=%d (ideal %d) low4_nonzero=%d\n",
               samples, lsb_ones, samples/2, nonzero_low);
        printf("PROBE rdtsc_histo:");
        for (int i = 0; i < 16; i++) printf(" %llu", (unsigned long long)histo[i]);
        printf("\n");
    }
    printf("PROBE_DONE\n");
    return 0;
}
