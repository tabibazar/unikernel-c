// cunningham.c — one worker in a swarm hunting Cunningham chains of the first
// kind: primes p where 2p+1 is prime, and 2(2p+1)+1 is prime, and so on.
//
//   (2, 5, 11, 23, 47) is a chain of length 5.
//   (89, 179, 359, 719, 1439, 2879) is a chain of length 6.
//
// Each term is double the previous plus one, and every term must be prime. The
// first term of a chain of length >= 2 is a Sophie Germain prime, which is the
// same structure that produces safe primes for cryptography. Length is steeply
// graded: 7 is findable in seconds, 9 takes real work, 12 needs a serious
// machine — which is exactly what makes it worth throwing a swarm at.
//
// THE SWARM
//
// There is no coordinator, no queue, and no shared database. Every worker knows
// two numbers — its own id and how many workers exist — and takes the candidates
// whose index is congruent to its id modulo the swarm size. That convention is
// the entire coordination protocol: coverage is complete and disjoint by
// construction, workers never talk to each other, and a worker dying loses only
// its own residue class rather than corrupting anyone else's work.
//
// Heartbeats are staggered by worker id so N workers don't all report at once.
//
//   build:  gcc -O2 -o cunningham cunningham.c -lcurl
//           -DWORKER_ID=1 -DWORKER_COUNT=3
//   run:    TELEGRAM_BOT_TOKEN=... TELEGRAM_CHAT_ID=... ./cunningham
//
// BareMetal has no environment, so a unikernel build takes the #defines below.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <curl/curl.h>

#define TELEGRAM_TOKEN_DEFAULT  "PUT_BOT_TOKEN_HERE"
#define TELEGRAM_CHAT_DEFAULT   "PUT_CHAT_ID_HERE"

#ifndef WORKER_ID
#define WORKER_ID          0
#endif
#ifndef WORKER_COUNT
#define WORKER_COUNT       1
#endif
#ifndef START_AT
#define START_AT           3ULL       // first odd candidate to consider
#endif
#ifndef REPORT_FLOOR
#define REPORT_FLOOR       7          // announce chains at least this long
#endif
#ifndef HEARTBEAT_SECONDS
#define HEARTBEAT_SECONDS  1800
#endif
#ifndef HEARTBEAT_EVERY
#define HEARTBEAT_EVERY    50000000ULL
#endif

#define HTTP_TIMEOUT       30L
#define CA_BUNDLE_PATH     "/etc/ssl/cacert.pem"
#define USER_AGENT         "BareMetal-Cunningham/1.0"
#define MAX_CHAIN          64

__attribute__((weak)) const unsigned char cacert_pem[1];
__attribute__((weak)) const unsigned int  cacert_pem_len;

// ---------------------------------------------------------------- primality
// Deterministic Miller-Rabin; the 12-base witness set is proven for n < 2^64.

static uint64_t mulmod(uint64_t a, uint64_t b, uint64_t m) {
    return (uint64_t)((__uint128_t)a * b % m);
}

static uint64_t powmod(uint64_t base, uint64_t exp, uint64_t m) {
    uint64_t r = 1;
    base %= m;
    while (exp) {
        if (exp & 1) r = mulmod(r, base, m);
        base = mulmod(base, base, m);
        exp >>= 1;
    }
    return r;
}

static int is_prime(uint64_t n) {
    if (n < 2) return 0;
    for (uint64_t p = 2; p < 38; p++) {
        if (p * p > n) return 1;
        if (n % p == 0) return n == p;
    }
    uint64_t d = n - 1;
    int r = 0;
    while ((d & 1) == 0) { d >>= 1; r++; }

    static const uint64_t witnesses[] = {2,3,5,7,11,13,17,19,23,29,31,37};
    for (size_t i = 0; i < sizeof(witnesses)/sizeof(witnesses[0]); i++) {
        uint64_t a = witnesses[i] % n;
        if (a == 0) continue;
        uint64_t x = powmod(a, d, n);
        if (x == 1 || x == n - 1) continue;
        int composite = 1;
        for (int j = 1; j < r; j++) {
            x = mulmod(x, x, n);
            if (x == n - 1) { composite = 0; break; }
        }
        if (composite) return 0;
    }
    return 1;
}

// How far the chain starting at p runs. Writes the terms into `chain`.
// Stops at overflow rather than wrapping — a wrapped term would silently
// restart the search in a different part of the number line.
static int chain_length(uint64_t p, uint64_t *chain) {
    int len = 0;
    uint64_t q = p;
    while (len < MAX_CHAIN && is_prime(q)) {
        chain[len++] = q;
        if (q > (UINT64_MAX - 1) / 2) break;
        q = 2 * q + 1;
    }
    return len;
}

// ---------------------------------------------------------------- telegram

static const char *g_token;
static const char *g_chat;
static char        g_url[512];
static long        g_posts_ok, g_posts_failed;

static size_t discard_cb(void *p, size_t size, size_t nmemb, void *u) {
    (void)p; (void)u;
    return size * nmemb;
}

