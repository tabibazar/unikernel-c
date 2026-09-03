/* dyn_aco.c -- does pheromone memory survive the map changing under it?
 *
 * The claim that has been running through this repo is that Ant Colony
 * Optimization's forgetting mechanism is an error-correction mechanism:
 * evaporation erases a perturbation within a few iterations, so a corrupted
 * pheromone write does not accumulate. That was the fault-tolerance argument
 * for a platform that miscomputes.
 *
 * Stale pheromone from a CHANGED WORLD is structurally the same object as
 * corrupted pheromone. Both are trail strength that points somewhere the
 * problem no longer rewards. So this tests the same mechanism directly, on a
 * machine that computes correctly, with no fault injection needed.
 *
 * THE EXPERIMENT
 *
 * Solve a Euclidean TSP for a while. Then delete a fraction of the cities
 * mid-run, leaving their pheromone in place -- ghost cities, trails leading to
 * nowhere. Keep going. Three arms, forked from one identical pre-change state:
 *
 *   RESTART       throw the pheromone away and re-solve from scratch.
 *                 The control. If memory is worthless, this wins.
 *   RETAIN        keep everything, ghosts included, and let evaporation
 *                 deal with them. The claim under test.
 *   RETAIN_CLEAN  keep the pheromone but explicitly zero the ghost rows.
 *                 The discriminator, and the reason this experiment is worth
 *                 running rather than assuming.
 *
 * WHY THE THIRD ARM IS THE POINT
 *
 * RETAIN beating RESTART would only show that memory helps -- unsurprising,
 * and it would not say whether the ghosts were harmless or merely outweighed.
 * The interesting question is narrower: DO THE GHOSTS COST ANYTHING? If
 * RETAIN and RETAIN_CLEAN come out level, evaporation absorbed them and the
 * forgetting-as-error-correction claim holds on its own terms. If
 * RETAIN_CLEAN beats RETAIN, ghosts actively hurt and evaporation is not
 * enough by itself -- which would be a real dent in the argument, and is the
 * outcome this is built to be able to report.
 *
 * PRE-REGISTERED, before any of it was run:
 *
 *   1. RETAIN beats RESTART at small change fractions.
 *   2. There is a crossover -- some fraction above which RESTART wins,
 *      because too little of the old world survives to be worth remembering.
 *      Finding where it sits is the actual result.
 *   3. RETAIN and RETAIN_CLEAN are within noise of each other. If they are
 *      not, the error-correction claim is weaker than this repo has been
 *      saying, and that gets reported instead.
 *
 * All three arms fork from ONE pre-change run with one RNG state, so they see
 * an identical world and an identical starting memory. Anything that differs
 * afterwards is the arm.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/* ------------------------------------------------------------- parameters */

#ifndef DA_N
#define DA_N 200                 /* cities in the original instance */
#endif
#ifndef DA_ANTS
#define DA_ANTS 25
#endif
#ifndef DA_RHO
#define DA_RHO 0.02              /* evaporation, matching aco.c */
#endif
#ifndef DA_BETA
#define DA_BETA 2                /* integer, so no pow() is needed */
#endif
#ifndef DA_CAND
#define DA_CAND 20
#endif
#ifndef DA_SEED
#define DA_SEED 20260903ULL
#endif
#ifndef DA_PRE_ITERS
#define DA_PRE_ITERS 3000        /* iterations before the map changes */
#endif
#ifndef DA_POST_ITERS
#define DA_POST_ITERS 3000       /* iterations after */
#endif
#ifndef DA_REMOVE_PCT
#define DA_REMOVE_PCT 20         /* percent of cities deleted at the change */
#endif
#ifndef DA_TRIALS
#define DA_TRIALS 5              /* independent instances/seeds, averaged */
#endif
#ifndef DA_MODE
#define DA_MODE 1                /* 0 = delete cities, 1 = relocate them */
#endif
#ifndef DA_LOCAL_SEARCH
#define DA_LOCAL_SEARCH 1        /* 0 = construction only, pheromone-driven */
#endif
#ifndef DA_TRACE
#define DA_TRACE 0               /* 1 = print the post-change recovery curve */
#endif

#define CAND_LEN ((DA_CAND) < (DA_N - 1) ? (DA_CAND) : (DA_N - 1))

