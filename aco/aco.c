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
   iteration rate forces it. Measured, 20s, seed 1: pcb442 9,372 -> 18,636
   iterations and rat783 4,424 -> 8,644, for +0.37 MB and +1.17 MB. Roughly 2x
   for about a megabyte, inside a 16 MiB budget. Verified a pure optimisation:
   same seed and iteration count give a bit-identical tour either way. Adopted. */
#ifndef ACO_DIST_CACHE
#define ACO_DIST_CACHE 1
#endif

/* 2-opt local search on every constructed tour. Canonical MMAS for TSP runs a
   local search and this engine originally had none, which is why the gap grew
   with n: 0.45% at n=100 but 14.1% at n=442, with nine of ten seeds converging
   to the identical tour. Switchable so the contribution stays measurable. */
#ifndef ACO_LOCAL_SEARCH
#define ACO_LOCAL_SEARCH 1
#endif

/* Fixed iteration budget, compiled in. BareMetal passes no argv -- the same
   reason it has no environment -- so a build for it cannot be told what to do
   on a command line, and the --seconds default is useless there because
   clock() does not advance: the run spins forever. Set this for any BareMetal
   build; leave it 0 on a host, where argv works. */
#ifndef ACO_ITERS
#define ACO_ITERS 0
#endif

/* Likewise the seed: no argv means it has to be baked in per build. */
#ifndef ACO_SEED
#define ACO_SEED 1
#endif

/* Print a progress line every N iterations. A swarm worker's console is the
   only channel back on BareMetal Cloud (`bm-api.sh instances logs`), and a run
   that prints nothing until it finishes is indistinguishable from one that
   has hung. 0 disables. */
#ifndef ACO_PROGRESS_EVERY
#define ACO_PROGRESS_EVERY 0
#endif

/* Worker identity, for a swarm. Purely cosmetic in R2: the islands are
   independent and coordinate through nothing at all. */
#ifndef ACO_WORKER_ID
#define ACO_WORKER_ID 0
#endif

/* Island migration through a shared register. Off by default: R2 is the
   control arm and coordinates through nothing at all. When on, the register is
   an S3 bucket addressed by presigned URLs compiled in from
   build/register_urls.h -- the URL carries its own credential, so the guest
   does a plain HTTPS PUT or GET and needs no signing code.
   The interval is counted in ITERATIONS, not seconds: clock() does not advance
   on BareMetal, so a time-based interval cannot exist there. */
#ifndef ACO_MIGRATE
#define ACO_MIGRATE 0
#endif

/* Dump the final tour as city indices, for plotting. Off by default: a swarm
   worker's console is a scarce channel and a 442-city tour is a lot of it. */
#ifndef ACO_PRINT_TOUR
#define ACO_PRINT_TOUR 0
#endif
#ifndef ACO_MIGRATE_EVERY
#define ACO_MIGRATE_EVERY 200
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
/* Lazily built, because a caller that reaches euc2d before build_candidates
   would otherwise read a zero-filled matrix and get silent nonsense rather
   than a crash. The branch is perfectly predicted after the first call. */
static inline int32_t euc2d(int a, int b) {
    if (!dist_ready) build_dist();
    return (int32_t)dist[a][b];
}
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


/* ---- 2-opt with neighbour lists and don't-look bits ----
   The standard formulation (Bentley; Johnson & McGeoch). Three things make it
   affordable: candidates are distance-sorted so the scan breaks as soon as
   d(a,c) >= d(a,b) and no gain is possible; a don't-look bit retires a city
   until one of its neighbours changes; and a reversal always flips the shorter
   of the two arcs, which bounds each move at n/2 swaps. */
#if ACO_LOCAL_SEARCH
static int           pos_[ACO_N];
static unsigned char dlb[ACO_N];

static inline int nxt(int i) { return (i + 1) % ACO_N; }
static inline int prv(int i) { return (i + ACO_N - 1) % ACO_N; }

/* Reverse tour positions i..j inclusive, walking forward from i. Flipping the
   complement yields the same cyclic tour, so take whichever arc is shorter. */
static void reverse_seg(int *t, int i, int j) {
    const int n = ACO_N;
    int len = (j - i + n) % n + 1;
    if (len > n - len) {
        int ni = (j + 1) % n, nj = (i - 1 + n) % n;
        i = ni; j = nj; len = n - len;
    }
    for (int k = 0; k < len / 2; k++) {
        int x = (i + k) % n, y = (j - k + n) % n;
        int tx = t[x], ty = t[y];
        t[x] = ty; t[y] = tx;
        pos_[ty] = x; pos_[tx] = y;
    }
}

