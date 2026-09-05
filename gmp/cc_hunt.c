/* cc_hunt -- PRP stage of the Cunningham chain hunt.
 *
 * Reads multipliers from cc_sieve --list on stdin ("M <m>" lines) and, for
 * each one, tests the chain
 *
 *     N_i(m) = m * p# * 2^(j+i) - 1        for i = 0 .. k-1
 *
 * reporting the longest run of probable primes it finds. cc_sieve removes the
 * multipliers that trial division can kill; everything reaching this program
 * has survived that, and the only way left to reject it is a PRP test.
 *
 * WHY THE TERMS ARE TESTED IN ORDER, AND WHY THAT IS THE WHOLE PERFORMANCE STORY
 *
 * A chain dies at its first composite, so the terms are tested lowest-first
 * and the loop breaks immediately. Roughly 1 - 1/ln(N) of multipliers die on
 * N_0 alone, which at ~1000 digits is about 99.96%. The expected cost per
 * multiplier is therefore barely more than one PRP test, not k of them, and
 * the projected core-hours for the hunt depend on that being true. If this
 * loop is ever changed to test terms out of order or speculatively, the cost
 * model in docs/portfolio/CUNNINGHAM-SIZE-RECORDS.md stops holding.
 *
 * ON mpz_probab_prime_p
 *
 * This is Miller-Rabin (plus Baillie-PSW in GMP >= 6), so a "prime" here is a
 * probable prime. That is the correct and standard tool for a search stage:
 * the record tables want a proof, but a proof on a composite that got this far
 * is unaffordable to attempt on every candidate. Anything this program reports
 * as a full-length chain is a *candidate* and must be re-tested and then
 * proven before it is claimed. It is not a record until that happens.
 *
 * REPS is deliberately low for the intermediate terms. A false positive costs
 * one wasted term-test, and the final confirmation re-tests survivors at much
 * higher strength, so paying for certainty inside the hot loop buys nothing.
 *
 * OUTPUT
 *
 * One line per multiplier that achieves at least --min-report terms, plus a
 * HUNT_DONE summary. The histogram of chain lengths is the useful diagnostic:
 * it should fall by roughly a factor of ln(N) per term, and if it does not,
 * either the sieve or the arithmetic is wrong.
 *
 *   build:  gcc -O2 -o cc_hunt cc_hunt.c -lgmp
 *   use:    cc_sieve --list --quiet ... | cc_hunt --k 6 --primorial 2357 --j 1
 *   check:  cc_hunt --selftest
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include <gmp.h>

#ifndef HUNT_REPS
#define HUNT_REPS 8                 /* Miller-Rabin reps per intermediate term */
#endif
#ifndef HUNT_REPS_FINAL
#define HUNT_REPS_FINAL 64          /* reps re-applied to a full-length hit */
#endif

struct params {
	unsigned long primorial;
	int           k;
	int           j;
	int           min_report;
	int           quiet;
};

static void die(const char *msg)
{
	fprintf(stderr, "cc_hunt: %s\n", msg);
	exit(1);
}

/* p# -- the product of every prime <= lim. Trial division is fine here: this
   runs once, and lim is a few thousand. */
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

/* N_i(m) = m * p# * 2^(j+i) - 1 */
static void term(mpz_t N, const mpz_t pr, uint64_t m, int j, int i)
{
	mpz_mul_ui(N, pr, (unsigned long)m);
	mpz_mul_2exp(N, N, (unsigned long)(j + i));
	mpz_sub_ui(N, N, 1);
}

/* How many consecutive terms starting at i=0 are probable primes. Stops at the
   first composite -- see the header on why that break is load-bearing. */
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

/* Known-answer check. 2 is the start of a length-5 chain
   (2, 5, 11, 23, 47) and 89 starts a length-6 chain
   (89, 179, 359, 719, 1439, 2879). Both are classical and small enough to
   verify by hand, so a wrong term(), a wrong primorial, or an off-by-one in
   the exponent shows up here rather than after a week of compute. */