/* ------------------------------------------------------------------- rng */

static uint64_t rs[4];
static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

static uint64_t nrand(void)
{
	uint64_t r = rotl(rs[0] + rs[3], 23) + rs[0];
	uint64_t t = rs[1] << 17;
	rs[2] ^= rs[0]; rs[3] ^= rs[1]; rs[1] ^= rs[2]; rs[0] ^= rs[3];
	rs[2] ^= t; rs[3] = rotl(rs[3], 45);
	return r;
}
static double urand(void) { return (double)(nrand() >> 11) * 0x1.0p-53; }

static void seed_rng(uint64_t s)
{
	for (int i = 0; i < 4; i++) {
		s += 0x9E3779B97F4A7C15ULL;
		uint64_t z = s;
		z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
		z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
		rs[i] = z ^ (z >> 31);
	}
}

/* --------------------------------------------------------------- instance */

static double cx[DA_N], cy[DA_N];
static int32_t dist[DA_N][DA_N];
static unsigned char active[DA_N];     /* 0 = ghost city, deleted mid-run */
static unsigned char stale[DA_N];      /* 1 = city relocated; its trails now lie */
static int live[DA_N], n_live;         /* compacted list of active cities */

/* Integer sqrt by Newton, because the target platform has no math library and
   this program is meant to build there unchanged. Distances are rounded the
   TSPLIB way, so tour lengths are exact integers and comparisons between arms
   can never turn on a floating-point tie. */
static int32_t isqrt_round(double v)
{
	if (v <= 0) return 0;
	double x = v, prev = 0;
	for (int i = 0; i < 60 && x != prev; i++) { prev = x; x = 0.5 * (x + v / x); }
	return (int32_t)(x + 0.5);
}

static void build_instance(void)
{
	for (int i = 0; i < DA_N; i++) {
		cx[i] = urand() * 1000.0;
		cy[i] = urand() * 1000.0;
	}
	for (int i = 0; i < DA_N; i++)
		for (int j = 0; j < DA_N; j++) {
			double dx = cx[i] - cx[j], dy = cy[i] - cy[j];
			dist[i][j] = isqrt_round(dx * dx + dy * dy);
		}
	for (int i = 0; i < DA_N; i++) active[i] = 1;
}

static void rebuild_live(void)
{
	n_live = 0;
	for (int i = 0; i < DA_N; i++) if (active[i]) live[n_live++] = i;
}

/* Candidate lists are rebuilt after the change, because a nearest-neighbour
   list that still names deleted cities would quietly starve construction of
   choices and look like an algorithmic result. */
static int16_t cand[DA_N][CAND_LEN];
static int cand_len_live;

static void build_candidates(void)
{
	cand_len_live = CAND_LEN < n_live - 1 ? CAND_LEN : n_live - 1;
	if (cand_len_live < 1) cand_len_live = 1;

	for (int a = 0; a < n_live; a++) {
		int i = live[a];
		int32_t best_d[CAND_LEN];
		for (int k = 0; k < cand_len_live; k++) { best_d[k] = INT32_MAX; cand[i][k] = -1; }
		for (int b = 0; b < n_live; b++) {
			int j = live[b];
			if (j == i) continue;
			int32_t d = dist[i][j];
			for (int k = 0; k < cand_len_live; k++) {
				if (d < best_d[k]) {
					for (int m = cand_len_live - 1; m > k; m--) {
						best_d[m] = best_d[m - 1]; cand[i][m] = cand[i][m - 1];
					}
					best_d[k] = d; cand[i][k] = (int16_t)j;
					break;
				}
			}
		}
	}
}

/* -------------------------------------------------------------- pheromone */

static float tau[DA_N][DA_N];
static float tau_min, tau_max;

static void set_bounds(int32_t l_best)
{
	tau_max = 1.0f / (DA_RHO * (float)l_best);
	tau_min = tau_max / (2.0f * (float)n_live);
}

static void tau_reset_all(void)
{
	for (int i = 0; i < DA_N; i++)
		for (int j = 0; j < DA_N; j++) tau[i][j] = tau_max;
}

