// research.c — a stateless research agent.
//
// Ask it a question. It searches, decides what it still needs, searches again,
// reads the pages that look worth reading, and answers when it can. Then it
// exits. Nothing is carried forward: no history file, no cache, no state
// between questions. The next question starts from nothing.
//
//   ./research "does Firecracker require a PVH note in an ELF kernel?"
//
// WHY AN AGENT AND NOT A SCRIPT
//
// The second query exists because of what the first one returned, and the
// decision to stop is a judgement about whether the evidence settles the
// question. Neither is expressible as a fixed sequence of steps, which is the
// only honest reason to put a model in a loop.
//
// WHAT IS ENFORCED IN CODE RATHER THAN ASKED FOR IN THE PROMPT
//
//   - The answer must arrive through submit_answer, with sources attached.
//     Free text does not end the loop.
//   - Every cited URL is checked against the URLs this run actually retrieved.
//     Citing a plausible-looking URL that was never seen is the characteristic
//     failure of research agents, so fabricated citations are stripped here and
//     the answer is marked as having lost them.
//   - Exhausting the step budget is an outcome, not an error: it answers with
//     what it has and names what it could not establish.
//
// Search and page reading are Firecrawl (firecrawl.dev), which returns pages as
// markdown, so this program never parses HTML.
//
//   build: gcc -O2 -o research research.c -lcurl -lcjson
//   keys:  ~/.firecrawl_key, and GEMINI_API_KEY in the environment

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <curl/curl.h>
#include "cjson/cJSON.h"

#define LLM_URL_DEFAULT   "https://generativelanguage.googleapis.com/v1beta/openai/chat/completions"
#define LLM_MODEL_DEFAULT "gemini-2.5-flash"

#define SEARCH_URL   "https://api.firecrawl.dev/v2/search"
#define SCRAPE_URL   "https://api.firecrawl.dev/v2/scrape"

#ifndef MAX_STEPS
#define MAX_STEPS      12        // search/read rounds before it must answer
#endif
#define SEARCH_RESULTS 5         // results kept per search
#define DESC_CAP       320       // per-result description, bytes
#define PAGE_CAP       6000      // per-page markdown handed to the model, bytes
#define MAX_SEEN_URLS  128       // URL registry for citation checking
#define URL_LEN        512

static const char *g_llm_url, *g_llm_model, *g_llm_key, *g_fc_key;

// Cost accounting. Nothing here changes behaviour; it exists so a run can be
// priced from what it actually consumed rather than from an estimate.
static long g_llm_calls, g_tok_in, g_tok_out, g_search_calls, g_fetch_calls;

// ---------------------------------------------------------------- http

struct buf { char *p; size_t n; };

static size_t collect(void *contents, size_t size, size_t nmemb, void *userp) {
    struct buf *b = userp;
    size_t total = size * nmemb;
    char *q = realloc(b->p, b->n + total + 1);
    if (!q) return 0;
    b->p = q;
    memcpy(b->p + b->n, contents, total);
    b->n += total;
    b->p[b->n] = '\0';
    return total;
}

// Retries a request that failed in a way retrying can fix. Rate limiting is the
// common case here: several of these agents running at once will trip a search
// API's limits, and a tool that treats one 429 as "this fact does not exist"
// turns my own concurrency into a permanent hole in the results.
static char *post_json_once(const char *url, const char *body, const char *bearer, long *status_out);

static char *post_json(const char *url, const char *body, const char *bearer, long *status_out) {
    int backoff = 2;
    for (int attempt = 1; attempt <= 4; attempt++) {
        long status = 0;
        char *r = post_json_once(url, body, bearer, &status);
        if (status_out) *status_out = status;
        if (r && status < 400) return r;
        int retryable = (status == 429 || status >= 500 || status == 0);
        free(r);
        if (!retryable || attempt == 4) return NULL;
        fprintf(stderr, "    [~] %s returned %ld, retrying in %ds\n", url, status, backoff);
        sleep((unsigned)backoff);
        backoff *= 2;
    }
    return NULL;
}

