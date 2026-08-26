#define main prime_hunter_main
#include "prime_hunter.c"
#undef main

// Count twin pairs below a limit using the same is_prime() the hunter uses,
// so the sieve comparison actually tests the Miller-Rabin implementation.
int main(int argc, char **argv) {
    uint64_t limit = (argc > 1) ? strtoull(argv[1], NULL, 10) : 1000000ULL;
    uint64_t twins = 0, biggest_gap = 0, last = 0;

    for (uint64_t k = 1; 6 * k + 1 < limit; k++) {
        uint64_t p = 6 * k - 1, q = 6 * k + 1;
        if (is_prime(p) && is_prime(q)) {
            twins++;
            if (last && p - last > biggest_gap) biggest_gap = p - last;
            last = p;
        }
    }
    printf("below %llu: %llu twin pairs (excluding (3,5)), largest gap %llu\n",
           (unsigned long long)limit, (unsigned long long)twins,
           (unsigned long long)biggest_gap);
    return 0;
}
