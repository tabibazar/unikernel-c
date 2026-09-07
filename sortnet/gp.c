/* gp.c -- steady-state search for a sorting network of a fixed length.
 *
 * Per docs/superpowers/specs/2026-09-05-sorting-network-gp-design.md.
 *
 * FIXED LENGTH IS THE KEY CHOICE
 *
 * A run targets exactly L comparators and either finds a correct network of
 * that length or does not. Length is what is being minimised, so making it a
 * parameter of the run rather than a property of the individual removes the
 * bloat pathology outright and avoids a fitness function that has to trade two
 * incomparable quantities. It also makes the representation closed under
 * crossover: both parents are valid length-L networks, so any splice of them is
 * a valid length-L network and no repair step is needed.
 *
 * STEADY-STATE, NOT GENERATIONAL
 *
 * One child at a time replaces the current worst individual. There is no
 * generation boundary to synchronise on, which matters because the eventual
 * target for this is a set of workers with no cheap way to synchronise.
 *
 * SELECTION
 *
 * Tournament rather than roulette. Fitness here is a large count -- up to 2^n --
 * with tiny differences near the top, and roulette on such values degenerates to
 * almost uniform selection, which is drift rather than search.
 *
 * ELITISM
 *
 * The best individual is never replaced. Without that, steady-state replacement
 * eventually overwrites the best network with a worse child.
 *
 *   build: gcc -O2 -o gp gp.c
 *   run:   ./gp --n 6 --len 12 --seed 1 --budget 1000000
 */

#define EVAL_NO_MAIN
#include "eval.c"

#include <inttypes.h>

/* ------------------------------------------------------------------- rng */
/* xoshiro256** seeded through splitmix64.
 *
 * The first version used splitmix64 alone with state = seed*GAMMA, where GAMMA
 * is also splitmix's increment. That puts every seed on the same arithmetic
 * progression, so seed 2's stream is seed 1's stream shifted by exactly one
 * draw and seed 3's by two -- verified directly. Ten "independent" seeds were
 * really one sequence sampled at ten offsets, which is precisely the failure
 * the median-of-ten gate exists to prevent. Streams must be independent or the
 * gate measures nothing.
 *
 * splitmix64 is fine as a *seeder* -- that is its documented use -- so it fills
 * xoshiro's four words, and xoshiro provides the stream. */
static uint64_t sm_state;
static uint64_t splitmix64(void)
{
	uint64_t z = (sm_state += 0x9E3779B97F4A7C15ULL);
	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
	z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
	return z ^ (z >> 31);
}

static uint64_t s[4];
static inline uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
static uint64_t rnd(void)
{
	uint64_t r = rotl(s[1] * 5, 7) * 9;
	uint64_t t = s[1] << 17;
	s[2] ^= s[0]; s[3] ^= s[1]; s[1] ^= s[2]; s[0] ^= s[3]; s[2] ^= t;
	s[3] = rotl(s[3], 45);
	return r;
}
static void rng_seed(uint64_t seed)
{
	sm_state = seed;
	for (int i = 0; i < 4; i++) s[i] = splitmix64();
}
static int rnd_int(int n) { return (int)(rnd() % (uint64_t)n); }

/* --------------------------------------------------------------- genomes */

static int g_n, g_len;

/* A legal comparator: two distinct wires, lower index first. The canonical
   form matters -- {b,a} with a<b is the same comparator relabelled, and
   allowing both doubles the search space for nothing. */
static Cmp rnd_cmp(void)
{
	int a = rnd_int(g_n), b = rnd_int(g_n);
	while (b == a) b = rnd_int(g_n);
	Cmp c;
	c.a = (uint8_t)(a < b ? a : b);
	c.b = (uint8_t)(a < b ? b : a);
	return c;
}

static void rnd_net(Cmp *net) { for (int i = 0; i < g_len; i++) net[i] = rnd_cmp(); }

/* Two-point crossover: child takes parent B's comparators between two cut
   points and parent A's elsewhere. Valid by construction. */
static void crossover(Cmp *child, const Cmp *A, const Cmp *B)
{
	int p = rnd_int(g_len), q = rnd_int(g_len);
	if (p > q) { int t = p; p = q; q = t; }
	for (int i = 0; i < g_len; i++)
		child[i] = (i >= p && i <= q) ? B[i] : A[i];
}

/* Three operators, per the spec. Position matters in a comparator network --
   the same multiset in a different order is a different network -- so the swap
   is a real move and not a no-op. */
