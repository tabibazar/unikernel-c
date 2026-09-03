/* cc_sieve -- residue-class sieve for Cunningham chains of the first kind.
 *
 * Produces the multipliers m for which every term of
 *
 *     N_i(m) = m * p# * 2^(j+i) - 1        for i = 0 .. k-1
 *
 * survives trial division by all primes up to a chosen depth. Those
 * survivors are what the PRP stage actually tests; everything this program
 * removes is a candidate the expensive stage never has to look at.
 *
 * WHY THIS FORM
 *
 * A Cunningham chain of the first kind has p_{i+1} = 2*p_i + 1, so
 * p_i + 1 = 2^i * (p_1 + 1). Writing p_1 = m*p#*2^j - 1 makes p_1 + 1 a
 * clean power-of-two multiple of the primorial, and the whole chain becomes
 * k consecutive powers of two against one multiplier. Every record at
 * length >= 4 uses this shape.
 *
 * It also sieves itself for free. For any prime q <= p, p# = 0 (mod q), so
 * every term is congruent to -1 and q can never divide one. That is why the
 * primorial is chosen large: it is simultaneously the source of the digit
 * count and a complete sieve to depth p.
 *
 * THE SIEVE PROPER
 *
 * For a prime q > p, q divides N_i(m) exactly when
 *
 *     m * p# * 2^(j+i) = 1   (mod q)
 *     m               = (p# * 2^(j+i))^-1   (mod q)
 *
 * so each (q, i) pair kills exactly one residue class of m. With
 * A_i = p#*2^(j+i) mod q and A_{i+1} = 2*A_i, the k inverses come from one
 * extended-Euclid call followed by k-1 multiplications by inv(2), which is
 * (q+1)/2 for odd q.
 *
 * WHERE THIS RUNS
 *
 * On a host with real memory, not on the 16 MiB unikernel -- and that is a
 * design decision rather than a limitation, because the arithmetic says so.
 * Survival to depth P is (ln p / ln P)^k, so depth pays for itself
 * geometrically in PRP tests avoided:
 *
 *     depth P     survival      PRP tests vs depth 1e13
 *     1e9         2.8e-3        9.4x more
 *     1e11        8.4e-4        2.8x more
 *     1e12        4.9e-4        1.6x more
 *     1e13        3.0e-4        1x
 *
 * Deep sieving means enumerating billions of primes, and the per-prime setup
 * cost is only worth paying if it is amortised over a large span of m -- i.e.
 * over a large bitmap. So: sieve centrally where RAM is cheap, ship the
 * survivor list to the swarm, and let the unikernels do the PRP work, which
 * is 95%+ of the total and needs almost no memory. See ../gmp/README.md.
 *
 * MEASURED, 2026-09-03 (Apple arm64, -O2, 2357# = 999 digits, k=6)
 *
 * Survival rate against the Mertens prediction (ln p / ln P)^k -- an
 * independent check on the density model the whole cost estimate rests on:
 *
 *     depth      predicted   measured    ratio
 *     1e6        3.153e-2    3.195e-2    1.013
 *     1e7        1.250e-2    1.251e-2    1.001
 *     1e8        5.611e-3    5.607e-3    0.999
 *
 * Per-prime setup cost is 8.0e-7 s, about 2,400 cycles, dominated by the 53
 * limb reductions in primorial_mod(). That cost is paid once per prime PER
 * SEGMENT, which sets the whole shape of the job: bigger segments are
 * strictly better, and the sieve wants a big-memory host.
 *
 * THE COST BALANCE, AND A CORRECTION
 *
 * Sieving deeper trades its own cost against PRP tests avoided. With a 1 GB
 * bitmap (8.6e9 multipliers per segment) and the 2.4e13 multipliers expected
 * to yield one k=6 chain, at 8.6 ms per PRP test:
 *
 *     depth    PRP tests   PRP h    sieve h   TOTAL h
 *     1e9       6.6e10     158,692       30   158,722
 *     1e10      3.5e10      84,336      269    84,605
 *     1e11      2.0e10      47,605    2,449    50,054   <- optimum
 *     1e12      1.2e10      28,244   22,450    50,693
 *     1e13      7.3e09      17,472  207,227   224,699
 *
 * This corrects a figure carried in
 * ../docs/portfolio/CUNNINGHAM-SIZE-RECORDS.md. That document quotes 7.3e9
 * PRP tests and ~15,000-18,000 core-hours, which is the depth-1e13 row --
 * true only if sieving to 1e13 were free. It is not. The honest total for
 * this implementation is nearer 50,000 core-hours.
 *
 * Two changes bring it back down, neither of them exotic: a Barrett
 * reciprocal per q replaces the division in primorial_mod() with a multiply
 * and a shift (~5x), and an 8 GB bitmap cuts the number of segments eightfold.
 * Together those put the optimum near 23,000 core-hours at depth 1e13. So the
 * range to plan against is 23,000 optimised to 50,000 as written -- still
 * inside a $500 budget at $0.005/core-hour, but not the 18,000 quoted.
 *
 * Build:  cc -O2 -o cc_sieve cc_sieve.c
 * Check:  ./cc_sieve --selftest
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>

/* ------------------------------------------------------------------ types */

