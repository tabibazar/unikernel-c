/* Host-only test harness. Includes the engine directly so statics are
   reachable; ACO_NO_MAIN suppresses the engine's own main(). */
#define ACO_NO_MAIN 1
#include "aco.c"
#include <math.h>      /* host-only: the oracle for a_sqrt, never in aco.c */

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { \
    printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); failures++; } } while (0)

static void test_sqrt_matches_libm(void) {
    double worst = 0.0;
    for (double x = 0.0; x < 1e7; x = x * 1.0001 + 0.37) {
        double got = a_sqrt(x), want = sqrt(x);
        double err = want > 0 ? fabs(got - want) / want : fabs(got - want);
        if (err > worst) worst = err;
    }
    CHECK(worst < 1e-12, "a_sqrt relative error exceeds 1e-12");
    CHECK(a_sqrt(0.0) == 0.0, "a_sqrt(0) must be 0");
}

static void test_rng_is_reproducible(void) {
    rng_seed(12345); uint64_t a[8]; for (int i = 0; i < 8; i++) a[i] = rng_next();
    rng_seed(12345); uint64_t b[8]; for (int i = 0; i < 8; i++) b[i] = rng_next();
    for (int i = 0; i < 8; i++) CHECK(a[i] == b[i], "same seed must replay");
    rng_seed(12346); int differs = 0;
    for (int i = 0; i < 8; i++) if (rng_next() != a[i]) differs = 1;
    CHECK(differs, "different seed must diverge");
}

static void test_rng_unit_range_and_mean(void) {
    const int N = 1000000;
    rng_seed(99);
    for (int i = 0; i < N; i++) {
        double u = rng_unit();
        if (!(u >= 0.0 && u < 1.0)) { CHECK(0, "rng_unit out of [0,1)"); break; }
    }
    rng_seed(99); double sum = 0.0;
    for (int i = 0; i < N; i++) sum += rng_unit();
    double mean = sum / N;
    CHECK(mean > 0.498 && mean < 0.502, "rng_unit mean is not ~0.5");
}

/* The tests below hardcode the tiny5 fixture's five cities. Compiled against
   any other instance they would index a 5-element array with ACO_N entries,
   which is a stack overflow, not a test. */
#if ACO_N == 5
static void test_euc2d_rounds_to_nearest(void) {
    /* tiny5: (0,0)(3,4)(10,0)(0,10)(6,8). TSPLIB EUC_2D is nint(). */
    CHECK(euc2d(0, 1) == 5,  "euc2d(0,1) should be 5");
    CHECK(euc2d(0, 4) == 10, "euc2d(0,4) should be 10");
    CHECK(euc2d(3, 3) == 0,  "distance to self is 0");
}

static void test_tour_length_closes_the_loop(void) {
    int t[5] = {0, 1, 2, 3, 4};
    int32_t want = euc2d(0,1) + euc2d(1,2) + euc2d(2,3) + euc2d(3,4) + euc2d(4,0);
    CHECK(tour_length(t) == want, "tour_length must include the return edge");
}

static void test_tour_length_is_rotation_invariant(void) {
    int a[5] = {0, 1, 2, 3, 4};
    int b[5] = {2, 3, 4, 0, 1};
    CHECK(tour_length(a) == tour_length(b), "rotation must not change length");
}

static void test_validity_rejects_duplicates_and_range(void) {
    int good[5] = {4, 0, 3, 1, 2};
    int dup[5]  = {0, 1, 1, 3, 4};
    int oob[5]  = {0, 1, 2, 3, 5};
    CHECK(tour_is_valid(good), "a permutation is valid");
    CHECK(!tour_is_valid(dup), "a repeated city is invalid");
    CHECK(!tour_is_valid(oob), "an out-of-range city is invalid");
}

#endif /* ACO_N == 5 */

static void test_candidates_are_the_nearest(void) {
    build_candidates();
    for (int i = 0; i < ACO_N; i++) {
        int32_t prev = -1;
        for (int k = 0; k < CAND_LEN; k++) {
            int j = cand[i][k];
            CHECK(j != i, "a city is not its own neighbour");
            int32_t d = euc2d(i, j);
            CHECK(d >= prev, "candidate lists must be sorted by distance");
            prev = d;
        }
        int32_t worst = euc2d(i, cand[i][CAND_LEN - 1]);
        for (int j = 0; j < ACO_N; j++) {
            if (j == i) continue;
            int included = 0;
            for (int k = 0; k < CAND_LEN; k++) if (cand[i][k] == j) included = 1;
            if (!included) CHECK(euc2d(i, j) >= worst, "excluded city was nearer");
        }
    }
}