static void two_opt(int *t) {
    for (int i = 0; i < ACO_N; i++) pos_[t[i]] = i;
    memset(dlb, 0, sizeof dlb);
    int active = 1;
    while (active) {
        active = 0;
        for (int a = 0; a < ACO_N; a++) {
            if (dlb[a]) continue;
            int moved = 0;
            for (int dir = 0; dir < 2 && !moved; dir++) {
                int ia = pos_[a];
                int ib = dir ? prv(ia) : nxt(ia);
                int b  = t[ib];
                int32_t d_ab = euc2d(a, b);
                for (int k = 0; k < CAND_LEN; k++) {
                    int c = cand[a][k];
                    if (c == a || c == b) continue;
                    int32_t d_ac = euc2d(a, c);
                    if (d_ac >= d_ab) break;   /* sorted: no further gain */
                    int ic = pos_[c];
                    int id = dir ? prv(ic) : nxt(ic);
                    int d  = t[id];
                    if (d == a) continue;
                    int32_t gain = d_ab + euc2d(c, d) - d_ac - euc2d(b, d);
                    if (gain > 0) {
                        /* succ: ...a b...c d... -> reverse b..c, giving a-c and b-d
                           pred: ...b a...d c... -> reverse a..d, giving a-c and b-d */
                        if (dir == 0) reverse_seg(t, pos_[b], pos_[c]);
                        else          reverse_seg(t, pos_[a], pos_[d]);
                        dlb[a] = dlb[b] = dlb[c] = dlb[d] = 0;
                        moved = 1; active = 1;
                        break;
                    }
                }
            }
            if (!moved) dlb[a] = 1;
        }
    }
}
#endif

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
#if ACO_LOCAL_SEARCH
    two_opt(best_tour);
    best_len = tour_length(best_tour);
#endif
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
#if ACO_LOCAL_SEARCH
        two_opt(ant_tour);
#endif
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


#if ACO_MIGRATE
#include <curl/curl.h>
#include "register_urls.h"

/* base64 of the tour as uint16 city indices, little endian. Plain text so the
   payload parses with sscanf and no JSON library is linked into the image. */
static const char B64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t b64_encode(const unsigned char *in, size_t n, char *out) {
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        unsigned v = in[i] << 16;
        if (i + 1 < n) v |= in[i+1] << 8;
        if (i + 2 < n) v |= in[i+2];
        out[o++] = B64[(v >> 18) & 63];
        out[o++] = B64[(v >> 12) & 63];
        out[o++] = (i + 1 < n) ? B64[(v >> 6) & 63] : '=';
        out[o++] = (i + 2 < n) ? B64[v & 63]        : '=';
    }
    out[o] = 0;
    return o;
}

static int b64_val(char c) {
    const char *p = strchr(B64, c);
    return (p && c) ? (int)(p - B64) : -1;
}

static size_t b64_decode(const char *in, unsigned char *out, size_t cap) {
    size_t o = 0; int q[4]; int k = 0;
    for (const char *p = in; *p && *p != '\n'; p++) {
        if (*p == '=' ) break;
        int v = b64_val(*p);
        if (v < 0) continue;
        q[k++] = v;
        if (k == 4) {
            unsigned x = (q[0]<<18)|(q[1]<<12)|(q[2]<<6)|q[3];
            if (o < cap) out[o++] = (x >> 16) & 0xFF;
            if (o < cap) out[o++] = (x >> 8) & 0xFF;
            if (o < cap) out[o++] = x & 0xFF;
            k = 0;
        }
    }
    if (k == 3) {
        unsigned x = (q[0]<<18)|(q[1]<<12)|(q[2]<<6);
        if (o < cap) out[o++] = (x >> 16) & 0xFF;
        if (o < cap) out[o++] = (x >> 8) & 0xFF;
    } else if (k == 2) {
        unsigned x = (q[0]<<18)|(q[1]<<12);
        if (o < cap) out[o++] = (x >> 16) & 0xFF;
    }
    return o;
}

static size_t discard_reg_cb(void *p, size_t sz, size_t nm, void *u) {
    (void)p; (void)u; return sz * nm;
}

struct membuf { char *p; size_t n, cap; };
static size_t collect_cb(void *data, size_t sz, size_t nm, void *u) {
    struct membuf *m = (struct membuf *)u;
    size_t add = sz * nm;
    if (m->n + add < m->cap) { memcpy(m->p + m->n, data, add); m->n += add; m->p[m->n] = 0; }
    return add;
}