typedef uint64_t u64;
typedef unsigned __int128 u128;

#define MAX_PRIMORIAL_LIMBS 512      /* 512 * 64 bits ~ 9,800 digits */

struct params {
	u64 primorial;      /* p in p#  */
	int k;              /* chain length */
	int j;              /* the j in m*p#*2^j */
	u64 m_start;
	u64 m_count;
	u64 depth;          /* sieve all primes up to here */
	u64 seg_bits;       /* multipliers per bitmap segment */
	int quiet;
	int list;           /* print survivors, not just the count */
};

/* -------------------------------------------------------------- utilities */

static void die(const char *msg)
{
	fprintf(stderr, "cc_sieve: %s\n", msg);
	exit(1);
}

static u64 mulmod(u64 a, u64 b, u64 m)
{
	return (u64)(((u128)a * b) % m);
}

/* Extended Euclid. Returns a^-1 mod m, or 0 when a is not invertible --
   which cannot happen for the callers here, since every modulus is a prime
   strictly greater than p and every value being inverted is a product of
   primes <= p and powers of two. */
static u64 inv_mod(u64 a, u64 m)
{
	int64_t t = 0, newt = 1;
	u64 r = m, newr = a % m;

	while (newr != 0) {
		u64 quot = r / newr;
		int64_t tmp_t = t - (int64_t)quot * newt;
		u64 tmp_r = r - quot * newr;
		t = newt; newt = tmp_t;
		r = newr; newr = tmp_r;
	}
	if (r > 1) return 0;
	return (u64)(t < 0 ? t + (int64_t)m : t);
}

/* ------------------------------------------------- small primes, primorial */

/* Plain sieve of Eratosthenes up to limit. Caller frees. */
static u64 *small_primes(u64 limit, size_t *out_n)
{
	char *composite = calloc(limit + 1, 1);
	if (!composite) die("out of memory building small primes");

	size_t n = 0;
	for (u64 i = 2; i <= limit; i++) {
		if (composite[i]) continue;
		n++;
		for (u64 mlt = i * i; mlt <= limit && mlt >= i; mlt += i)
			composite[mlt] = 1;
	}

	u64 *primes = malloc(n * sizeof *primes);
	if (!primes) die("out of memory building small primes");
	size_t w = 0;
	for (u64 i = 2; i <= limit; i++)
		if (!composite[i]) primes[w++] = i;

	free(composite);
	*out_n = n;
	return primes;
}

/* p# as little-endian 64-bit limbs. Built by repeated multiply-by-small,
   which is exact and needs no bignum library -- this program deliberately
   does not link GMP, so it can be built and run anywhere without the port. */
static int build_primorial(u64 p, u64 *limbs, const u64 *primes, size_t nprimes)
{
	memset(limbs, 0, MAX_PRIMORIAL_LIMBS * sizeof *limbs);
	limbs[0] = 1;
	int used = 1;

	for (size_t i = 0; i < nprimes && primes[i] <= p; i++) {
		u64 carry = 0;
		for (int l = 0; l < used; l++) {
			u128 prod = (u128)limbs[l] * primes[i] + carry;
			limbs[l] = (u64)prod;
			carry = (u64)(prod >> 64);
		}
		if (carry) {
			if (used >= MAX_PRIMORIAL_LIMBS)
				die("primorial too large -- raise MAX_PRIMORIAL_LIMBS");
			limbs[used++] = carry;
		}
	}
	return used;
}