// Caller frees. status_out receives the HTTP status.
static char *post_json_once(const char *url, const char *body, const char *bearer, long *status_out) {
    CURL *h = curl_easy_init();
    if (!h) return NULL;

    struct buf b = { NULL, 0 };
    struct curl_slist *hdrs = curl_slist_append(NULL, "Content-Type: application/json");
    char auth[512];
    if (bearer && *bearer) {
        snprintf(auth, sizeof(auth), "Authorization: Bearer %s", bearer);
        hdrs = curl_slist_append(hdrs, auth);
    }

    curl_easy_setopt(h, CURLOPT_URL, url);
    curl_easy_setopt(h, CURLOPT_POST, 1L);
    curl_easy_setopt(h, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(h, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, collect);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &b);
    curl_easy_setopt(h, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(h);
    long status = 0;
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(h);
    if (status_out) *status_out = status;

    if (res != CURLE_OK) {
        fprintf(stderr, "    [!] %s: %s\n", url, curl_easy_strerror(res));
        free(b.p);
        return NULL;
    }
    return b.p;
}

// ---------------------------------------------------------------- url registry
//
// Every URL this run actually retrieved. A citation that is not in here was not
// seen by anyone, whatever the model believes.

static char g_seen[MAX_SEEN_URLS][URL_LEN];
static int  g_seen_count;

static void remember_url(const char *u) {
    if (!u || g_seen_count >= MAX_SEEN_URLS) return;
    for (int i = 0; i < g_seen_count; i++)
        if (strcmp(g_seen[i], u) == 0) return;
    snprintf(g_seen[g_seen_count++], URL_LEN, "%s", u);
}

static int url_was_seen(const char *u) {
    if (!u) return 0;
    for (int i = 0; i < g_seen_count; i++)
        if (strcmp(g_seen[i], u) == 0) return 1;
    return 0;
}

// Host of "https://host/path", written into out. Empty on anything unexpected.
static void url_host(const char *u, char *out, size_t n) {
    out[0] = '\0';
    const char *p = strstr(u, "://");
    if (!p) return;
    p += 3;
    const char *slash = strchr(p, '/');
    size_t len = slash ? (size_t)(slash - p) : strlen(p);
    if (len >= n) len = n - 1;
    memcpy(out, p, len);
    out[len] = '\0';
}

// Reading is allowed for a url already seen, or for any url on a host that a
// search result came from.
//
// The first version required an exact match, which sounded safe and was too
// strict to be useful: a repository landing page rarely contains the fact, and
// the page that does — a file inside the repo — never appears as its own search
// result. The agent could see where the answer lived and not be allowed to go
// there. Restricting to hosts that search actually surfaced keeps the property
// that matters (it cannot wander to a domain it invented) without forbidding
// navigation within a site it legitimately found.
static int url_allowed(const char *u) {
    if (url_was_seen(u)) return 1;
    char want[256], have[256];
    url_host(u, want, sizeof(want));
    if (!*want) return 0;
    for (int i = 0; i < g_seen_count; i++) {
        url_host(g_seen[i], have, sizeof(have));
        if (*have && strcmp(want, have) == 0) return 1;
    }
    return 0;
}

// Repeated identical calls are a loop, not progress. Saying so is cheaper than
// letting the agent spend its budget rediscovering the same page.
#define MAX_HISTORY 24
static char g_calls[MAX_HISTORY][320];
static int  g_call_count;

static int already_called(const char *name, const char *args) {
    char key[320];
    snprintf(key, sizeof(key), "%s|%s", name, args ? args : "");
    for (int i = 0; i < g_call_count; i++)
        if (strcmp(g_calls[i], key) == 0) return 1;
    if (g_call_count < MAX_HISTORY) snprintf(g_calls[g_call_count++], 320, "%s", key);
    return 0;
}

// ---------------------------------------------------------------- tools

static const char *TOOLS_JSON =
"["
"  {\"type\":\"function\",\"function\":{"
"     \"name\":\"web_search\","
"     \"description\":\"Search the web. Returns the top results as title, url and a short description. Use a fresh, more specific query when earlier results were not decisive.\","
"     \"parameters\":{\"type\":\"object\",\"properties\":{"
"        \"query\":{\"type\":\"string\",\"description\":\"The search query.\"}},"
"        \"required\":[\"query\"]}}},"
"  {\"type\":\"function\",\"function\":{"
"     \"name\":\"read_page\","
"     \"description\":\"Fetch a page and read its main content as text. Use when a search result looks like it settles the question but the description is not enough. The url must come from a search result you have already seen.\","
"     \"parameters\":{\"type\":\"object\",\"properties\":{"
"        \"url\":{\"type\":\"string\",\"description\":\"The page to read.\"}},"
"        \"required\":[\"url\"]}}},"
"  {\"type\":\"function\",\"function\":{"
"     \"name\":\"submit_answer\","
"     \"description\":\"Give the final answer. This is the only way to finish. Cite only urls you actually retrieved in this run.\","
"     \"parameters\":{\"type\":\"object\",\"properties\":{"
"        \"answer\":{\"type\":\"string\",\"description\":\"The answer, in a few sentences. Say what is established and how you know.\"},"
"        \"sources\":{\"type\":\"string\",\"description\":\"Urls that support the answer, separated by spaces.\"},"
"        \"confidence\":{\"type\":\"string\",\"enum\":[\"high\",\"medium\",\"low\"]},"
"        \"unresolved\":{\"type\":\"string\",\"description\":\"Anything you could not establish, or empty if nothing.\"}},"
"        \"required\":[\"answer\",\"sources\",\"confidence\"]}}}"
"]";

static char *g_result;             // current tool result, heap, freed by caller

static void set_result(const char *s) {
    free(g_result);
    g_result = strdup(s);
}

static void set_resultf(const char *fmt, const char *a) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), fmt, a);
    set_result(tmp);
}

