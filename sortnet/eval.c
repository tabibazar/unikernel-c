/* eval.c -- exhaustive correctness test for comparator networks, by bit trick.
 *
 * A sorting network sorts every input iff it sorts every 0-1 input (the
 * zero-one principle), so correctness is exactly decidable rather than sampled.
 * There are 2^n such inputs, and instead of running them one at a time we hold
 * each *wire* as a 2^n-bit vector and push all of them through at once:
 *
 *     lo = a & b;    the minimum, for every input simultaneously
 *     hi = a | b;    the maximum, for every input simultaneously
 *
 * AND is min and OR is max on single bits, so one comparator costs two bitwise
 * operations across a flat array and the whole exhaustive test costs a couple
 * per comparator. At n=18 that is ~630,000 word-operations over 0.56 MB.
 *
 * WIRE INITIALISATION
 *
 * Input k (0 <= k < 2^n) is the binary expansion of k, so the value on wire i
 * for input k is bit i of k. Wire i's vector therefore has bit k set iff
 * (k >> i) & 1 -- an alternating block pattern of 2^i zeros then 2^i ones.
 *
 * CORRECTNESS AND FITNESS
 *
 * With the minimum going to the lower-numbered wire, an output is sorted iff no
 * wire holds a 1 immediately above a wire holding a 0:
 *
 *     bad |= w[i] & ~w[i+1];
 *
 * The network is correct iff bad is zero everywhere. The *count* of zero bits in
 * bad is the number of inputs sorted correctly, and that is the fitness the
 * search climbs -- a pass/fail signal would give it nothing to work with, and
 * this costs nothing extra because bad is already being built.
 *
 * A note on n < 6: 2^n is then smaller than one 64-bit word, so only the low
 * 2^n bits of the single word are meaningful. Everything is masked with
 * VALID_MASK rather than special-cased, because an unmasked high half reads as
 * 2^64 - 2^n spuriously-sorted inputs and would make every tiny network look
 * perfect.
 *
 *   build: gcc -O2 -o eval eval.c
 *   test:  ./eval --selftest
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MAX_N    20
#define MAX_CMP  256

typedef struct { uint8_t a, b; } Cmp;

typedef struct {
	int      n;          /* wires */
	int      words;      /* 64-bit words per wire vector */
	uint64_t tail;       /* mask for meaningful bits in the last word */
	uint64_t *w;         /* n * words */
	uint64_t *init;      /* pristine copy, reloaded per evaluation */
} Eval;

static int ev_init(Eval *e, int n)
{
	if (n < 2 || n > MAX_N) return -1;
	e->n = n;
	uint64_t inputs = 1ULL << n;
	e->words = (int)((inputs + 63) / 64);
	/* For n < 6 the single word is only partly meaningful. For n >= 6 the
	   vector is a whole number of words and every bit counts. */
	e->tail = (inputs >= 64) ? ~0ULL : ((1ULL << inputs) - 1);

	e->w    = malloc((size_t)n * e->words * sizeof(uint64_t));
	e->init = malloc((size_t)n * e->words * sizeof(uint64_t));
	if (!e->w || !e->init) return -1;

	for (int i = 0; i < n; i++)
		for (int k = 0; k < e->words; k++) {
			uint64_t v = 0;
			for (int b = 0; b < 64; b++) {
				uint64_t idx = (uint64_t)k * 64 + b;
				if (idx >= inputs) break;
				if ((idx >> i) & 1ULL) v |= 1ULL << b;
			}
			e->init[(size_t)i * e->words + k] = v;
		}
	return 0;
}

static void ev_free(Eval *e) { free(e->w); free(e->init); }

/* Number of the 2^n binary inputs this network sorts correctly.
   Equals 2^n exactly when the network is a sorting network. */
static uint64_t ev_fitness(Eval *e, const Cmp *net, int len)
{
	const int W = e->words, N = e->n;
	memcpy(e->w, e->init, (size_t)N * W * sizeof(uint64_t));

	for (int c = 0; c < len; c++) {
		uint64_t *A = e->w + (size_t)net[c].a * W;
		uint64_t *B = e->w + (size_t)net[c].b * W;
		for (int k = 0; k < W; k++) {
			uint64_t lo = A[k] & B[k];
			uint64_t hi = A[k] | B[k];
			A[k] = lo; B[k] = hi;
		}
	}

	uint64_t sorted = 0;
	for (int k = 0; k < W; k++) {
		uint64_t bad = 0;
		for (int i = 0; i + 1 < N; i++)
			bad |= e->w[(size_t)i * W + k] & ~e->w[(size_t)(i + 1) * W + k];
		uint64_t mask = (k == W - 1) ? e->tail : ~0ULL;
		sorted += (uint64_t)__builtin_popcountll(~bad & mask);
	}
	return sorted;
}