/* p# mod q, by Horner over the limbs from the top down.
 *
 * This is the hot path: it runs once per sieving prime, so at depth 1e11 it
 * runs four billion times. Each step is a 128/64 division, which the
 * hardware does in roughly 25 cycles, giving ~1,300 cycles per prime for a
 * 1,000-digit primorial. That is affordable to 1e9 and uncomfortable beyond
 * it; the standard fix is a precomputed Barrett reciprocal per q, turning
 * each step into a multiply and a shift. Left undone deliberately -- it is
 * an optimisation of a measured cost, and the cost has not been measured on
 * the machine that will run it. */
static u64 primorial_mod(const u64 *limbs, int used, u64 q)
{
	u128 r = 0;
	for (int l = used - 1; l >= 0; l--)
		r = ((r << 64) | limbs[l]) % q;
	return (u64)r;
}

/* ------------------------------------------------------------ the sieve */

struct sieve_stats {
	u64 examined;
	u64 survivors;
	u64 primes_used;
};

/* Mark every m in [seg_lo, seg_lo+seg_n) that any prime in (p, depth]
   divides one of the k terms of. bitmap bit set == still alive. */
static void sieve_segment(const struct params *pr, const u64 *pl, int pl_used,
                          u64 seg_lo, u64 seg_n, unsigned char *alive,
                          const u64 *base_primes, size_t n_base,
                          struct sieve_stats *st)
{
	memset(alive, 0xFF, (seg_n + 7) / 8);

	/* Segmented Eratosthenes over q, so that depth can exceed anything we
	   could hold a list of. The block is sized to stay in cache. */
	const u64 QBLOCK = 1u << 20;
	unsigned char *qcomp = malloc(QBLOCK);
	if (!qcomp) die("out of memory for the prime block");

	for (u64 qlo = 2; qlo <= pr->depth; qlo += QBLOCK) {
		u64 qhi = qlo + QBLOCK - 1;
		if (qhi > pr->depth) qhi = pr->depth;
		u64 span = qhi - qlo + 1;

		memset(qcomp, 0, span);
		for (size_t i = 0; i < n_base; i++) {
			u64 bp = base_primes[i];
			if (bp * bp > qhi) break;
			u64 first = (qlo + bp - 1) / bp * bp;
			if (first < bp * bp) first = bp * bp;
			for (u64 c = first; c <= qhi; c += bp)
				qcomp[c - qlo] = 1;
		}

		for (u64 off = 0; off < span; off++) {
			if (qcomp[off]) continue;
			u64 q = qlo + off;
			if (q <= pr->primorial) continue;   /* p# already covers these */

			st->primes_used++;

			/* A_0 = p# * 2^j mod q, then r_i = A_i^-1 with
			   r_{i+1} = r_i * inv(2). */
			u64 a = primorial_mod(pl, pl_used, q);
			if (a == 0) continue;               /* q | p#, impossible here */
			for (int s = 0; s < pr->j; s++)
				a = (a * 2) % q;

			u64 r = inv_mod(a, q);
			if (r == 0) continue;
			u64 inv2 = (q + 1) / 2;

			for (int i = 0; i < pr->k; i++) {
				/* first index in this segment with m = r (mod q) */
				u64 start = r >= seg_lo % q
				          ? r - seg_lo % q
				          : r + q - seg_lo % q;
				for (u64 idx = start; idx < seg_n; idx += q)
					alive[idx >> 3] &= (unsigned char)~(1u << (idx & 7));
				r = mulmod(r, inv2, q);
			}
		}
	}
	free(qcomp);
}

/* ---------------------------------------------------------------- selftest */

/* Brute-force check on parameters small enough that every term fits in 64
 * bits: classify each m by direct trial division and compare against the
 * sieve, in both directions.
 *
 * Both directions matter. A sieve that marks nothing passes any
 * "survivors are really coprime" test trivially, and a sieve that marks
 * everything passes any "no valid candidate was missed" test just as
 * trivially. This repo has already published one measurement that read
 * zero because nothing was measured rather than because nothing happened
 * (see docs/aco-r1/README.md); the check for that is to assert both a
 * floor and a ceiling. */