// Answer, captured when submit_answer fires.
static char *g_answer, *g_unresolved, *g_confidence;
static char  g_sources[16][URL_LEN];
static int   g_source_count, g_sources_stripped, g_answered;

static void tool_web_search(const cJSON *args) {
    cJSON *q = cJSON_GetObjectItemCaseSensitive(args, "query");
    if (!cJSON_IsString(q)) { set_result("{\"error\":\"missing query\"}"); return; }

    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "query", q->valuestring);
    cJSON_AddNumberToObject(req, "limit", SEARCH_RESULTS);
    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!body) { set_result("{\"error\":\"could not build request\"}"); return; }

    g_search_calls++;
    long status = 0;
    char *resp = post_json(SEARCH_URL, body, g_fc_key, &status);
    free(body);
    if (!resp || status >= 400) {
        set_resultf("{\"error\":\"search failed\",\"detail\":\"%s\"}",
                    resp ? "non-2xx from the search API" : "no response");
        free(resp);
        return;
    }

    cJSON *json = cJSON_Parse(resp);
    free(resp);
    if (!json) { set_result("{\"error\":\"search returned unparseable JSON\"}"); return; }

    // v2 nests results under data.web; v1 returns a flat data array. Accept both.
    cJSON *data = cJSON_GetObjectItemCaseSensitive(json, "data");
    cJSON *web  = data ? cJSON_GetObjectItemCaseSensitive(data, "web") : NULL;
    cJSON *list = cJSON_IsArray(web) ? web : (cJSON_IsArray(data) ? data : NULL);

    cJSON *out = cJSON_CreateArray();
    int kept = 0;
    if (list) {
        cJSON *r = NULL;
        cJSON_ArrayForEach(r, list) {
            if (kept >= SEARCH_RESULTS) break;
            cJSON *u = cJSON_GetObjectItemCaseSensitive(r, "url");
            cJSON *t = cJSON_GetObjectItemCaseSensitive(r, "title");
            cJSON *d = cJSON_GetObjectItemCaseSensitive(r, "description");
            if (!cJSON_IsString(u)) continue;

            remember_url(u->valuestring);

            cJSON *slim = cJSON_CreateObject();
            cJSON_AddStringToObject(slim, "url", u->valuestring);
            if (cJSON_IsString(t)) cJSON_AddStringToObject(slim, "title", t->valuestring);
            if (cJSON_IsString(d)) {
                char desc[DESC_CAP + 1];
                snprintf(desc, sizeof(desc), "%s", d->valuestring);
                cJSON_AddStringToObject(slim, "description", desc);
            }
            cJSON_AddItemToArray(out, slim);
            kept++;
        }
    }
    cJSON_Delete(json);

    char *printed = cJSON_PrintUnformatted(out);
    cJSON_Delete(out);
    if (!printed) { set_result("{\"error\":\"could not format results\"}"); return; }
    set_result(printed);
    free(printed);
}