static void set_ca_bundle(CURL *h) {
    FILE *f = fopen(CA_BUNDLE_PATH, "r");
    if (f) {
        fclose(f);
        curl_easy_setopt(h, CURLOPT_CAINFO, CA_BUNDLE_PATH);
    } else if (cacert_pem_len > 0) {
        struct curl_blob blob = { (void *)cacert_pem, cacert_pem_len, CURL_BLOB_NOCOPY };
        curl_easy_setopt(h, CURLOPT_CAINFO_BLOB, &blob);
    }
}

static void json_escape(const char *in, char *out, size_t out_sz) {
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && o + 7 < out_sz; p++) {
        switch (*p) {
            case '"':  out[o++] = '\\'; out[o++] = '"';  break;
            case '\\': out[o++] = '\\'; out[o++] = '\\'; break;
            case '\n': out[o++] = '\\'; out[o++] = 'n';  break;
            case '\r': out[o++] = '\\'; out[o++] = 'r';  break;
            case '\t': out[o++] = '\\'; out[o++] = 't';  break;
            default:
                if (*p < 0x20) o += (size_t)snprintf(out + o, out_sz - o, "\\u%04x", *p);
                else           out[o++] = (char)*p;
        }
    }
    out[o] = '\0';
}

static int telegram_send(const char *text) {
    CURL *h = curl_easy_init();
    if (!h) return 0;

    static char escaped[4096];
    static char payload[4608];
    json_escape(text, escaped, sizeof(escaped));
    int n = snprintf(payload, sizeof(payload),
                     "{\"chat_id\":\"%s\",\"text\":\"%s\"}", g_chat, escaped);
    if (n < 0 || (size_t)n >= sizeof(payload)) { curl_easy_cleanup(h); return 0; }

    struct curl_slist *hdrs = curl_slist_append(NULL, "Content-Type: application/json");
    curl_easy_setopt(h, CURLOPT_URL, g_url);
    curl_easy_setopt(h, CURLOPT_POST, 1L);
    curl_easy_setopt(h, CURLOPT_POSTFIELDS, payload);
    curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, (long)n);
    curl_easy_setopt(h, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, discard_cb);
    curl_easy_setopt(h, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(h, CURLOPT_TIMEOUT, HTTP_TIMEOUT);
    curl_easy_setopt(h, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(h, CURLOPT_SSL_VERIFYHOST, 2L);
    set_ca_bundle(h);

    CURLcode res = curl_easy_perform(h);
    long status = 0;
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(h);

    if (res != CURLE_OK) {
        fprintf(stderr, "[!] telegram: %s\n", curl_easy_strerror(res));
        g_posts_failed++;
        return 0;
    }
    if (status >= 400) {
        fprintf(stderr, "[!] telegram: HTTP %ld\n", status);
        g_posts_failed++;
        return 0;
    }
    g_posts_ok++;
    printf("[+] telegram ok (%ld sent, %ld failed)\n", g_posts_ok, g_posts_failed);
    return 1;
}

// ---------------------------------------------------------------- formatting

static const char *commas(uint64_t v, char *buf, size_t sz) {
    char digits[24];
    int n = snprintf(digits, sizeof(digits), "%llu", (unsigned long long)v);
    size_t o = 0;
    for (int i = 0; i < n && o + 2 < sz; i++) {
        if (i > 0 && (n - i) % 3 == 0) buf[o++] = ',';
        buf[o++] = digits[i];
    }
    buf[o] = '\0';
    return buf;
}

static const char *duration(long secs, char *buf, size_t sz) {
    long h = secs / 3600, m = (secs % 3600) / 60, s = secs % 60;
    if (h)      snprintf(buf, sz, "%ldh%02ldm", h, m);
    else if (m) snprintf(buf, sz, "%ldm%02lds", m, s);
    else        snprintf(buf, sz, "%lds", s);
    return buf;
}

// ---------------------------------------------------------------- the hunt

int main(int argc, char **argv) {
    const char *tok = getenv("TELEGRAM_BOT_TOKEN");
    const char *cht = getenv("TELEGRAM_CHAT_ID");
    g_token = (tok && *tok) ? tok : TELEGRAM_TOKEN_DEFAULT;
    g_chat  = (cht && *cht) ? cht : TELEGRAM_CHAT_DEFAULT;

    int quiet = 0;                 // --quiet: search without posting anything
    long run_for = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--quiet") == 0) quiet = 1;
        if (strcmp(argv[i], "--seconds") == 0 && i + 1 < argc)
            run_for = strtol(argv[i + 1], NULL, 10);
    }

    if (!quiet && !strchr(g_token, ':')) {
        fprintf(stderr, "[!] no usable bot token (got %.12s...): set TELEGRAM_BOT_TOKEN, "
                        "or bake one in for BareMetal.\n", g_token);
        return 1;
    }
    snprintf(g_url, sizeof(g_url), "https://api.telegram.org/bot%s/sendMessage", g_token);

    if (!quiet) curl_global_init(CURL_GLOBAL_DEFAULT);

    uint64_t chain[MAX_CHAIN];
    uint64_t checked = 0, found[MAX_CHAIN] = {0}, hb_mark = 0;
    int      best = 0;

    time_t started = time(NULL);
    // Stagger the first heartbeat by worker id so the swarm doesn't all report
    // in the same second.
    time_t last_hb = started - (time_t)((HEARTBEAT_SECONDS / WORKER_COUNT) * WORKER_ID);

    char b1[32], b2[32], b3[32], msg[1400];

    printf("[+] worker %d/%d hunting Cunningham chains, floor %d\n",
           WORKER_ID, WORKER_COUNT, REPORT_FLOOR);
    if (!quiet) {
        snprintf(msg, sizeof(msg),
                 "\xF0\x9F\x9A\x80 worker %d of %d online\n"
                 "hunting Cunningham chains (p, 2p+1, 4p+3, ...)\n"
                 "taking candidates \xE2\x89\xA1 %d (mod %d) \xC2\xB7 announcing length \xE2\x89\xA5 %d",
                 WORKER_ID, WORKER_COUNT, WORKER_ID, WORKER_COUNT, REPORT_FLOOR);
        telegram_send(msg);
    }

    // Candidate j is the odd number START_AT + 2j. This worker takes the
    // candidates where j ≡ WORKER_ID (mod WORKER_COUNT) — its residue class.
    for (uint64_t j = (uint64_t)WORKER_ID; ; j += (uint64_t)WORKER_COUNT) {
        uint64_t p = START_AT + 2 * j;
        checked++;

        int len = chain_length(p, chain);
        if (len > 0 && len < MAX_CHAIN) found[len]++;

        if (len >= REPORT_FLOOR) {
            // Print the whole chain: the point of a find is seeing it.
            char terms[900];
            size_t o = 0;
            for (int i = 0; i < len && o + 32 < sizeof(terms); i++) {
                o += (size_t)snprintf(terms + o, sizeof(terms) - o, "%s%llu",
                                      i ? " \xE2\x86\x92 " : "", (unsigned long long)chain[i]);
            }
            printf("[*] worker %d: chain of %d at %llu\n",
                   WORKER_ID, len, (unsigned long long)p);
            if (!quiet) {
                snprintf(msg, sizeof(msg),
                         "\xE2\x9C\xA8 worker %d \xE2\x80\x94 chain of %d%s\n%s\n"
                         "searched to %s \xC2\xB7 uptime %s",
                         WORKER_ID, len, len > best ? " (personal best)" : "", terms,
                         commas(p, b1, sizeof(b1)),
                         duration((long)(time(NULL) - started), b2, sizeof(b2)));
                telegram_send(msg);
            }
        }
        if (len > best) best = len;

        time_t now = time(NULL);
        if ((long)(now - last_hb) >= HEARTBEAT_SECONDS ||
            checked - hb_mark >= HEARTBEAT_EVERY) {
            long total = (long)(now - started);
            uint64_t rate = total > 0 ? checked / (uint64_t)total : checked;

            // A histogram of what this worker's slice of the number line holds.
            char hist[400];
            size_t o = 0;
            for (int L = 4; L < MAX_CHAIN && o + 24 < sizeof(hist); L++)
                if (found[L])
                    o += (size_t)snprintf(hist + o, sizeof(hist) - o, "%slen %d: %llu",
                                          o ? "\n" : "", L, (unsigned long long)found[L]);
            if (!o) snprintf(hist, sizeof(hist), "(nothing of length 4 or more yet)");

            printf("[*] worker %d heartbeat: searched to %llu, best %d\n",
                   WORKER_ID, (unsigned long long)p, best);
            if (!quiet) {
                snprintf(msg, sizeof(msg),
                         "\xF0\x9F\x94\x8D worker %d of %d\n"
                         "searched to: %s\n"
                         "candidates:  %s (%s/sec)\n"
                         "longest:     %d\n%s\n"
                         "uptime %s",
                         WORKER_ID, WORKER_COUNT,
                         commas(p, b1, sizeof(b1)),
                         commas(checked, b2, sizeof(b2)),
                         commas(rate, b3, sizeof(b3)),
                         best, hist,
                         duration(total, (char[16]){0}, 16));
                telegram_send(msg);
            }
            last_hb = now;
            hb_mark = checked;
        }

        if (run_for && (long)(now - started) >= run_for) {
            printf("[+] worker %d stopped: %llu candidates, best %d, %ld posts ok / %ld failed\n",
                   WORKER_ID, (unsigned long long)checked, best, g_posts_ok, g_posts_failed);
            for (int L = 4; L < MAX_CHAIN; L++)
                if (found[L]) printf("    length %2d: %llu\n", L, (unsigned long long)found[L]);
            break;
        }
    }

    if (!quiet) curl_global_cleanup();
    return 0;
}
