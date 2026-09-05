/* cc_worker -- the PRP stage as a BareMetal unikernel.
 *
 * Same arithmetic as cc_hunt, packaged for a platform with no argv, no stdin
 * and 16 MiB of RAM. The multipliers it tests are baked into the image at
 * build time as a generated header (see scripts/mkslice.sh), because there is
 * no other way to parameterise an app here -- BareMetal hands main() nothing.
 *
 * WHY THE SIEVE IS NOT IN HERE
 *
 * Sieving to depth 1e9 means enumerating ~5.8e7 primes and holding a segment
 * bitmap. That does not fit in 16 MiB, and it should not: the sieve cost is
 * fixed rather than per-multiplier (measured: 200k multipliers 38.0 s, 2M
 * multipliers 39.2 s), so running it once on a host and shipping the survivors
 * is strictly better than running it again inside every worker. A survivor
 * list is small -- a few thousand 64-bit integers -- so it costs almost
 * nothing to carry in the image.
 *
 * This is the split cc_sieve.c's header describes: sieve on a host with real
 * memory, "let the unikernels do the PRP work".
 *
 * WHAT IT REPORTS, AND WHY IT REPORTS SO MUCH
 *
 * There is no filesystem to collect afterwards and no way to attach to a
 * running instance -- the serial console retrieved with `bm-api.sh instances
 * logs` is the entire record. So progress is printed as it goes, not
 * accumulated and printed at the end: an instance stopped or preempted
 * halfway must still leave behind everything it learned.
 *
 * Every line carries the slice id, so logs from four instances can be pooled
 * without ambiguity about which range a result came from.
 *
 * A CHAIN REPORTED HERE IS A CANDIDATE, NOT A RECORD
 *
 * mpz_probab_prime_p is Miller-Rabin plus Baillie-PSW: a probable prime. A
 * full-length hit must be re-tested and then proven before it is claimed
 * anywhere. The worker's job is to not lose it.
 *
 *   build:  see scripts/build-worker.sh
 */

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <gmp.h>

/* Generated: defines SLICE_ID, SLICE_M_LO, SLICE_M_HI, SLICE_N,
   SLICE_PRIMORIAL, SLICE_K, SLICE_J and the array slice_m[]. */
#include "cc_slice.h"

#ifndef WORKER_REPS
#define WORKER_REPS 8
#endif
#ifndef WORKER_REPS_FINAL
#define WORKER_REPS_FINAL 64
#endif
#ifndef WORKER_PROGRESS_EVERY
#define WORKER_PROGRESS_EVERY 100
#endif

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

static void term(mpz_t N, const mpz_t pr, uint64_t m, int j, int i)
{
	mpz_mul_ui(N, pr, (unsigned long)m);
	mpz_mul_2exp(N, N, (unsigned long)(j + i));
	mpz_sub_ui(N, N, 1);
}

/* Lowest term first, stop at the first composite. The cost model depends on
   this break: ~99.96% of multipliers die on term 0 at this size, so the
   expected cost is barely more than one PRP test rather than k of them. */
static int chain_length(const mpz_t pr, uint64_t m, int j, int k, int reps)
{
	mpz_t N;
	mpz_init(N);
	int len = 0;
	for (int i = 0; i < k; i++) {
		term(N, pr, m, j, i);
		if (mpz_probab_prime_p(N, reps) == 0) break;
		len++;
	}
	mpz_clear(N);
	return len;
}

int main(void)
{
	mpz_t p, N;
	mpz_inits(p, N, NULL);
	primorial(p, SLICE_PRIMORIAL);

	term(N, p, slice_m[0], SLICE_J, 0);
	size_t digits = mpz_sizeinbase(N, 10);

	printf("WORKER_START slice=%s m=[%" PRIu64 ",%" PRIu64 ") n=%d "
	       "primorial=%d# k=%d j=%d digits=%zu\n",
	       SLICE_ID, (uint64_t)SLICE_M_LO, (uint64_t)SLICE_M_HI,
	       SLICE_N, SLICE_PRIMORIAL, SLICE_K, SLICE_J, digits);
	fflush(stdout);

	/* A self-check before the real work. If GMP is miscompiled for this
	   target the whole run is worthless, and a wrong answer here is much
	   cheaper to notice now than after a week of instance time. 89 starts
	   the classical chain 89 179 359 719 1439 2879, so with p#=1 and j=0
	   the multiplier 90 must give exactly 6. */
	mpz_t one;
	mpz_init_set_ui(one, 1);
	int check = chain_length(one, 90, 0, 8, 25);
	mpz_clear(one);
	printf("WORKER_SELFTEST chain_from_89=%d expect=6 %s\n",
	       check, check == 6 ? "ok" : "FAILED");
	fflush(stdout);
	if (check != 6) {
		printf("WORKER_ABORT selftest failed -- refusing to report results\n");
		return 1;
	}

	uint64_t hist[SLICE_K + 1];
	for (int i = 0; i <= SLICE_K; i++) hist[i] = 0;

	int best = 0;
	uint64_t best_m = 0;

	for (int idx = 0; idx < SLICE_N; idx++) {
		uint64_t m = slice_m[idx];
		int len = chain_length(p, m, SLICE_J, SLICE_K, WORKER_REPS);
		hist[len]++;

		if (len > best) { best = len; best_m = m; }

		if (len >= 3) {
			int confirmed = chain_length(p, m, SLICE_J, SLICE_K,
			                             WORKER_REPS_FINAL);
			printf("WORKER_HIT slice=%s m=%" PRIu64 " len=%d confirmed=%d\n",
			       SLICE_ID, m, len, confirmed);
			fflush(stdout);
		}

		/* Printed as it goes: the console is the only record, and an
		   instance that is stopped must not take its progress with it. */
		if ((idx + 1) % WORKER_PROGRESS_EVERY == 0) {
			printf("WORKER_PROGRESS slice=%s done=%d of=%d best=%d\n",
			       SLICE_ID, idx + 1, SLICE_N, best);
			fflush(stdout);
		}
	}

	printf("WORKER_DONE slice=%s examined=%d best=%d best_m=%" PRIu64 "\n",
	       SLICE_ID, SLICE_N, best, best_m);
	printf("WORKER_HIST slice=%s", SLICE_ID);
	for (int i = 0; i <= SLICE_K; i++) printf(" %d:%" PRIu64, i, hist[i]);
	printf("\n");
	fflush(stdout);

	mpz_clears(p, N, NULL);
	return 0;
}