static void tool_read_page(const cJSON *args) {
    cJSON *u = cJSON_GetObjectItemCaseSensitive(args, "url");
    if (!cJSON_IsString(u)) { set_result("{\"error\":\"missing url\"}"); return; }

    if (!url_allowed(u->valuestring)) {
        set_result("{\"error\":\"that url is on a site no search result came from this run; "
                   "search for it first\"}");
        return;
    }

    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "url", u->valuestring);
    cJSON *fmts = cJSON_CreateArray();
    cJSON_AddItemToArray(fmts, cJSON_CreateString("markdown"));
    cJSON_AddItemToObject(req, "formats", fmts);
    cJSON_AddBoolToObject(req, "onlyMainContent", 1);
    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!body) { set_result("{\"error\":\"could not build request\"}"); return; }

    g_fetch_calls++;
    long status = 0;
    char *resp = post_json(SCRAPE_URL, body, g_fc_key, &status);
    free(body);
    if (!resp || status >= 400) {
        set_result("{\"error\":\"could not fetch that page\"}");
        free(resp);
        return;
    }

    cJSON *json = cJSON_Parse(resp);
    free(resp);
    if (!json) { set_result("{\"error\":\"page fetch returned unparseable JSON\"}"); return; }

    cJSON *data = cJSON_GetObjectItemCaseSensitive(json, "data");
    cJSON *md   = data ? cJSON_GetObjectItemCaseSensitive(data, "markdown") : NULL;

    if (!cJSON_IsString(md)) {
        cJSON_Delete(json);
        set_result("{\"error\":\"no readable content on that page\"}");
        return;
    }

    // A page it actually read is citable.
    remember_url(u->valuestring);

    // Truncate hard. Whole pages would dominate the conversation, and the
    // conversation is re-sent on every subsequent turn.
    size_t len = strlen(md->valuestring);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "url", u->valuestring);
    char *clipped = malloc(PAGE_CAP + 1);
    if (clipped) {
        snprintf(clipped, PAGE_CAP + 1, "%s", md->valuestring);
        cJSON_AddStringToObject(o, "content", clipped);
        free(clipped);
    }
    cJSON_AddBoolToObject(o, "truncated", len > PAGE_CAP);
    cJSON_Delete(json);

    char *printed = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (!printed) { set_result("{\"error\":\"could not format page\"}"); return; }
    set_result(printed);
    free(printed);
}

// The only way to finish. Citations are verified here, not trusted.
static void tool_submit_answer(const cJSON *args) {
    cJSON *a = cJSON_GetObjectItemCaseSensitive(args, "answer");
    cJSON *s = cJSON_GetObjectItemCaseSensitive(args, "sources");
    cJSON *c = cJSON_GetObjectItemCaseSensitive(args, "confidence");
    cJSON *u = cJSON_GetObjectItemCaseSensitive(args, "unresolved");
    if (!cJSON_IsString(a)) { set_result("{\"error\":\"answer is required\"}"); return; }

    free(g_answer); free(g_confidence); free(g_unresolved);
    g_answer     = strdup(a->valuestring);
    g_confidence = strdup(cJSON_IsString(c) ? c->valuestring : "unstated");
    g_unresolved = strdup(cJSON_IsString(u) ? u->valuestring : "");

    // Accepts either shape: a space-separated string (what the schema asks for
    // now) or an array (what it used to ask for, and what a model may still
    // produce). Being lenient in what is accepted costs a few lines and removes
    // a whole class of failure.
    g_source_count = g_sources_stripped = 0;
    if (cJSON_IsString(s)) {
        char buf[4096];
        snprintf(buf, sizeof(buf), "%s", s->valuestring);
        char *save = NULL;
        for (char *t = strtok_r(buf, " ,\n\t", &save); t; t = strtok_r(NULL, " ,\n\t", &save)) {
            if (strncmp(t, "http", 4) != 0) continue;
            if (!url_was_seen(t)) { g_sources_stripped++; continue; }
            if (g_source_count < 16) snprintf(g_sources[g_source_count++], URL_LEN, "%s", t);
        }
    } else if (cJSON_IsArray(s)) {
        cJSON *it = NULL;
        cJSON_ArrayForEach(it, s) {
            if (!cJSON_IsString(it)) continue;
            if (!url_was_seen(it->valuestring)) { g_sources_stripped++; continue; }
            if (g_source_count < 16)
                snprintf(g_sources[g_source_count++], URL_LEN, "%s", it->valuestring);
        }
    }

    g_answered = 1;
    if (g_sources_stripped)
        set_result("{\"ok\":true,\"note\":\"answer recorded, but some cited urls were not "
                   "retrieved in this run and were removed\"}");
    else
        set_result("{\"ok\":true}");
}