/* Reset the trails that now lie: every edge touching a city that moved (or,
   in delete mode, one that vanished). This is the ONLY difference between
   RETAIN and RETAIN_CLEAN.
 *
 * Deletion turned out to make that comparison vacuous. A deleted city is
 * dropped from the candidate lists and from construction, so nothing ever
 * reads its pheromone and zeroing it cannot change a single decision -- the
 * two arms came out bit-identical for a reason that had nothing to do with
 * evaporation. Relocation is the honest version of the question: the city is
 * still there, still chosen between, and its stored trail strength is now
 * simply wrong. That stale value IS read, on every construction step, and
 * only evaporation can remove it. */
static void tau_clear_stale(void)
{
	for (int i = 0; i < DA_N; i++) {
		int lies = DA_MODE ? stale[i] : !active[i];
		if (!lies) continue;
		for (int j = 0; j < DA_N; j++) { tau[i][j] = tau_max; tau[j][i] = tau_max; }
	}
}

/* ------------------------------------------------------------ tour + 2-opt */

static int  ant_tour[DA_N];
static unsigned char visited[DA_N];
static int  best_tour[DA_N];
static int32_t best_len;
static long improvements;   /* times an ant beat the incumbent */

static int32_t tour_len(const int *t, int n)
{
	int32_t s = 0;
	for (int i = 0; i < n; i++) s += dist[t[i]][t[(i + 1) % n]];
	return s;
}

static int pos_[DA_N];

static void two_opt(int *t, int n)
{
	for (int i = 0; i < n; i++) pos_[t[i]] = i;
	int improved = 1, guard = 0;
	while (improved && guard++ < 200) {
		improved = 0;
		for (int a = 0; a < n; a++) {
			int c1 = t[a], c1n = t[(a + 1) % n];
			for (int k = 0; k < cand_len_live; k++) {
				int c2 = cand[c1][k];
				if (c2 < 0 || !active[c2]) continue;
				int b = pos_[c2];
				int c2n = t[(b + 1) % n];
				if (c2 == c1 || c2n == c1) continue;
				int32_t before = dist[c1][c1n] + dist[c2][c2n];
				int32_t after  = dist[c1][c2]  + dist[c1n][c2n];
				if (after < before) {
					int i = (a + 1) % n, j = b;
					while (i != j && t[i] != t[(j + 1) % n]) {
						int tmp = t[i]; t[i] = t[j]; t[j] = tmp;
						pos_[t[i]] = i; pos_[t[j]] = j;
						i = (i + 1) % n;
						if (i == j) break;
						j = (j + n - 1) % n;
					}
					improved = 1;
					break;
				}
			}
		}
	}
}

static void construct(int *out)
{
	memset(visited, 0, sizeof visited);
	int cur = live[(int)(nrand() % (uint64_t)n_live)];
	out[0] = cur; visited[cur] = 1;

	for (int step = 1; step < n_live; step++) {
		double w[CAND_LEN]; int who[CAND_LEN]; int m = 0; double tot = 0.0;

		for (int k = 0; k < cand_len_live; k++) {
			int j = cand[cur][k];
			if (j < 0 || !active[j] || visited[j]) continue;
			double eta = 1.0 / (double)(dist[cur][j] > 0 ? dist[cur][j] : 1);
			double e = eta; for (int b = 1; b < DA_BETA; b++) e *= eta;
			double v = (double)tau[cur][j] * e;
			w[m] = v; who[m] = j; tot += v; m++;
		}

		int nxt = -1;
		if (m > 0 && tot > 0.0) {
			double r = urand() * tot, acc = 0.0;
			for (int k = 0; k < m; k++) { acc += w[k]; if (r <= acc) { nxt = who[k]; break; } }
			if (nxt < 0) nxt = who[m - 1];
		} else {
			/* Candidate list exhausted -- fall back to the nearest unvisited
			   live city. Without this the tour would simply be short, which
			   validates as a bug rather than showing up as poor quality. */
			int32_t bd = INT32_MAX;
			for (int a = 0; a < n_live; a++) {
				int j = live[a];
				if (visited[j]) continue;
				if (dist[cur][j] < bd) { bd = dist[cur][j]; nxt = j; }
			}
		}
		out[step] = nxt; visited[nxt] = 1; cur = nxt;
	}
}

