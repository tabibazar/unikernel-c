/* Ant Colony Optimization (MAX-MIN Ant System) for TSPLIB EUC_2D instances.
 *
 * Builds unchanged on macOS, Linux, and BareMetal. Three platform rules shape
 * every choice here and are not negotiable:
 *
 *   1. No math library. BareMetal is freestanding; sqrt is hand-rolled and
 *      alpha/beta are integers so no pow() is ever needed.
 *   2. No filesystem. The instance is compiled in (see gen_instance.py).
 *   3. No environment. Configuration is #define, overridable with -D.
 *
 *   build:  cc -O2 -std=c99 -DACO_INSTANCE_HEADER='"instances/pcb442.h"' \
 *              -o aco aco.c
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef ACO_INSTANCE_HEADER
#define ACO_INSTANCE_HEADER "instances/berlin52.h"
#endif
#include ACO_INSTANCE_HEADER

/* ---- frozen parameters (spec: not tuned against results) ---- */
#ifndef ACO_BETA
#define ACO_BETA 2               /* integer, so tau*eta^beta needs no pow() */
#endif
#define ACO_RHO        0.02      /* evaporation */
#define ACO_ANTS       25
#define ACO_CAND       20        /* candidate list length */

/* Distances are either recomputed per call or held in a uint16 matrix. The
   spec requires on-the-fly by default and a recorded decision if the measured
   iteration rate forces the cache; this switch is that decision, made visible. */
#ifndef ACO_DIST_CACHE
#define ACO_DIST_CACHE 0
#endif

/* ---- xoshiro256++, seeded by splitmix64 ----
   Not RDRAND: that costs hundreds of cycles per draw and cannot be replayed.
   RDSEED mints the seed on BareMetal; the seed is logged so a run can be
   replayed exactly, which is what makes same-seed divergence a measurement. */
static uint64_t rs[4];