static int selftest(void)
{
	struct params pr = {
		.primorial = 13,      /* 13# = 30030 */
		.k = 3,
		.j = 1,
		.m_start = 1,
		.m_count = 20000,
		.depth = 5000,
		.seg_bits = 20000,
		.quiet = 1,
		.list = 0,
	};

	size_t n_small;
	u64 *primes = small_primes(pr.depth, &n_small);

	u64 pl[MAX_PRIMORIAL_LIMBS];
	int pl_used = build_primorial(pr.primorial, pl, primes, n_small);

	printf("selftest: p#=%" PRIu64 "# k=%d j=%d m in [%" PRIu64 ",%" PRIu64 ") depth=%" PRIu64 "\n",
	       pr.primorial, pr.k, pr.j, pr.m_start, pr.m_start + pr.m_count, pr.depth);
	printf("          primorial = %" PRIu64 " (%d limb%s)\n",
	       pl[0], pl_used, pl_used == 1 ? "" : "s");

	unsigned char *alive = malloc((pr.m_count + 7) / 8);
	if (!alive) die("out of memory");
	struct sieve_stats st = {0};
	sieve_segment(&pr, pl, pl_used, pr.m_start, pr.m_count, alive,
	              primes, n_small, &st);

	u64 sieve_live = 0, brute_live = 0, false_kill = 0, false_keep = 0;

	for (u64 idx = 0; idx < pr.m_count; idx++) {
		u64 m = pr.m_start + idx;
		int by_sieve = (alive[idx >> 3] >> (idx & 7)) & 1;

		/* direct: does any prime in (p, depth] divide any term? */
		int by_brute = 1;
		for (int i = 0; i < pr.k && by_brute; i++) {
			u64 term = m * pl[0];
			for (int s = 0; s < pr.j + i; s++) term *= 2;
			term -= 1;
			for (size_t t = 0; t < n_small; t++) {
				u64 q = primes[t];
				if (q <= pr.primorial) continue;
				if (q > pr.depth) break;
				if (q >= term) break;
				if (term % q == 0) { by_brute = 0; break; }
			}
		}

		sieve_live += by_sieve;
		brute_live += by_brute;
		if (by_sieve && !by_brute) false_keep++;
		if (!by_sieve && by_brute) false_kill++;
	}

	printf("          sieve kept %" PRIu64 ", brute force kept %" PRIu64 "\n",
	       sieve_live, brute_live);
	printf("          survivors the sieve wrongly killed: %" PRIu64 "\n", false_kill);
	printf("          composites the sieve wrongly kept:  %" PRIu64 "\n", false_keep);

	int ok = 1;
	if (false_kill || false_keep) ok = 0;
	/* Guard against the degenerate passes: a sieve that keeps everything or
	   keeps nothing agrees with nothing useful. */
	if (sieve_live == 0 || sieve_live == pr.m_count) {
		printf("          DEGENERATE: sieve kept %s\n",
		       sieve_live ? "everything" : "nothing");
		ok = 0;
	}

	free(alive);
	free(primes);

	printf("\n%s\n", ok ? "SELFTEST PASSED" : "SELFTEST FAILED");
	return ok ? 0 : 1;
}

/* -------------------------------------------------------------------- main */

static void usage(void)
{
	fprintf(stderr,
	  "usage: cc_sieve [options]\n"
	  "  --primorial P   largest prime in p#            (default 2357)\n"
	  "  --k K           chain length                   (default 6)\n"
	  "  --j J           the j in m*p#*2^j              (default 1)\n"
	  "  --m-start M     first multiplier               (default 1)\n"
	  "  --m-count N     multipliers to examine         (default 10000000)\n"
	  "  --depth D       sieve primes up to D           (default 1000000000)\n"
	  "  --segment S     multipliers per bitmap segment (default 1<<26)\n"
	  "  --list          print surviving multipliers\n"
	  "  --quiet         suppress progress\n"
	  "  --selftest      brute-force check on small parameters, then exit\n");
	exit(2);
}