/* Weak symbols, as prime_hunter.c does: the CA bundle is either a file on the
   host or a blob linked in by the BareMetal port. */
__attribute__((weak)) const unsigned char cacert_pem[1];
__attribute__((weak)) const unsigned int  cacert_pem_len;

static void reg_ca(CURL *h) {
    FILE *f = fopen("/etc/ssl/certs/ca-certificates.crt", "r");
    if (f) { fclose(f); curl_easy_setopt(h, CURLOPT_CAINFO, "/etc/ssl/certs/ca-certificates.crt"); return; }
    f = fopen("/etc/ssl/cacert.pem", "r");
    if (f) { fclose(f); curl_easy_setopt(h, CURLOPT_CAINFO, "/etc/ssl/cacert.pem"); return; }
    if (cacert_pem_len > 0) {
        struct curl_blob blob = { (void *)cacert_pem, cacert_pem_len, CURL_BLOB_NOCOPY };
        curl_easy_setopt(h, CURLOPT_CAINFO_BLOB, &blob);
    }
}

static char reg_payload[8 + 4 * ACO_N + 64];
static char reg_recv[8 + 4 * ACO_N + 64];

/* Publish this island's best tour to its own key. No other worker writes it,
   so there is no race and no compare-and-swap. */
static int reg_publish(void) {
    static unsigned char raw[2 * ACO_N];
    for (int i = 0; i < ACO_N; i++) {
        raw[2*i]   = (unsigned char)(best_tour[i] & 0xFF);
        raw[2*i+1] = (unsigned char)((best_tour[i] >> 8) & 0xFF);
    }
    /* base64 of 2*ACO_N BYTES, not of ACO_N cities: 4 chars per 3 bytes,
       rounded up, plus the terminator. Sizing this from the city count
       overflows the buffer by roughly 2x and aborts on the first publish. */
    char b64[4 * ((2 * ACO_N + 2) / 3) + 8];
    b64_encode(raw, sizeof raw, b64);
    int n = snprintf(reg_payload, sizeof reg_payload, "len=%d tour=%s\n", best_len, b64);
    if (n < 0 || (size_t)n >= sizeof reg_payload) return 0;

    CURL *h = curl_easy_init();
    if (!h) return 0;
    curl_easy_setopt(h, CURLOPT_URL, REG_PUT_URL[ACO_WORKER_ID]);
    curl_easy_setopt(h, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(h, CURLOPT_POSTFIELDS, reg_payload);
    curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, (long)n);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, discard_reg_cb);
    curl_easy_setopt(h, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(h, CURLOPT_USERAGENT, "BareMetal-ACO/1.0");
    reg_ca(h);
    CURLcode rc = curl_easy_perform(h);
    long status = 0;
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(h);
    return (rc == CURLE_OK && status == 200);
}

/* Read the peers and adopt the best tour better than ours. A 404 means that
   peer has published nothing yet, which is normal early on and not an error. */
static int reg_fetch(int *out_tour, int32_t *out_len) {
    int found = 0;
    for (int w = 0; w < REG_WORKERS; w++) {
        if (w == ACO_WORKER_ID) continue;
        struct membuf m = { reg_recv, 0, sizeof reg_recv };
        reg_recv[0] = 0;
        CURL *h = curl_easy_init();
        if (!h) continue;
        curl_easy_setopt(h, CURLOPT_URL, REG_GET_URL[w]);
        curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, collect_cb);
        curl_easy_setopt(h, CURLOPT_WRITEDATA, &m);
        curl_easy_setopt(h, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(h, CURLOPT_USERAGENT, "BareMetal-ACO/1.0");
        reg_ca(h);
        CURLcode rc = curl_easy_perform(h);
        long status = 0;
        curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
        curl_easy_cleanup(h);
        if (rc != CURLE_OK || status != 200) continue;   /* 404 = nothing yet */

        int plen = 0;
        char *tp = strstr(reg_recv, "tour=");
        if (sscanf(reg_recv, "len=%d", &plen) != 1 || !tp) continue;
        if (plen <= 0 || plen >= *out_len) continue;     /* not an improvement */

        static unsigned char raw[2 * ACO_N];
        size_t got = b64_decode(tp + 5, raw, sizeof raw);
        if (got != sizeof raw) continue;
        static int cand_tour[ACO_N];
        for (int i = 0; i < ACO_N; i++)
            cand_tour[i] = (int)(raw[2*i] | (raw[2*i+1] << 8));
        if (!tour_is_valid(cand_tour)) continue;         /* never trust the wire */
        if (tour_length(cand_tour) != plen) continue;    /* claimed length must be real */
        memcpy(out_tour, cand_tour, sizeof cand_tour);
        *out_len = plen;
        found = 1;
    }
    return found;
}
#endif /* ACO_MIGRATE */

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
    double seconds = 10.0; uint64_t seed = (uint64_t)ACO_SEED;
    long iters = (ACO_ITERS > 0) ? (long)ACO_ITERS : -1;
    for (int i = 1; i < argc - 1; i++) {
        if      (!strcmp(argv[i], "--seconds")) seconds = atof(argv[++i]);
        else if (!strcmp(argv[i], "--seed"))    seed    = strtoull(argv[++i], 0, 10);
        else if (!strcmp(argv[i], "--iters"))   iters   = strtol(argv[++i], 0, 10);
    }
    printf("ACO_START worker=%d instance=%s n=%d optimum=%d beta=%d ants=%d cache=%d ls=%d seed=%llu static_bytes=%zu\n",
           ACO_WORKER_ID, ACO_NAME, ACO_N, ACO_OPTIMUM, ACO_BETA, ACO_ANTS, ACO_DIST_CACHE,
           ACO_LOCAL_SEARCH, (unsigned long long)seed, static_bytes());