static uint64_t splitmix64(uint64_t *x) {
    uint64_t z = (*x += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

void rng_seed(uint64_t seed) {
    uint64_t s = seed;
    for (int i = 0; i < 4; i++) rs[i] = splitmix64(&s);
}

static inline uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

uint64_t rng_next(void) {
    const uint64_t result = rotl(rs[0] + rs[3], 23) + rs[0];
    const uint64_t t = rs[1] << 17;
    rs[2] ^= rs[0]; rs[3] ^= rs[1]; rs[1] ^= rs[2]; rs[0] ^= rs[3];
    rs[2] ^= t;     rs[3] = rotl(rs[3], 45);
    return result;
}

/* 53 significant bits, the most a double holds exactly. */
double rng_unit(void) { return (double)(rng_next() >> 11) * (1.0 / 9007199254740992.0); }

/* ---- hand-rolled sqrt ----
   Newton-Raphson from a bit-trick seed: adding the exponent bias before the
   shift halves the exponent, which puts the guess within a few percent and
   lets Newton converge to full double precision in a handful of steps. There
   is no libm in a freestanding unikernel; docs/ising/ising.c does the same. */
double a_sqrt(double x) {
    if (x <= 0.0) return 0.0;
    union { double d; uint64_t u; } v = { x };
    v.u = (v.u + 0x3FF0000000000000ULL) >> 1;
    double r = v.d;
    for (int i = 0; i < 8; i++) r = 0.5 * (r + x / r);
    return r;
}

/* ---- distances ----
   TSPLIB EUC_2D is nint(sqrt(dx^2+dy^2)). Rounding to nearest is not a
   detail: the published optima do not reproduce with truncation. */
static inline int32_t euc2d_compute(int a, int b) {
    double dx = (double)(ACO_XY[a][0] - ACO_XY[b][0]);
    double dy = (double)(ACO_XY[a][1] - ACO_XY[b][1]);
    return (int32_t)(a_sqrt(dx * dx + dy * dy) + 0.5);
}

#if ACO_DIST_CACHE
static uint16_t dist[ACO_N][ACO_N];
static int dist_ready = 0;
static void build_dist(void) {
    for (int i = 0; i < ACO_N; i++)
        for (int j = 0; j < ACO_N; j++) dist[i][j] = (uint16_t)euc2d_compute(i, j);
    dist_ready = 1;
}
static inline int32_t euc2d(int a, int b) { return (int32_t)dist[a][b]; }
#else
static inline int32_t euc2d(int a, int b) { return euc2d_compute(a, b); }
#endif

/* A tour is a permutation of 0..N-1; its length includes the closing edge. */
int32_t tour_length(const int *tour) {
    int32_t total = 0;
    for (int i = 0; i < ACO_N; i++)
        total += euc2d(tour[i], tour[(i + 1) % ACO_N]);
    return total;
}

int tour_is_valid(const int *tour) {
    static unsigned char seen[ACO_N];
    memset(seen, 0, sizeof seen);
    for (int i = 0; i < ACO_N; i++) {
        int c = tour[i];
        if (c < 0 || c >= ACO_N || seen[c]) return 0;
        seen[c] = 1;
    }
    return 1;
}

/* Candidate lists: each city's CAND_LEN nearest neighbours, sorted.
   MMAS constructs almost entirely within these, which is what makes the
   iteration rate tolerable at n=783. 783*20*2 bytes is negligible. */
#define CAND_LEN ((ACO_CAND) < (ACO_N - 1) ? (ACO_CAND) : (ACO_N - 1))
static int16_t cand[ACO_N][CAND_LEN];

void build_candidates(void) {
#if ACO_DIST_CACHE
    if (!dist_ready) build_dist();
#endif
    for (int i = 0; i < ACO_N; i++) {
        int32_t best_d[CAND_LEN];
        for (int k = 0; k < CAND_LEN; k++) { best_d[k] = INT32_MAX; cand[i][k] = 0; }
        for (int j = 0; j < ACO_N; j++) {
            if (j == i) continue;
            int32_t d = euc2d(i, j);
            if (d >= best_d[CAND_LEN - 1]) continue;
            int k = CAND_LEN - 1;
            while (k > 0 && best_d[k - 1] > d) {
                best_d[k] = best_d[k - 1]; cand[i][k] = cand[i][k - 1]; k--;
            }
            best_d[k] = d; cand[i][k] = (int16_t)j;
        }
    }
}

/* ---- MAX-MIN Ant System ----
   Only the best ant deposits, and tau is clamped into [tau_min, tau_max].
   That clamp is what stops premature convergence, and it needs no libm:
   tau_max = 1/(rho*L_best), and tau_min = tau_max/(2n) -- the standard
   simplification, since the textbook form needs an n-th root. */
static float tau[ACO_N][ACO_N];
static float tau_min, tau_max;
int best_tour[ACO_N];
int32_t best_len;

static int    ant_tour[ACO_N];
static unsigned char visited[ACO_N];

static int32_t nearest_neighbour_tour(int *out) {
    memset(visited, 0, sizeof visited);
    out[0] = 0; visited[0] = 1;
    for (int i = 1; i < ACO_N; i++) {
        int from = out[i - 1], best = -1; int32_t bd = INT32_MAX;
        for (int j = 0; j < ACO_N; j++) {
            if (visited[j]) continue;
            int32_t d = euc2d(from, j);
            if (d < bd) { bd = d; best = j; }
        }
        out[i] = best; visited[best] = 1;
    }
    return tour_length(out);
}

static void set_bounds(int32_t l_best) {
    tau_max = (float)(1.0 / (ACO_RHO * (double)l_best));
    tau_min = tau_max / (2.0f * (float)ACO_N);
}

void mmas_init(void) {
    build_candidates();
    best_len = nearest_neighbour_tour(best_tour);
    set_bounds(best_len);
    for (int i = 0; i < ACO_N; i++)
        for (int j = 0; j < ACO_N; j++) tau[i][j] = tau_max;
}

/* weight = tau * eta^beta = tau / d^beta. beta is an integer, so this is a
   fixed number of divides -- no pow(), which is the whole reason beta is
   frozen at 2. */
static inline double weight(int i, int j) {
    int32_t d = euc2d(i, j);
    if (d <= 0) d = 1;
    double w = (double)tau[i][j];
    for (int b = 0; b < ACO_BETA; b++) w /= (double)d;
    return w;
}

static void construct(int *out) {
    memset(visited, 0, sizeof visited);
    int cur = (int)(rng_next() % (uint64_t)ACO_N);
    out[0] = cur; visited[cur] = 1;
    for (int step = 1; step < ACO_N; step++) {
        double w[CAND_LEN]; double total = 0.0; int n = 0;
        int pool[CAND_LEN];
        for (int k = 0; k < CAND_LEN; k++) {
            int j = cand[cur][k];
            if (visited[j]) continue;
            double x = weight(cur, j);
            pool[n] = j; w[n] = x; total += x; n++;
        }
        int next = -1;
        if (n > 0) {
            double r = rng_unit() * total, acc = 0.0;
            next = pool[n - 1];
            for (int k = 0; k < n; k++) { acc += w[k]; if (acc >= r) { next = pool[k]; break; } }
        } else {
            /* every candidate visited: fall back to the best unvisited city */
            double bw = -1.0;
            for (int j = 0; j < ACO_N; j++) {
                if (visited[j]) continue;
                double x = weight(cur, j);
                if (x > bw) { bw = x; next = j; }
            }
        }
        out[step] = next; visited[next] = 1; cur = next;
    }
}

static void clamp_all(void) {
    for (int i = 0; i < ACO_N; i++)
        for (int j = 0; j < ACO_N; j++) {
            if (tau[i][j] < tau_min) tau[i][j] = tau_min;
            if (tau[i][j] > tau_max) tau[i][j] = tau_max;
        }
}

int32_t mmas_iterate(void) {
    int32_t iter_best = INT32_MAX;
    static int iter_tour[ACO_N];
    for (int a = 0; a < ACO_ANTS; a++) {
        construct(ant_tour);
        int32_t len = tour_length(ant_tour);
        if (len < iter_best) { iter_best = len; memcpy(iter_tour, ant_tour, sizeof iter_tour); }
    }
    if (iter_best < best_len) {
        best_len = iter_best;
        memcpy(best_tour, iter_tour, sizeof best_tour);
        set_bounds(best_len);
    }
    /* evaporate everywhere */
    const float keep = 1.0f - (float)ACO_RHO;
    for (int i = 0; i < ACO_N; i++)
        for (int j = 0; j < ACO_N; j++) tau[i][j] *= keep;
    /* Deposit on the ITERATION best, not the global best. Depositing only on
       the global best is the documented stagnation mode of MMAS (Stuetzle &
       Hoos): the colony locks onto one tour and tau_min alone cannot pull it
       out. Measured here on berlin52 x 10 seeds: global-best reached the
       optimum 3/10 times and stalled by ~18k iterations. */
    const int   *dep     = iter_tour;
    const float  add     = (float)(1.0 / (double)iter_best);
    for (int i = 0; i < ACO_N; i++) {
        int a = dep[i], b = dep[(i + 1) % ACO_N];
        tau[a][b] += add; tau[b][a] += add;
    }
    clamp_all();
    return iter_best;
}

#ifndef ACO_NO_MAIN
/* Static footprint, printed so the 16 MiB budget is evidenced rather than
   assumed -- the same idea as agent/agent_static.c's high-water mark. */
static size_t static_bytes(void) {
    size_t n = sizeof tau + sizeof cand + sizeof best_tour
             + sizeof ant_tour + sizeof visited + sizeof ACO_XY;
#if ACO_DIST_CACHE
    n += sizeof dist;
#endif
    return n;
}

int main(int argc, char **argv) {
    double seconds = 10.0; uint64_t seed = 1; long iters = -1;
    for (int i = 1; i < argc - 1; i++) {
        if      (!strcmp(argv[i], "--seconds")) seconds = atof(argv[++i]);
        else if (!strcmp(argv[i], "--seed"))    seed    = strtoull(argv[++i], 0, 10);
        else if (!strcmp(argv[i], "--iters"))   iters   = strtol(argv[++i], 0, 10);
    }
    printf("ACO_START instance=%s n=%d optimum=%d beta=%d ants=%d cache=%d seed=%llu static_bytes=%zu\n",
           ACO_NAME, ACO_N, ACO_OPTIMUM, ACO_BETA, ACO_ANTS, ACO_DIST_CACHE,
           (unsigned long long)seed, static_bytes());

    rng_seed(seed); mmas_init();
    clock_t t0 = clock(); long it = 0;
    for (;;) {
        mmas_iterate(); it++;
        double el = (double)(clock() - t0) / CLOCKS_PER_SEC;
        if (iters > 0 ? it >= iters : el >= seconds) break;
    }
    double el = (double)(clock() - t0) / CLOCKS_PER_SEC;
    /* gap in basis points, so no floating-point formatting is needed on a
       platform whose printf may lack %f */
    long gap_bp = (long)(10000.0 * (double)(best_len - ACO_OPTIMUM) / (double)ACO_OPTIMUM);
    long ms = (long)(el * 1000.0);
    printf("ACO_DONE instance=%s seed=%llu iters=%ld best=%d optimum=%d gap_bp=%ld "
           "ms=%ld valid=%d\n",
           ACO_NAME, (unsigned long long)seed, it, best_len, ACO_OPTIMUM, gap_bp,
           ms, tour_is_valid(best_tour));
    return 0;
}
#endif