static void mutate(Cmp *net)
{
	switch (rnd_int(3)) {
	case 0:                                     /* replace one comparator */
		net[rnd_int(g_len)] = rnd_cmp();
		break;
	case 1: {                                   /* swap two positions */
		int i = rnd_int(g_len), j = rnd_int(g_len);
		Cmp t = net[i]; net[i] = net[j]; net[j] = t;
		break;
	}
	default: {                                  /* perturb one endpoint */
		int i = rnd_int(g_len);
		Cmp c = net[i];
		int w = rnd_int(g_n);
		if (rnd_int(2)) c.a = (uint8_t)w; else c.b = (uint8_t)w;
		if (c.a == c.b) { net[i] = rnd_cmp(); break; }
		if (c.a > c.b) { uint8_t t = c.a; c.a = c.b; c.b = t; }
		net[i] = c;
		break;
	}
	}
}

int main(int argc, char **argv)
{
	int n = 6, len = 12, pop = 200, tour = 5;
	uint64_t seed = 1, budget = 1000000;
	int quiet = 0;

	for (int i = 1; i < argc; i++) {
		#define NEXT() (++i < argc ? argv[i] : "")
		if      (!strcmp(argv[i], "--n"))      n      = atoi(NEXT());
		else if (!strcmp(argv[i], "--len"))    len    = atoi(NEXT());
		else if (!strcmp(argv[i], "--pop"))    pop    = atoi(NEXT());
		else if (!strcmp(argv[i], "--tour"))   tour   = atoi(NEXT());
		else if (!strcmp(argv[i], "--seed"))   seed   = strtoull(NEXT(), 0, 10);
		else if (!strcmp(argv[i], "--budget")) budget = strtoull(NEXT(), 0, 10);
		else if (!strcmp(argv[i], "--quiet"))  quiet  = 1;
		else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
		#undef NEXT
	}
	if (len < 1 || len > MAX_CMP) { fprintf(stderr, "bad --len\n"); return 2; }

	g_n = n; g_len = len; rng_seed(seed);

	Eval e;
	if (ev_init(&e, n) != 0) { fprintf(stderr, "bad --n\n"); return 2; }
	uint64_t target = 1ULL << n;

	Cmp *ind = malloc((size_t)pop * len * sizeof(Cmp));
	uint64_t *fit = malloc((size_t)pop * sizeof(uint64_t));
	Cmp *child = malloc((size_t)len * sizeof(Cmp));
	if (!ind || !fit || !child) return 1;

	uint64_t evals = 0;
	int best = 0, worst = 0;
	for (int i = 0; i < pop; i++) {
		rnd_net(ind + (size_t)i * len);
		fit[i] = ev_fitness(&e, ind + (size_t)i * len, len);
		evals++;
		if (fit[i] > fit[best])  best  = i;
		if (fit[i] < fit[worst]) worst = i;
	}

	if (!quiet)
		printf("GP_START n=%d len=%d pop=%d tour=%d seed=%" PRIu64
		       " budget=%" PRIu64 " target=%" PRIu64 "\n",
		       n, len, pop, tour, seed, budget, target);

	/* The initial population can already contain a solution -- at n=4 with 200
	   random 5-comparator networks it usually does. Reporting evals=0 in that
	   case was wrong and made the gate look stronger than it is: the population
	   cost 200 evaluations to build. */
	uint64_t solved_at = (fit[best] >= target) ? evals : 0;
	while (evals < budget && fit[best] < target) {
		/* two tournaments -> two parents */
		int pa = rnd_int(pop), pb = rnd_int(pop);
		for (int t = 1; t < tour; t++) {
			int c = rnd_int(pop); if (fit[c] > fit[pa]) pa = c;
			c = rnd_int(pop);     if (fit[c] > fit[pb]) pb = c;
		}
		crossover(child, ind + (size_t)pa * len, ind + (size_t)pb * len);
		mutate(child);
		if (rnd_int(2)) mutate(child);

		uint64_t f = ev_fitness(&e, child, len);
		evals++;

		/* Replace the worst, but never the best -- elitism. */
		if (worst != best && f >= fit[worst]) {
			memcpy(ind + (size_t)worst * len, child, (size_t)len * sizeof(Cmp));
			fit[worst] = f;
			if (f > fit[best]) best = worst;
			worst = 0;
			for (int i = 1; i < pop; i++) if (fit[i] < fit[worst]) worst = i;
		}
		if (fit[best] >= target) { solved_at = evals; break; }
	}

	if (fit[best] >= target) {
		printf("GP_SOLVED n=%d len=%d seed=%" PRIu64 " evals=%" PRIu64 "\n",
		       n, len, seed, solved_at);
		if (!quiet) {
			printf("GP_NET");
			Cmp *b = ind + (size_t)best * len;
			for (int i = 0; i < len; i++) printf(" (%d,%d)", b[i].a, b[i].b);
			printf("\n");
		}
	} else {
		printf("GP_FAILED n=%d len=%d seed=%" PRIu64 " evals=%" PRIu64
		       " best=%" PRIu64 " of %" PRIu64 "\n",
		       n, len, seed, evals, fit[best], target);
	}

	free(ind); free(fit); free(child); ev_free(&e);
	return fit[best] >= target ? 0 : 1;
}
