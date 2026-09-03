/* prp_bench -- cost of one Fermat PRP test at Cunningham-record sizes.
 *
 * This is the measurement that decides whether the swarm can host a
 * chain hunt: docs/portfolio/CUNNINGHAM-SIZE-RECORDS.md sets the bar at
 * under 49 ms per test to fit a $500 cap at 1x expectation, under 16 ms
 * for 3x. Native GMP with assembly is 8.6 ms on arm64; what this
 * reports is the same operation on this platform.
 *
 * Timed with RDTSC, not clock(). clock() never advances on BareMetal
 * (docs/aco-r1/BAREMETAL.md), which is why the ACO driver had to be
 * given a compile-time iteration count instead of a seconds budget. It
 * is also the better metric here: cycles per test are independent of
 * clock frequency, so a BareMetal number and a Linux number taken on
 * the same machine compare directly without trusting either side's
 * idea of what time it is.
 *
 * Compile-time configured (ACO's lesson: BareMetal passes no argv), but
 * argv is still read when it is available, so the identical source runs
 * both places:
 *   BareMetal:  gcc -DPRP_PRIMORIAL=2357 -DPRP_REPS=20 ...
 *   Linux:      ./prp_bench [primorial] [reps]
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <gmp.h>

#ifndef PRP_PRIMORIAL
#define PRP_PRIMORIAL 2357          /* p# such that m*p#*2^j is ~1,020 digits */
#endif
#ifndef PRP_REPS
#define PRP_REPS 20
#endif
#ifndef PRP_MULTIPLIER
#define PRP_MULTIPLIER 31415926535897ULL
#endif
#ifndef PRP_SHIFT
#define PRP_SHIFT 1                 /* the j in m*p#*2^j */
#endif

#if defined(__x86_64__)
#define PRP_TICKS "cycles"
static inline uint64_t rdtsc(void)
{
	uint32_t lo, hi;
	/* lfence both sides: rdtsc is not ordered against surrounding
	   loads/stores, and without a fence the measured span can slide
	   past the region of interest. */
	__asm__ __volatile__("lfence\n\trdtsc\n\tlfence"
	                     : "=a"(lo), "=d"(hi) :: "memory");
	return ((uint64_t)hi << 32) | lo;
}
#else
/* Not the target architecture -- present only so the identical source
   builds and self-tests on an arm64 development machine. Reports
   nanoseconds; the ticks-per-second constant below keeps the derived
   columns honest rather than silently reporting nanoseconds as cycles. */
#include <time.h>
#define PRP_TICKS "nanoseconds"
static inline uint64_t rdtsc(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}
#endif

/* Ticks per second assumed when converting to wall-clock. On x86-64 this
   is a placeholder: the platform cannot report its own TSC frequency, so
   the ms and core-hour columns are indicative and the operator should
   recompute them from the host's known base clock. On the arm64
   fallback the unit really is nanoseconds, so the conversion is exact. */
#if defined(__x86_64__)
#define PRP_TICKS_PER_SEC 3.0e9
#else
#define PRP_TICKS_PER_SEC 1.0e9
#endif

/* p# for all primes <= lim, by trial division. lim is ~2,400, so the
   naive test costs nothing next to one PRP and saves carrying a table. */
static void primorial(mpz_t r, unsigned long lim)
{
	mpz_set_ui(r, 1);
	for (unsigned long n = 2; n <= lim; n++) {
		int prime = 1;
		for (unsigned long d = 2; d * d <= n; d++)
			if (n % d == 0) { prime = 0; break; }
		if (prime) mpz_mul_ui(r, r, n);
	}
}

int main(int argc, char **argv)
{
	unsigned long lim  = PRP_PRIMORIAL;
	int           reps = PRP_REPS;

	/* BareMetal passes no argv at all -- argc is 0 there, so this is
	   skipped rather than crashing on argv[0]. */
	if (argc > 1) lim  = strtoul(argv[1], NULL, 10);
	if (argc > 2) reps = atoi(argv[2]);

	mpz_t pr, N, e, base, out;
	mpz_inits(pr, N, e, base, out, NULL);

	primorial(pr, lim);
	mpz_mul_ui(N, pr, PRP_MULTIPLIER);
	mpz_mul_2exp(N, N, PRP_SHIFT);
	mpz_sub_ui(N, N, 1);              /* first kind: m*p#*2^j - 1 */

	size_t digits = mpz_sizeinbase(N, 10);
	size_t bits   = mpz_sizeinbase(N, 2);

	mpz_sub_ui(e, N, 1);
	mpz_set_ui(base, 3);

	printf("PRP_START primorial=%lu digits=%zu bits=%zu reps=%d\n",
	       lim, digits, bits, reps);

	/* One warm-up outside the timed span: the first call faults in the
	   heap GMP's temporaries live on, which would otherwise be charged
	   to test number one. */
	mpz_powm(out, base, e, N);
	int residue_is_one = (mpz_cmp_ui(out, 1) == 0);

	uint64_t t0 = rdtsc();
	for (int i = 0; i < reps; i++)
		mpz_powm(out, base, e, N);
	uint64_t cycles = (rdtsc() - t0) / (uint64_t)reps;

	/* Cycles is the primary result. Milliseconds are printed only as an
	   orientation figure at an assumed 3 GHz, because this platform
	   cannot tell us its own clock rate -- treat the ms column as
	   indicative and derive the real one from the host's known TSC
	   frequency. */
	printf("PRP_DONE unit=%s ticks_per_test=%llu ms_per_test=%.3f prp_is_one=%d\n",
	       PRP_TICKS, (unsigned long long)cycles,
	       1.0e3 * (double)cycles / PRP_TICKS_PER_SEC, residue_is_one);

	/* The decision the number feeds, restated so a console capture is
	   self-contained: 7.3e9 tests is one expected k=6 chain. */
	printf("PRP_PROJECT tests=7.3e9 core_hours=%.0f\n",
	       7.3e9 * ((double)cycles / PRP_TICKS_PER_SEC) / 3600.0);

	mpz_clears(pr, N, e, base, out, NULL);
	return 0;
}