/* ------------------------------------------------------------------ tests */

/* Only built into the standalone evaluator; gp.c needs the engine, not the
   fixtures, and compiling them there is a dead-code warning. */
#ifndef EVAL_NO_MAIN

/* The optimal 4-input network: 5 comparators, 3 layers. Small enough to check
   by hand, and the answer (5) is a proved optimum. */
static const Cmp NET4[] = {
	{0,1},{2,3},
	{0,2},{1,3},
	{1,2}
};

/* A standard optimal 8-input network: 19 comparators. 19 is the proved optimum
   for n=8, so if this scores a perfect 256 both the network and the evaluator
   agree with the published result. */
static const Cmp NET8[] = {
	{0,1},{2,3},{4,5},{6,7},
	{0,2},{1,3},{4,6},{5,7},
	{1,2},{5,6},{0,4},{3,7},
	{1,5},{2,6},
	{1,4},{3,6},
	{2,4},{3,5},
	{3,4}
};

/* Deliberately broken: NET4 with its last comparator removed. It must NOT
   score perfectly. A test suite that only checks correct inputs cannot tell a
   working evaluator from one that returns "sorted" unconditionally. */
static const Cmp NET4_BROKEN[] = {
	{0,1},{2,3},
	{0,2},{1,3}
};

struct Case { const char *name; int n; const Cmp *net; int len; int want_perfect; };

static int selftest(void)
{
	struct Case cases[] = {
		{"n=4, optimal 5 comparators",   4, NET4,        5,  1},
		{"n=8, optimal 19 comparators",  8, NET8,       19,  1},
		{"n=4, 4 comparators (broken)",  4, NET4_BROKEN, 4,  0},
		{"n=4, empty network",           4, NET4,        0,  0},
		{"n=6, empty network",           6, NET4,        0,  0},
	};
	int fails = 0;

	printf("%-32s %8s %8s  %s\n", "network", "sorted", "of", "verdict");
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
		Eval e;
		if (ev_init(&e, cases[i].n) != 0) { printf("init failed\n"); return 1; }
		uint64_t total = 1ULL << cases[i].n;
		uint64_t got   = ev_fitness(&e, cases[i].net, cases[i].len);
		int perfect    = (got == total);
		int ok         = (perfect == cases[i].want_perfect);
		if (!ok) fails++;
		printf("%-32s %8llu %8llu  %s\n", cases[i].name,
		       (unsigned long long)got, (unsigned long long)total,
		       ok ? "ok" : "MISMATCH");
		ev_free(&e);
	}

	/* An empty network still sorts the inputs that were already sorted --
	   for n=4 those are the 5 vectors 0000,0001,0011,0111,1111. Checking the
	   exact number, not just "not perfect", catches an evaluator that
	   miscounts rather than one that merely fails. */
	Eval e; ev_init(&e, 4);
	uint64_t empty = ev_fitness(&e, NET4, 0);
	int ok = (empty == 5);
	if (!ok) fails++;
	printf("%-32s %8llu %8llu  %s\n", "n=4 empty: already-sorted count",
	       (unsigned long long)empty, 5ULL, ok ? "ok" : "MISMATCH");
	ev_free(&e);

	printf("\n%s (%d failure%s)\n", fails ? "SELFTEST FAILED" : "SELFTEST PASSED",
	       fails, fails == 1 ? "" : "s");
	return fails ? 1 : 0;
}

#endif /* !EVAL_NO_MAIN -- end of test fixtures */

/* gp.c includes this file directly to reuse the evaluator without a header
   and without a second translation unit -- the search is the only consumer and
   the two are versioned together. It defines EVAL_NO_MAIN to suppress this. */
#ifndef EVAL_NO_MAIN
int main(int argc, char **argv)
{
	if (argc > 1 && !strcmp(argv[1], "--selftest")) return selftest();
	fprintf(stderr, "usage: %s --selftest\n", argv[0]);
	return 2;
}
#endif