static void clamp_all(void)
{
	for (int a = 0; a < n_live; a++)
		for (int b = 0; b < n_live; b++) {
			int i = live[a], j = live[b];
			if (tau[i][j] < tau_min) tau[i][j] = tau_min;
			if (tau[i][j] > tau_max) tau[i][j] = tau_max;
		}
}

/* One MMAS iteration: every ant builds and improves a tour, the global best
   is updated, then evaporation is applied and only the best deposits. */
static void iterate(void)
{
	for (int k = 0; k < DA_ANTS; k++) {
		construct(ant_tour);
#if DA_LOCAL_SEARCH
		two_opt(ant_tour, n_live);
#endif
		int32_t l = tour_len(ant_tour, n_live);
		if (l < best_len) {
			best_len = l;
			improvements++;
			memcpy(best_tour, ant_tour, sizeof(int) * n_live);
			set_bounds(best_len);
		}
	}

	/* Evaporation is applied to the LIVE sub-matrix only. Ghost entries are
	   deliberately left frozen rather than decayed, which is the harsher test:
	   if evaporation were also quietly erasing them, RETAIN would be getting
	   help this experiment is supposed to be measuring the need for. */
	for (int a = 0; a < n_live; a++)
		for (int b = 0; b < n_live; b++) {
			int i = live[a], j = live[b];
			tau[i][j] *= (1.0f - (float)DA_RHO);
		}

	float dep = 1.0f / (float)best_len;
	for (int i = 0; i < n_live; i++) {
		int a = best_tour[i], b = best_tour[(i + 1) % n_live];
		tau[a][b] += dep; tau[b][a] += dep;
	}
	clamp_all();
}

/* ------------------------------------------------------------------ arms */

/* Snapshot of the world at the moment of change, so all three arms start
   from bit-identical state. */
static float  snap_tau[DA_N][DA_N];
static int    snap_best[DA_N];
static int32_t snap_len;
static uint64_t snap_rs[4];
static unsigned char snap_active[DA_N];
static unsigned char snap_stale[DA_N];
static int32_t snap_dist[DA_N][DA_N];

#define N_CHECK 5
static const int checkpoint[N_CHECK] = { 5, 15, 40, 100, DA_POST_ITERS };
static int32_t out_curve[N_CHECK];

enum { ARM_RESTART, ARM_RETAIN, ARM_RETAIN_CLEAN, ARM_SHUFFLE, N_ARMS };
static const char *arm_name[N_ARMS] =
	{ "RESTART", "RETAIN", "RETAIN_CLEAN", "SHUFFLE" };

/* SHUFFLE is the control that decides what RETAIN's advantage actually is.
 *
 * A converged pheromone matrix differs from a fresh one in TWO ways at once:
 * it remembers which edges were good, and it is concentrated rather than
 * uniform. RESTART installs a flat tau_max everywhere, so any win by RETAIN
 * could be memory or could just be that a low-entropy distribution exploits
 * faster on a short horizon. The 100%-relocation row forces the question --
 * there is no old map left to remember, and RETAIN still won.
 *
 * This arm permutes the pheromone matrix through a random relabelling of the
 * cities. The multiset of values is preserved exactly, so concentration is
 * untouched; the correspondence between a value and the edge it was earned on
 * is destroyed. If SHUFFLE matches RETAIN, the advantage was never memory. If
 * RETAIN beats SHUFFLE, the remembered structure is doing real work. */
static void tau_shuffle(void)
{
	int perm[DA_N];
	for (int i = 0; i < DA_N; i++) perm[i] = i;
	for (int i = DA_N - 1; i > 0; i--) {
		int j = (int)(nrand() % (uint64_t)(i + 1));
		int t = perm[i]; perm[i] = perm[j]; perm[j] = t;
	}
	static float tmp[DA_N][DA_N];
	memcpy(tmp, tau, sizeof tau);
	for (int i = 0; i < DA_N; i++)
		for (int j = 0; j < DA_N; j++) tau[i][j] = tmp[perm[i]][perm[j]];
}