static void test_pheromone_stays_clamped(void) {
    rng_seed(7); mmas_init();
    for (int it = 0; it < 50; it++) mmas_iterate();
    for (int i = 0; i < ACO_N; i++)
        for (int j = 0; j < ACO_N; j++) {
            CHECK(tau[i][j] >= tau_min * 0.999f, "tau fell below tau_min");
            CHECK(tau[i][j] <= tau_max * 1.001f, "tau rose above tau_max");
        }
}

static void test_every_constructed_tour_is_valid(void) {
    rng_seed(11); mmas_init();
    for (int it = 0; it < 20; it++) {
        mmas_iterate();
        CHECK(tour_is_valid(best_tour), "best tour must stay a permutation");
    }
}

static void test_best_length_never_increases(void) {
    rng_seed(3); mmas_init();
    int32_t prev = INT32_MAX;
    for (int it = 0; it < 30; it++) {
        mmas_iterate();
        CHECK(best_len <= prev, "best-so-far must be monotone non-increasing");
        prev = best_len;
    }
}

static void test_same_seed_same_result(void) {
    rng_seed(42); mmas_init();
    for (int i = 0; i < 25; i++) mmas_iterate();
    int32_t first = best_len;
    rng_seed(42); mmas_init();
    for (int i = 0; i < 25; i++) mmas_iterate();
    CHECK(best_len == first, "identical seed must give an identical result");
}


#if ACO_LOCAL_SEARCH
static void test_two_opt_only_improves_and_keeps_validity(void) {
    /* The invariant that catches a wrong reversal: 2-opt must never lengthen a
       tour and must never break the permutation. A mis-oriented reverse_seg
       does both, and silently. */
    rng_seed(5); build_candidates();
    for (int trial = 0; trial < 200; trial++) {
        int t[ACO_N];
        for (int i = 0; i < ACO_N; i++) t[i] = i;
        for (int i = ACO_N - 1; i > 0; i--) {      /* Fisher-Yates shuffle */
            int j = (int)(rng_next() % (uint64_t)(i + 1));
            int tmp = t[i]; t[i] = t[j]; t[j] = tmp;
        }
        int32_t before = tour_length(t);
        two_opt(t);
        int32_t after = tour_length(t);
        CHECK(tour_is_valid(t), "2-opt must preserve the permutation");
        CHECK(after <= before, "2-opt must never lengthen a tour");
    }
}

static void test_two_opt_is_a_fixed_point(void) {
    /* Running it twice must change nothing: the first pass leaves no
       improving move, so a second pass that finds one means the search
       terminated early or the gain arithmetic disagrees with tour_length. */
    rng_seed(6); build_candidates();
    int t[ACO_N];
    for (int i = 0; i < ACO_N; i++) t[i] = i;
    two_opt(t);
    int32_t once = tour_length(t);
    two_opt(t);
    CHECK(tour_length(t) == once, "a second 2-opt pass must find nothing");
}
#endif

int main(void) {
    test_sqrt_matches_libm();
    test_rng_is_reproducible();
    test_rng_unit_range_and_mean();
#if ACO_N == 5
    test_euc2d_rounds_to_nearest();
    test_tour_length_closes_the_loop();
    test_tour_length_is_rotation_invariant();
    test_validity_rejects_duplicates_and_range();
#endif
    test_candidates_are_the_nearest();
    test_pheromone_stays_clamped();
    test_every_constructed_tour_is_valid();
    test_best_length_never_increases();
    test_same_seed_same_result();
#if ACO_LOCAL_SEARCH
    test_two_opt_only_improves_and_keeps_validity();
    test_two_opt_is_a_fixed_point();
#endif
    if (failures) { printf("FAILED (%d)\n", failures); return 1; }
    printf("ok (n=%d, ls=%d)\n", ACO_N, ACO_LOCAL_SEARCH); return 0;
}