static void call_tool(const char *name, const char *args_json) {
    cJSON *args = args_json ? cJSON_Parse(args_json) : NULL;
    if      (strcmp(name, "web_search") == 0)    { if (args) tool_web_search(args); else set_result("{\"error\":\"bad arguments\"}"); }
    else if (strcmp(name, "read_page") == 0)     { if (args) tool_read_page(args);  else set_result("{\"error\":\"bad arguments\"}"); }
    else if (strcmp(name, "submit_answer") == 0) { if (args) tool_submit_answer(args); else set_result("{\"error\":\"bad arguments\"}"); }
    else set_resultf("{\"error\":\"no such tool: %s\"}", name);
    if (args) cJSON_Delete(args);
}

// ---------------------------------------------------------------- llm

static cJSON *llm_attempt(const cJSON *messages, int *retryable);

static cJSON *llm_turn(const cJSON *messages) {
    int backoff = 3;
    for (int attempt = 1; attempt <= 4; attempt++) {
        int retryable = 0;
        cJSON *m = llm_attempt(messages, &retryable);
        if (m) return m;
        if (!retryable || attempt == 4) return NULL;
        fprintf(stderr, "    [~] model busy, retrying in %ds\n", backoff);
        sleep((unsigned)backoff);
        backoff *= 2;
    }
    return NULL;
}

static cJSON *llm_attempt(const cJSON *messages, int *retryable) {
    *retryable = 0;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", g_llm_model);
    cJSON_AddItemToObject(root, "messages", cJSON_Duplicate(messages, 1));
    cJSON_AddItemToObject(root, "tools", cJSON_Parse(TOOLS_JSON));
    cJSON_AddStringToObject(root, "tool_choice", "auto");
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) return NULL;

    long status = 0;
    char *resp = post_json(g_llm_url, payload, g_llm_key, &status);
    free(payload);

    if (!resp) { *retryable = 1; return NULL; }
    if (status >= 400) {
        fprintf(stderr, "    [!] model HTTP %ld: %.160s\n", status, resp);
        *retryable = (status == 429 || status >= 500);
        free(resp);
        return NULL;
    }

    cJSON *json = cJSON_Parse(resp);
    free(resp);
    if (!json) { fprintf(stderr, "    [!] model returned unparseable JSON\n"); return NULL; }

    // usage is reported per response by the OpenAI-compatible endpoint
    cJSON *usage = cJSON_GetObjectItemCaseSensitive(json, "usage");
    if (usage) {
        cJSON *pi = cJSON_GetObjectItemCaseSensitive(usage, "prompt_tokens");
        cJSON *co = cJSON_GetObjectItemCaseSensitive(usage, "completion_tokens");
        if (cJSON_IsNumber(pi)) g_tok_in  += (long)pi->valuedouble;
        if (cJSON_IsNumber(co)) g_tok_out += (long)co->valuedouble;
    }
    g_llm_calls++;

    cJSON *choices = cJSON_GetObjectItemCaseSensitive(json, "choices");
    cJSON *msg = NULL;
    if (cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
        cJSON *choice = cJSON_GetArrayItem(choices, 0);
        cJSON *m = cJSON_GetObjectItemCaseSensitive(choice, "message");
        if (m) msg = cJSON_Duplicate(m, 1);
        if (m) {
            cJSON *c  = cJSON_GetObjectItemCaseSensitive(m, "content");
            cJSON *tc = cJSON_GetObjectItemCaseSensitive(m, "tool_calls");
            if ((!cJSON_IsString(c) || !*c->valuestring) &&
                (!cJSON_IsArray(tc) || cJSON_GetArraySize(tc) == 0)) {
                cJSON *fr = cJSON_GetObjectItemCaseSensitive(choice, "finish_reason");
                fprintf(stderr, "    [!] empty turn, finish_reason=%s\n",
                        cJSON_IsString(fr) ? fr->valuestring : "(absent)");
            }
        }
    }
    cJSON_Delete(json);
    return msg;
}