static int32_t run_arm(int arm)
{
	memcpy(tau, snap_tau, sizeof tau);
	memcpy(best_tour, snap_best, sizeof best_tour);
	memcpy(active, snap_active, sizeof active);
	memcpy(stale, snap_stale, sizeof stale);
	memcpy(dist, snap_dist, sizeof dist);
	memcpy(rs, snap_rs, sizeof rs);
	rebuild_live();
	build_candidates();

	/* The old best tour still contains deleted cities, so its length is
	   meaningless now. Every arm re-seeds its incumbent the same way: strip
	   the ghosts out of the old tour and 2-opt what is left. Restart differs
	   only in its pheromone, not in its starting tour. */
	int t[DA_N], m = 0;
	for (int i = 0; i < DA_N; i++) if (active[snap_best[i]]) t[m++] = snap_best[i];
	two_opt(t, m);
	best_len = tour_len(t, m);
	memcpy(best_tour, t, sizeof(int) * m);
	set_bounds(best_len);

	improvements = 0;

	if (arm == ARM_RESTART)           tau_reset_all();
	else if (arm == ARM_RETAIN_CLEAN) tau_clear_stale();
	else if (arm == ARM_SHUFFLE)      tau_shuffle();
	/* ARM_RETAIN: leave the ghosts exactly where they are. */

	/* Checkpoints, because the endpoint is the wrong metric here. With 2-opt
	   running on every ant, all three arms converge to the same local optimum
	   given enough iterations -- so a final-quality comparison reports a tie
	   for a reason that has nothing to do with pheromone. If memory is worth
	   anything it is worth it EARLY, in how fast the colony re-finds a good
	   tour after the world moves. That is what these measure. */
	for (int it = 0; it < DA_POST_ITERS; it++) {
		iterate();
		for (int c = 0; c < N_CHECK; c++)
			if (it + 1 == checkpoint[c]) out_curve[c] = best_len;
#if DA_TRACE
		if (it % 25 == 0)
			printf("TRACE arm=%s it=%d best=%d\n", arm_name[arm], it, best_len);
#endif
	}
	/* A run where no ant ever beat the seeded incumbent has not tested the
	   pheromone at all -- every arm would report the same number for the same
	   uninteresting reason. This repo has published one such zero before
	   (docs/aco-r1/), so the count is printed rather than assumed. */
	printf("DYN_ARM arm=%-12s improvements=%3ld  at_iter", arm_name[arm], improvements);
	for (int c = 0; c < N_CHECK; c++)
		printf(" %d:%d", checkpoint[c], out_curve[c]);
	printf("\n");
	return best_len;
}

/* ------------------------------------------------------------------- main */