static int selftest(void)
{
	int fails = 0;
	mpz_t one, N;
	mpz_inits(one, N, NULL);
	mpz_set_ui(one, 1);

	/* With p#=1 and j=0, N_i(m) = m*2^i - 1, so m = p+1 generates the
	   Cunningham chain of the first kind starting at p. */
	struct { uint64_t m; int want; const char *what; } cases[] = {
		{ 3,   5, "chain from 2:  2 5 11 23 47" },
		{ 90,  6, "chain from 89: 89 179 359 719 1439 2879" },
		{ 7,   0, "first term 7*2^0-1 = 6, composite: no chain at all" },
		{ 6,   4, "chain from 5:  5 11 23 47, then 95 = 5*19" },
	};

	printf("selftest: p#=1 j=0, N_i(m) = m*2^i - 1\n");
	for (size_t c = 0; c < sizeof cases / sizeof cases[0]; c++) {
		int got = chain_length(one, cases[c].m, 0, 8, 25);
		const char *verdict = (got == cases[c].want) ? "ok" : "MISMATCH";
		if (got != cases[c].want) fails++;
		printf("  m=%-4" PRIu64 " len=%d want=%d  %-8s %s\n",
		       cases[c].m, got, cases[c].want, verdict, cases[c].what);
	}

	/* A composite must not be reported as a chain of any length. */
	mpz_set_ui(N, 0);
	term(N, one, 10, 0, 0);            /* 10*2^0 - 1 = 9 = 3*3 */
	if (mpz_probab_prime_p(N, 25) != 0) {
		printf("  9 reported prime -- MISMATCH\n");
		fails++;
	} else {
		printf("  9 correctly rejected as composite    ok\n");
	}

	mpz_clears(one, N, NULL);
	printf("\n%s (%d failure%s)\n",
	       fails ? "SELFTEST FAILED" : "SELFTEST PASSED",
	       fails, fails == 1 ? "" : "s");
	return fails ? 1 : 0;
}

static void usage(void)
{
	fprintf(stderr,
	  "usage: cc_hunt [options]   (multipliers on stdin as \"M <m>\" lines)\n"
	  "  --primorial P    largest prime in p#           (default 2357)\n"
	  "  --k K            chain length sought           (default 6)\n"
	  "  --j J            the j in m*p#*2^j             (default 1)\n"
	  "  --min-report N   report chains at least N long (default 3)\n"
	  "  --quiet          suppress progress\n"
	  "  --selftest       known-answer check, then exit\n");
	exit(2);
}

int main(int argc, char **argv)
{
	struct params pr = { .primorial = 2357, .k = 6, .j = 1,
	                     .min_report = 3, .quiet = 0 };

	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		#define NEXT() (++i < argc ? argv[i] : (usage(), ""))
		if      (!strcmp(a, "--selftest"))   return selftest();
		else if (!strcmp(a, "--primorial"))  pr.primorial  = strtoul(NEXT(), 0, 10);
		else if (!strcmp(a, "--k"))          pr.k          = atoi(NEXT());
		else if (!strcmp(a, "--j"))          pr.j          = atoi(NEXT());
		else if (!strcmp(a, "--min-report")) pr.min_report = atoi(NEXT());
		else if (!strcmp(a, "--quiet"))      pr.quiet      = 1;
		else usage();
		#undef NEXT
	}
	if (pr.k < 1 || pr.k > 32) die("--k out of range");

	mpz_t p, N;
	mpz_inits(p, N, NULL);
	primorial(p, pr.primorial);

	term(N, p, 1, pr.j, 0);
	size_t digits = mpz_sizeinbase(N, 10);

	printf("HUNT_START primorial=%lu# k=%d j=%d digits~%zu reps=%d\n",
	       pr.primorial, pr.k, pr.j, digits, HUNT_REPS);
	fflush(stdout);

	/* hist[n] = multipliers whose chain reached exactly n terms */
	uint64_t hist[33] = {0};
	uint64_t examined = 0, best_m = 0;
	int best = 0;
	clock_t t0 = clock();

	char line[128];
	while (fgets(line, sizeof line, stdin)) {
		uint64_t m;
		if (sscanf(line, "M %" SCNu64, &m) != 1) continue;
		examined++;

		int len = chain_length(p, m, pr.j, pr.k, HUNT_REPS);
		hist[len]++;

		if (len > best) { best = len; best_m = m; }

		if (len >= pr.min_report) {
			/* Re-test at full strength before saying anything. A hit
			   printed here is a candidate, never a claim. */
			int confirmed = chain_length(p, m, pr.j, pr.k, HUNT_REPS_FINAL);
			printf("HUNT_HIT m=%" PRIu64 " len=%d confirmed=%d k=%d\n",
			       m, len, confirmed, pr.k);
			fflush(stdout);
		}

		if (!pr.quiet && examined % 100 == 0) {
			double el = (double)(clock() - t0) / CLOCKS_PER_SEC;
			fprintf(stderr, "  examined=%" PRIu64 " best=%d %.1f/s\n",
			        examined, best, examined / (el > 0 ? el : 1));
		}
	}

	double el = (double)(clock() - t0) / CLOCKS_PER_SEC;
	printf("HUNT_DONE examined=%" PRIu64 " best=%d best_m=%" PRIu64
	       " seconds=%.2f per_m_ms=%.3f\n",
	       examined, best, best_m, el,
	       examined ? 1000.0 * el / (double)examined : 0.0);
	printf("HUNT_HIST");
	for (int n = 0; n <= pr.k; n++) printf(" %d:%" PRIu64, n, hist[n]);
	printf("\n");

	mpz_clears(p, N, NULL);
	return 0;
}