// ---------------------------------------------------------------- loop

#define SYSTEM_PROMPT \
    "You research a question and then answer it, using web search and by reading pages.\n\n" \
    "Work in steps. Search, look at what came back, and ask yourself what is still missing. " \
    "Choose your next query because of what the last one told you, not from a list you " \
    "planned in advance. When a result looks like it settles the question but the snippet " \
    "is too thin, read the page.\n\n" \
    "A fact often lives on a specific page rather than on the front door of a site. For a " \
    "question about a codebase, the answer is usually inside a particular file — a build " \
    "script, a manifest, a config — not in the repository's README. If you can see where " \
    "such a file would be, read that file directly rather than searching again for a page " \
    "that summarises it. You may read any page on a site that a search result came from.\n\n" \
    "Finish by calling submit_answer. That is the only way to end; prose alone will not do " \
    "it. Cite only urls you actually retrieved in this run — a citation you did not read " \
    "will be detected and removed, and the answer will be marked as having lost it.\n\n" \
    "Say what the evidence supports and no more. If sources disagree, say so and say which " \
    "you find more credible and why. If you cannot establish something, put it in " \
    "'unresolved' rather than smoothing it over. An honest partial answer is worth more " \
    "than a confident invented one."

static void add_message(cJSON *msgs, const char *role, const char *content) {
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "role", role);
    cJSON_AddStringToObject(m, "content", content);
    cJSON_AddItemToArray(msgs, m);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);

    if (argc < 2) {
        fprintf(stderr, "usage: %s \"a question\"\n", argv[0]);
        return 64;
    }
    const char *question = argv[1];

    const char *k = getenv("GEMINI_API_KEY");
    g_llm_key   = (k && *k) ? k : NULL;
    g_llm_url   = getenv("LLM_BASE_URL")  ? getenv("LLM_BASE_URL")  : LLM_URL_DEFAULT;
    g_llm_model = getenv("LLM_MODEL")     ? getenv("LLM_MODEL")     : LLM_MODEL_DEFAULT;
    g_fc_key    = getenv("FIRECRAWL_API_KEY");

    if (!g_llm_key)  { fprintf(stderr, "[!] set GEMINI_API_KEY\n"); return 1; }
    if (!g_fc_key)   { fprintf(stderr, "[!] set FIRECRAWL_API_KEY\n"); return 1; }

    curl_global_init(CURL_GLOBAL_ALL);

    cJSON *messages = cJSON_CreateArray();
    add_message(messages, "system", SYSTEM_PROMPT);
    add_message(messages, "user", question);

    int steps_used = 0;
    for (int step = 1; step <= MAX_STEPS && !g_answered; step++) {
        steps_used = step;
        fprintf(stderr, "[*] step %d/%d\n", step, MAX_STEPS);

        cJSON *assistant = llm_turn(messages);
        if (!assistant) { fprintf(stderr, "[!] no answer from the model\n"); break; }

        cJSON_AddItemToArray(messages, cJSON_Duplicate(assistant, 1));

        cJSON *calls = cJSON_GetObjectItemCaseSensitive(assistant, "tool_calls");
        if (!cJSON_IsArray(calls) || cJSON_GetArraySize(calls) == 0) {
            // Prose does not end the loop. Say so and let it try again.
            add_message(messages, "user",
                "Finish by calling submit_answer with your sources. Prose alone does not "
                "record an answer.");
            cJSON_Delete(assistant);
            continue;
        }

        cJSON *tc = NULL;
        cJSON_ArrayForEach(tc, calls) {
            cJSON *id = cJSON_GetObjectItemCaseSensitive(tc, "id");
            cJSON *fn = cJSON_GetObjectItemCaseSensitive(tc, "function");
            cJSON *nm = fn ? cJSON_GetObjectItemCaseSensitive(fn, "name") : NULL;
            cJSON *ar = fn ? cJSON_GetObjectItemCaseSensitive(fn, "arguments") : NULL;
            if (!cJSON_IsString(nm)) continue;

            const char *raw_args = cJSON_IsString(ar) ? ar->valuestring : NULL;
            fprintf(stderr, "    -> %s %.120s\n", nm->valuestring, raw_args ? raw_args : "");

            if (strcmp(nm->valuestring, "submit_answer") != 0 &&
                already_called(nm->valuestring, raw_args)) {
                set_result("{\"error\":\"you already made this exact call and it returned what "
                           "it returned. Repeating it will not help — either try a different "
                           "query or source, or answer with what you have.\"}");
            } else {
                call_tool(nm->valuestring, raw_args);
            }

            cJSON *tm = cJSON_CreateObject();
            cJSON_AddStringToObject(tm, "role", "tool");
            if (cJSON_IsString(id)) cJSON_AddStringToObject(tm, "tool_call_id", id->valuestring);
            cJSON_AddStringToObject(tm, "name", nm->valuestring);
            cJSON_AddStringToObject(tm, "content", g_result ? g_result : "{}");
            cJSON_AddItemToArray(messages, tm);
        }
        cJSON_Delete(assistant);

        // One step left: ask for what it has rather than letting the budget
        // expire silently. A partial answer with its gaps named is useful; a
        // dead end is not.
        if (step == MAX_STEPS - 1 && !g_answered)
            add_message(messages, "user",
                "This is your last step. Call submit_answer now with whatever you have "
                "established, and put everything you could not confirm in 'unresolved'. "
                "A partial answer that is honest about its gaps is what is wanted here.");
    }

    cJSON_Delete(messages);
    curl_global_cleanup();

    // ------------------------------------------------------------ report
    printf("\n");
    if (!g_answered) {
        printf("NO ANSWER after %d steps.\n", steps_used);
        printf("It retrieved %d source%s but never reached something it was willing to state.\n",
               g_seen_count, g_seen_count == 1 ? "" : "s");
        printf("COST llm_calls=%ld tokens_in=%ld tokens_out=%ld searches=%ld fetches=%ld steps=%d\n",
               g_llm_calls, g_tok_in, g_tok_out, g_search_calls, g_fetch_calls, steps_used);
        free(g_result);
        return 2;
    }

    printf("%s\n\n", g_answer);
    printf("confidence: %s\n", g_confidence);
    if (g_unresolved && *g_unresolved) printf("unresolved: %s\n", g_unresolved);
    printf("steps: %d of %d, urls retrieved: %d\n", steps_used, MAX_STEPS, g_seen_count);
    printf("COST llm_calls=%ld tokens_in=%ld tokens_out=%ld searches=%ld fetches=%ld steps=%d\n",
           g_llm_calls, g_tok_in, g_tok_out, g_search_calls, g_fetch_calls, steps_used);

    if (g_source_count) {
        printf("\nsources:\n");
        for (int i = 0; i < g_source_count; i++) printf("  [%d] %s\n", i + 1, g_sources[i]);
    } else {
        printf("\nno verifiable sources were cited.\n");
    }
    if (g_sources_stripped)
        printf("\n%d cited url%s removed: not retrieved in this run.\n",
               g_sources_stripped, g_sources_stripped == 1 ? " was" : "s were");

    free(g_answer); free(g_confidence); free(g_unresolved); free(g_result);
    return 0;
}