int main(void)
{
	printf("DYN_START n=%d remove_pct=%d ants=%d rho=%.3f pre=%d post=%d trials=%d\n",
	       DA_N, DA_REMOVE_PCT, DA_ANTS, (double)DA_RHO,
	       DA_PRE_ITERS, DA_POST_ITERS, DA_TRIALS);

	double sum[N_ARMS] = {0};
	int wins[N_ARMS] = {0};
	double csum[N_ARMS][N_CHECK]; memset(csum, 0, sizeof csum);

	for (int trial = 0; trial < DA_TRIALS; trial++) {
		seed_rng(DA_SEED + 1000ULL * (uint64_t)trial);
		build_instance();
		rebuild_live();
		build_candidates();

		/* Pre-change: solve the full instance. */
		best_len = INT32_MAX;
		for (int i = 0; i < DA_N; i++) best_tour[i] = i;
		best_len = tour_len(best_tour, n_live);
		set_bounds(best_len);
		tau_reset_all();
		for (int it = 0; it < DA_PRE_ITERS; it++) iterate();
		int32_t pre_len = best_len;

		/* The change: delete a random subset. Their pheromone stays. */
		memcpy(snap_tau, tau, sizeof tau);
		memcpy(snap_best, best_tour, sizeof best_tour);
		snap_len = best_len;
		memcpy(snap_rs, rs, sizeof rs);

		int to_change = DA_N * DA_REMOVE_PCT / 100;
		memcpy(snap_active, active, sizeof active);
		memset(snap_stale, 0, sizeof snap_stale);
		int removed = 0;
#if DA_MODE
		/* Relocate: the city stays in the problem, but somewhere else. Its
		   pheromone survives the move and now points at where it used to be. */
		while (removed < to_change) {
			int c = (int)(nrand() % DA_N);
			if (snap_stale[c]) continue;
			snap_stale[c] = 1; removed++;
			cx[c] = urand() * 1000.0;
			cy[c] = urand() * 1000.0;
		}
		for (int i = 0; i < DA_N; i++)
			for (int j = 0; j < DA_N; j++) {
				double dx = cx[i] - cx[j], dy = cy[i] - cy[j];
				dist[i][j] = isqrt_round(dx * dx + dy * dy);
			}
#else
		while (removed < to_change) {
			int c = (int)(nrand() % DA_N);
			if (snap_active[c]) { snap_active[c] = 0; removed++; }
		}
#endif
		memcpy(snap_dist, dist, sizeof dist);

		int32_t r[N_ARMS];
		for (int a = 0; a < N_ARMS; a++) {
			r[a] = run_arm(a);
			for (int c = 0; c < N_CHECK; c++) csum[a][c] += (double)out_curve[c];
		}

		int32_t bestv = r[0];
		for (int a = 1; a < N_ARMS; a++) if (r[a] < bestv) bestv = r[a];
		for (int a = 0; a < N_ARMS; a++) {
			sum[a] += (double)r[a];
			if (r[a] == bestv) wins[a]++;
		}

			printf("DYN_TRIAL trial=%d pre=%d changed=%d restart=%d retain=%d clean=%d shuffle=%d\n",
		       trial, pre_len, removed, r[ARM_RESTART], r[ARM_RETAIN],
		       r[ARM_RETAIN_CLEAN], r[ARM_SHUFFLE]);
		fflush(stdout);
	}

	printf("DYN_MEAN remove_pct=%d restart=%.1f retain=%.1f retain_clean=%.1f\n",
	       DA_REMOVE_PCT, sum[ARM_RESTART] / DA_TRIALS,
	       sum[ARM_RETAIN] / DA_TRIALS, sum[ARM_RETAIN_CLEAN] / DA_TRIALS);
	printf("DYN_WINS restart=%d retain=%d retain_clean=%d of %d\n",
	       wins[ARM_RESTART], wins[ARM_RETAIN], wins[ARM_RETAIN_CLEAN], DA_TRIALS);

	/* Relative to RESTART, so the sign is the answer: negative means memory
	   paid for itself at this change size. */
	printf("DYN_DELTA remove_pct=%d retain_vs_restart=%+.3f%% clean_vs_retain=%+.3f%%\n",
	       DA_REMOVE_PCT,
	       100.0 * (sum[ARM_RETAIN] - sum[ARM_RESTART]) / sum[ARM_RESTART],
	       100.0 * (sum[ARM_RETAIN_CLEAN] - sum[ARM_RETAIN]) / sum[ARM_RETAIN]);
	/* The headline: mean tour length at each checkpoint, per arm. Memory
	   paying for itself looks like RETAIN below RESTART at the early
	   checkpoints and the gap closing by the last one. */
	printf("\n%-14s", "checkpoint");
	for (int c = 0; c < N_CHECK; c++) printf("%12d", checkpoint[c]);
	printf("\n");
	for (int a = 0; a < N_ARMS; a++) {
		printf("DYN_CURVE %-12s", arm_name[a]);
		for (int c = 0; c < N_CHECK; c++) printf("%12.1f", csum[a][c] / DA_TRIALS);
		printf("\n");
	}
	printf("DYN_MEMORY shuffle_vs_retain=%+.3f%%   (>0 means remembered structure did real work)\n",
	       100.0 * (csum[ARM_SHUFFLE][N_CHECK-1] - csum[ARM_RETAIN][N_CHECK-1])
	             / csum[ARM_RETAIN][N_CHECK-1]);
	printf("DYN_EARLY retain_vs_restart_at_iter%d=%+.3f%%\n", checkpoint[0],
	       100.0 * (csum[ARM_RETAIN][0] - csum[ARM_RESTART][0]) / csum[ARM_RESTART][0]);
	printf("DYN_GHOSTCOST clean_vs_retain_at_iter%d=%+.3f%%\n", checkpoint[0],
	       100.0 * (csum[ARM_RETAIN_CLEAN][0] - csum[ARM_RETAIN][0]) / csum[ARM_RETAIN][0]);
	printf("DYN_DONE\n");
	return 0;
}