int main(int argc, char **argv)
{
	struct params pr = {
		.primorial = 2357,
		.k = 6,
		.j = 1,
		.m_start = 1,
		.m_count = 10000000ULL,
		.depth = 1000000000ULL,
		.seg_bits = 1ULL << 26,
		.quiet = 0,
		.list = 0,
	};

	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		#define NEXT() (++i < argc ? argv[i] : (usage(), ""))
		if      (!strcmp(a, "--selftest"))  return selftest();
		else if (!strcmp(a, "--primorial")) pr.primorial = strtoull(NEXT(), 0, 10);
		else if (!strcmp(a, "--k"))         pr.k         = atoi(NEXT());
		else if (!strcmp(a, "--j"))         pr.j         = atoi(NEXT());
		else if (!strcmp(a, "--m-start"))   pr.m_start   = strtoull(NEXT(), 0, 10);
		else if (!strcmp(a, "--m-count"))   pr.m_count   = strtoull(NEXT(), 0, 10);
		else if (!strcmp(a, "--depth"))     pr.depth     = strtoull(NEXT(), 0, 10);
		else if (!strcmp(a, "--segment"))   pr.seg_bits  = strtoull(NEXT(), 0, 10);
		else if (!strcmp(a, "--list"))      pr.list  = 1;
		else if (!strcmp(a, "--quiet"))     pr.quiet = 1;
		else usage();
		#undef NEXT
	}

	if (pr.k < 1 || pr.k > 32) die("--k out of range");
	if (pr.j < 0 || pr.j > 4096) die("--j out of range");
	if (pr.depth < pr.primorial) die("--depth below the primorial: nothing to sieve");

	/* Base primes for the segmented prime generator need only reach
	   sqrt(depth); the primorial's own factors come from the same list, so
	   take whichever bound is larger. */
	u64 root = 1;
	while ((root + 1) * (root + 1) <= pr.depth) root++;
	u64 base_limit = root > pr.primorial ? root : pr.primorial;

	size_t n_base;
	u64 *base = small_primes(base_limit, &n_base);

	u64 pl[MAX_PRIMORIAL_LIMBS];
	int pl_used = build_primorial(pr.primorial, pl, base, n_base);

	/* Digits of p#, for the log -- the whole point of the primorial choice
	   is the size of the numbers it produces, so state it up front rather
	   than making the operator work it out. */
	double bits = 0;
	for (int l = pl_used - 1; l >= 0; l--) { bits = bits * 64.0 + 0; }
	bits = (pl_used - 1) * 64.0;
	{ u64 top = pl[pl_used - 1]; while (top) { bits += 1; top >>= 1; } }

	if (!pr.quiet) {
		printf("SIEVE_START primorial=%" PRIu64 "# k=%d j=%d digits~%.0f "
		       "m=[%" PRIu64 ",%" PRIu64 ") depth=%" PRIu64 "\n",
		       pr.primorial, pr.k, pr.j, bits / 3.3219,
		       pr.m_start, pr.m_start + pr.m_count, pr.depth);
		fflush(stdout);
	}

	u64 seg_n = pr.seg_bits;
	unsigned char *alive = malloc((seg_n + 7) / 8);
	if (!alive) die("out of memory for the segment bitmap");

	struct sieve_stats total = {0};

	for (u64 done = 0; done < pr.m_count; done += seg_n) {
		u64 this_n = pr.m_count - done < seg_n ? pr.m_count - done : seg_n;
		u64 lo = pr.m_start + done;

		struct sieve_stats st = {0};
		sieve_segment(&pr, pl, pl_used, lo, this_n, alive, base, n_base, &st);

		u64 live = 0;
		for (u64 idx = 0; idx < this_n; idx++) {
			if (!((alive[idx >> 3] >> (idx & 7)) & 1)) continue;
			live++;
			if (pr.list) printf("M %" PRIu64 "\n", lo + idx);
		}

		total.examined  += this_n;
		total.survivors += live;
		total.primes_used = st.primes_used;

		if (!pr.quiet) {
			printf("SIEVE_SEG lo=%" PRIu64 " n=%" PRIu64 " survivors=%" PRIu64
			       " rate=%.3e\n", lo, this_n, live, (double)live / (double)this_n);
			fflush(stdout);
		}
	}

	if (!pr.quiet) {
		printf("SIEVE_DONE examined=%" PRIu64 " survivors=%" PRIu64
		       " rate=%.3e primes=%" PRIu64 "\n",
		       total.examined, total.survivors,
		       (double)total.survivors / (double)total.examined,
		       total.primes_used);
		/* The rate is the number that matters downstream: it multiplies
		   straight into the PRP bill, which is 95% of the cost. */
		printf("SIEVE_PROJECT prp_tests_per_1e12_m=%.3e\n",
		       1e12 * (double)total.survivors / (double)total.examined);
	}

	free(alive);
	free(base);
	return 0;
}