#if ACO_MIGRATE
    curl_global_init(CURL_GLOBAL_DEFAULT);
#endif
    rng_seed(seed); mmas_init();
    clock_t t0 = clock(); long it = 0;
    /* If clock() never advances (BareMetal), a seconds-based run cannot
       terminate. Refuse rather than spin, and say why. */
    if (iters <= 0) {
        clock_t probe = clock();
        for (volatile long z = 0; z < 20000000L && clock() == probe; z++) { }
        if (clock() == probe) {
            printf("ACO_ABORT reason=clock_does_not_advance "
                   "hint=build_with_-DACO_ITERS=N\n");
            return 2;
        }
    }
    for (;;) {
        mmas_iterate(); it++;
#if ACO_MIGRATE
        if (it % (ACO_MIGRATE_EVERY) == 0) {
            int32_t peer_len = best_len;
            static int peer_tour[ACO_N];
            if (reg_fetch(peer_tour, &peer_len) && peer_len < best_len) {
                /* Adopt the peer's tour and let evaporation carry it into the
                   pheromone the usual way, rather than special-casing it. */
                memcpy(best_tour, peer_tour, sizeof best_tour);
                best_len = peer_len;
                printf("ACO_MIGRATE_IN worker=%d iters=%ld adopted=%d\n",
                       ACO_WORKER_ID, it, best_len);
            }
            if (reg_publish())
                printf("ACO_MIGRATE_OUT worker=%d iters=%ld published=%d\n",
                       ACO_WORKER_ID, it, best_len);
        }
#endif
#if ACO_PROGRESS_EVERY > 0
        if (it % (ACO_PROGRESS_EVERY) == 0) {
            long g = (long)(10000.0 * (double)(best_len - ACO_OPTIMUM) / (double)ACO_OPTIMUM);
            printf("ACO_PROGRESS worker=%d instance=%s seed=%llu iters=%ld best=%d gap_bp=%ld\n",
                   ACO_WORKER_ID, ACO_NAME, (unsigned long long)seed, it, best_len, g);
        }
#endif
        double el = (double)(clock() - t0) / CLOCKS_PER_SEC;
        if (iters > 0 ? it >= iters : el >= seconds) break;
    }
    double el = (double)(clock() - t0) / CLOCKS_PER_SEC;
    /* gap in basis points, so no floating-point formatting is needed on a
       platform whose printf may lack %f */
    long gap_bp = (long)(10000.0 * (double)(best_len - ACO_OPTIMUM) / (double)ACO_OPTIMUM);
    long ms = (long)(el * 1000.0);
#if ACO_PRINT_TOUR
    printf("ACO_TOUR");
    for (int i = 0; i < ACO_N; i++) printf(" %d", best_tour[i]);
    printf("\n");
    printf("ACO_NN");
    { static int nn[ACO_N]; int32_t nl = nearest_neighbour_tour(nn);
      for (int i = 0; i < ACO_N; i++) printf(" %d", nn[i]);
      printf("\nACO_NN_LEN %d\n", nl); }
#endif
    printf("ACO_DONE worker=%d instance=%s seed=%llu iters=%ld best=%d optimum=%d gap_bp=%ld "
           "ms=%ld valid=%d\n",
           ACO_WORKER_ID, ACO_NAME, (unsigned long long)seed, it, best_len, ACO_OPTIMUM, gap_bp,
           ms, tour_is_valid(best_tour));
    return 0;
}
#endif
