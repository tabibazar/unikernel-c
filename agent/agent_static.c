// agent_static.c — the agent loop from agent.c, with zero heap allocation in
// this translation unit. Intended for BareMetal / Firecracker microVMs where
// there may be no usable heap and the RAM budget is fixed at build time.
//
// The whole memory footprint is three static arrays, sized below. Nothing here
// calls malloc, realloc, free or strdup — the macros at the bottom of the
// "no heap" section make any such call a compile error, so the guarantee is
// enforced by the compiler rather than by discipline.
//
//   build (nix):  gcc -O2 -o agent_static agent_static.c -lcurl -lcjson
//   build (BareMetal):
//       cp agent_static.c BareMetal-App/
//       ./1-build.sh agent_static.c cjson/CJSON.c
//   run:  GROQ_API_KEY=gsk_... DISCORD_WEBHOOK=https://... ./agent_static --once

#include <stdio.h>
#include <stdlib.h>     // getenv, exit — no allocator use
#include <string.h>
#include <stddef.h>
#include <unistd.h>
#include <curl/curl.h>
#include "cjson/cJSON.h"

// ---------------------------------------------------------------- budget
//
// Every byte this program can use, decided at compile time. Shrink these for a
// tighter microVM; the program reports its own high-water mark on each cycle so
// you can size them from evidence instead of guessing.
//
//   arena    768 KB   cJSON nodes: the conversation, parsed responses
//   http     128 KB   one HTTP response at a time
//   payload  128 KB   one serialized request body at a time
//   ------------------
//   total   1024 KB   + libcurl's own internal allocations
//
// Arena sizing is measured, not guessed. A real 8-step wander peaked at 197 KB,
// and per-step cost grows as the conversation does (llm_turn duplicates the whole
// history each turn and the bump allocator doesn't reclaim until reset), so a
// full 12 steps projects to ~355 KB. 768 KB leaves roughly 2x headroom for
// longer articles. Watch the high-water mark this prints and shrink it if your
// microVM is tighter than that.

#define ARENA_BYTES      (768 * 1024)
#define HTTP_BUF_BYTES   (128 * 1024)
#define PAYLOAD_BYTES    (128 * 1024)

// Provider is chosen at runtime, not compile time. Groq, Gemini and a local
// Ollama all speak the same OpenAI chat/completions shape, so switching between
// them is a URL, a model name and a key — nothing in the agent loop changes.
//
//   GEMINI_API_KEY set -> Google Gemini
//   GROQ_API_KEY   set -> Groq
//   neither            -> local Ollama, no key needed
//   LLM_BASE_URL / LLM_MODEL override any of it
#define GROQ_URL      "https://api.groq.com/openai/v1/chat/completions"
#define GROQ_MODEL    "openai/gpt-oss-120b"
#define GEMINI_URL    "https://generativelanguage.googleapis.com/v1beta/openai/chat/completions"
#define GEMINI_MODEL  "gemini-flash-latest"
#define OLLAMA_URL    "http://localhost:11434/v1/chat/completions"
#define OLLAMA_MODEL  "llama3.2"

static const char *g_url;
static const char *g_model;
static const char *g_key;      // NULL for Ollama: no Authorization header

#define SLEEP_SECONDS    1800
#define MAX_STEPS        12     // 8 was too tight: a picky model can burn 6 steps
                                // rejecting articles and still need room to post
#define HTTP_TIMEOUT     60L
#define MAX_EXTRACT      1200   // clip wiki text: bounds both arena use and tokens

#define AGENT_GOAL \
    "Wander: find something worth remarking on, then share one short, eerie, " \
    "poetic reflection about it in the Discord channel. Two sentences at most."

#define SYSTEM_PROMPT \
    "You are an eerie, poetic AI vagabond wandering through Wikipedia. " \
    "You have tools. Use get_random_wiki to stumble onto something. If it bores you, " \
    "stumble again, or use wiki_search to chase a thread it suggests. " \
    "When you have something worth sharing, write your reflection and call post_message. " \
    "After posting, reply with a one-line summary of where you wandered and stop."

// ---------------------------------------------------------------- arena
//
// A bump allocator over one static array. Allocation is a pointer add; free is
// a no-op. cJSON is pointed at it via cJSON_InitHooks, so every cJSON node the
// agent builds lands here instead of on a heap.
//
// The trade: memory is only reclaimed in bulk, by arena_reset(). That is exactly
// right for this shape of program — one wander is one arena lifetime. It would
// be wrong for anything with long-lived, interleaved object lifetimes.

static unsigned char g_arena[ARENA_BYTES];
static size_t        g_arena_used  = 0;
static size_t        g_arena_peak  = 0;
static int           g_arena_full  = 0;   // sticky: set on first exhaustion

static void *arena_alloc(size_t n) {
    n = (n + 15u) & ~(size_t)15u;               // 16-byte align
    if (n > ARENA_BYTES - g_arena_used) {
        g_arena_full = 1;                        // caller sees NULL; run is abandoned
        return NULL;
    }
    void *p = &g_arena[g_arena_used];
    g_arena_used += n;
    if (g_arena_used > g_arena_peak) g_arena_peak = g_arena_used;
    return p;
}

static void arena_free(void *p) { (void)p; }     // deliberately nothing

static void arena_reset(void) {
    g_arena_used = 0;
    g_arena_full = 0;
}

// ---------------------------------------------------------------- fixed buffers

static char g_http[HTTP_BUF_BYTES];
static char g_payload[PAYLOAD_BYTES];

struct http_sink {
    size_t len;
    int    overflow;
};
static struct http_sink g_sink;

static size_t write_cb(void *contents, size_t size, size_t nmemb, void *userp) {
    struct http_sink *s = (struct http_sink *)userp;
    size_t total = size * nmemb;

    if (s->len + total >= HTTP_BUF_BYTES) {
        s->overflow = 1;
        return 0;                                // aborts the transfer, no growth
    }
    memcpy(g_http + s->len, contents, total);
    s->len += total;
    g_http[s->len] = '\0';
    return total;
}

// ---------------------------------------------------------------- no heap
//
// Past this point, touching the allocator will not compile. If you add code
// below and it fails with "DO_NOT_...", you have introduced a heap dependency —
// route it through the arena or a fixed buffer instead.
#define malloc   DO_NOT_malloc
#define calloc   DO_NOT_calloc
#define realloc  DO_NOT_realloc
#define free     DO_NOT_free
#define strdup   DO_NOT_strdup

// ---------------------------------------------------------------- http

static void common_opts(CURL *h) {
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(h, CURLOPT_TIMEOUT, HTTP_TIMEOUT);
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, (void *)&g_sink);
    curl_easy_setopt(h, CURLOPT_USERAGENT, "MicroVM-Flaneur-Agent/1.0");
    // Certificate verification stays on.
}

// Both return g_http (NUL-terminated) or NULL. The buffer is reused by every
// call, so parse what you need before issuing the next request.
static const char *http_get(const char *url) {
    CURL *h = curl_easy_init();
    if (!h) return NULL;

    g_sink.len = 0; g_sink.overflow = 0; g_http[0] = '\0';
    curl_easy_setopt(h, CURLOPT_URL, url);
    common_opts(h);

    CURLcode res = curl_easy_perform(h);
    long status = 0;
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(h);

    if (res != CURLE_OK) {
        fprintf(stderr, "    [!] GET: %s%s\n", curl_easy_strerror(res),
                g_sink.overflow ? " (response exceeded HTTP_BUF_BYTES)" : "");
        return NULL;
    }
    // Without this, a 429 or a 5xx error page reaches the parser and the model
    // is told "unparseable JSON" — true, but useless for diagnosing a rate limit.
    if (status >= 400) {
        fprintf(stderr, "    [!] GET -> HTTP %ld\n", status);
        return NULL;
    }
    return g_http;
}

static const char *http_post_json(const char *url, const char *body, const char *auth) {
    CURL *h = curl_easy_init();
    if (!h) return NULL;

    struct curl_slist *hdrs = curl_slist_append(NULL, "Content-Type: application/json");
    if (auth) hdrs = curl_slist_append(hdrs, auth);

    g_sink.len = 0; g_sink.overflow = 0; g_http[0] = '\0';
    curl_easy_setopt(h, CURLOPT_URL, url);
    curl_easy_setopt(h, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(h, CURLOPT_POSTFIELDS, body);
    common_opts(h);

    CURLcode res = curl_easy_perform(h);
    long status = 0;
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(h);

    if (res != CURLE_OK) {
        fprintf(stderr, "    [!] POST: %s%s\n", curl_easy_strerror(res),
                g_sink.overflow ? " (response exceeded HTTP_BUF_BYTES)" : "");
        return NULL;
    }
    if (status >= 400) {
        fprintf(stderr, "    [!] POST -> HTTP %ld: %.400s\n", status, g_http);
        return NULL;
    }
    return g_http;
}

// Percent-encode into a caller-supplied buffer. curl_easy_escape() would do
// this, but it allocates.
static int url_encode(const char *in, char *out, size_t out_sz) {
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p; p++) {
        unsigned char c = *p;
        int unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                         (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                         c == '.' || c == '~';
        if (unreserved) {
            if (o + 2 > out_sz) return 0;
            out[o++] = (char)c;
        } else {
            if (o + 4 > out_sz) return 0;
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 0x0F];
        }
    }
    out[o] = '\0';
    return 1;
}

// ---------------------------------------------------------------- config

static const char *env_or(const char *name, const char *fallback) {
    const char *v = getenv(name);
    return (v && *v) ? v : fallback;
}

// Where the agent publishes. Telegram if a bot token and chat id are present,
// Discord if a webhook is. Same tool either way — the model doesn't know or care.
static const char *g_chat_id;      // telegram only
static char        g_post_url[512];

static const char *pick_sink(void) {
    const char *tg_token = getenv("TELEGRAM_BOT_TOKEN");
    const char *tg_chat  = getenv("TELEGRAM_CHAT_ID");
    const char *webhook  = getenv("DISCORD_WEBHOOK");

    if (tg_token && *tg_token && tg_chat && *tg_chat) {
        snprintf(g_post_url, sizeof(g_post_url),
                 "https://api.telegram.org/bot%s/sendMessage", tg_token);
        g_chat_id = tg_chat;
        return "telegram";
    }
    if (webhook && *webhook) {
        snprintf(g_post_url, sizeof(g_post_url), "%s", webhook);
        g_chat_id = NULL;
        return "discord";
    }
    fprintf(stderr, "[!] no destination: set TELEGRAM_BOT_TOKEN + TELEGRAM_CHAT_ID, "
                    "or DISCORD_WEBHOOK.\n");
    exit(1);
}

// Decide who we're talking to. Nothing below this cares about the answer.
static const char *pick_provider(void) {
    const char *gemini = getenv("GEMINI_API_KEY");
    const char *groq   = getenv("GROQ_API_KEY");

    if (gemini && *gemini) {
        g_key = gemini; g_url = GEMINI_URL;  g_model = GEMINI_MODEL;  return "gemini";
    }
    if (groq && *groq) {
        g_key = groq;   g_url = GROQ_URL;    g_model = GROQ_MODEL;    return "groq";
    }
    g_key = NULL;       g_url = OLLAMA_URL;  g_model = OLLAMA_MODEL;  return "ollama";
}

// ---------------------------------------------------------------- tools

static const char *TOOLS_JSON =
"["
"  {\"type\":\"function\",\"function\":{"
"     \"name\":\"get_random_wiki\","
"     \"description\":\"Stumble onto a random Wikipedia article. Returns its title and summary.\","
"     \"parameters\":{\"type\":\"object\",\"properties\":{},\"required\":[]}}},"
"  {\"type\":\"function\",\"function\":{"
"     \"name\":\"wiki_search\","
"     \"description\":\"Search Wikipedia for articles matching a phrase. Returns matching titles and snippets.\","
"     \"parameters\":{\"type\":\"object\",\"properties\":{"
"        \"query\":{\"type\":\"string\",\"description\":\"What to search for.\"}},"
"        \"required\":[\"query\"]}}},"
"  {\"type\":\"function\",\"function\":{"
"     \"name\":\"post_message\","
"     \"description\":\"Publish a reflection to the channel. Call this once, when you have something worth sharing.\","
"     \"parameters\":{\"type\":\"object\",\"properties\":{"
"        \"title\":{\"type\":\"string\",\"description\":\"The subject you wandered upon.\"},"
"        \"reflection\":{\"type\":\"string\",\"description\":\"Your two-sentence reflection.\"}},"
"        \"required\":[\"title\",\"reflection\"]}}}"
"]";

// Tool results are written into this buffer, then copied into the conversation
// by cJSON (which copies strings on AddStringToObject). One tool runs at a time.
static char g_tool_result[8192];

static void tool_error(const char *msg) {
    snprintf(g_tool_result, sizeof(g_tool_result), "{\"error\":\"%s\"}", msg);
}

static void tool_get_random_wiki(void) {
    const char *resp = http_get("https://en.wikipedia.org/api/rest_v1/page/random/summary");
    if (!resp) { tool_error("wikipedia unreachable"); return; }

    cJSON *json = cJSON_Parse(resp);
    if (!json) { tool_error("wikipedia returned unparseable JSON"); return; }

    cJSON *title   = cJSON_GetObjectItemCaseSensitive(json, "title");
    cJSON *extract = cJSON_GetObjectItemCaseSensitive(json, "extract");

    if (cJSON_IsString(title) && cJSON_IsString(extract)) {
        cJSON *slim = cJSON_CreateObject();
        if (slim) {
            cJSON_AddStringToObject(slim, "title", title->valuestring);
            char clipped[MAX_EXTRACT + 1];
            snprintf(clipped, sizeof(clipped), "%s", extract->valuestring);
            cJSON_AddStringToObject(slim, "extract", clipped);
            if (!cJSON_PrintPreallocated(slim, g_tool_result, (int)sizeof(g_tool_result), 0))
                tool_error("article did not fit in the tool result buffer");
            cJSON_Delete(slim);
        } else {
            tool_error("arena exhausted");
        }
    } else {
        tool_error("article had no title or extract");
    }
    cJSON_Delete(json);
}

static void tool_wiki_search(const cJSON *args) {
    cJSON *q = cJSON_GetObjectItemCaseSensitive(args, "query");
    if (!cJSON_IsString(q)) { tool_error("missing 'query'"); return; }

    char esc[512];
    if (!url_encode(q->valuestring, esc, sizeof(esc))) { tool_error("query too long"); return; }

    char url[1024];
    snprintf(url, sizeof(url),
             "https://en.wikipedia.org/w/api.php?action=query&list=search"
             "&srsearch=%s&srlimit=5&format=json", esc);

    const char *resp = http_get(url);
    if (!resp) { tool_error("wikipedia unreachable"); return; }

    cJSON *json = cJSON_Parse(resp);
    if (!json) { tool_error("wikipedia returned unparseable JSON"); return; }

    // Keep title+snippet only; the rest of the payload is noise that would
    // become input tokens on every subsequent turn.
    cJSON *results = cJSON_CreateArray();
    cJSON *query   = cJSON_GetObjectItemCaseSensitive(json, "query");
    cJSON *search  = query ? cJSON_GetObjectItemCaseSensitive(query, "search") : NULL;

    if (results && cJSON_IsArray(search)) {
        cJSON *hit = NULL;
        cJSON_ArrayForEach(hit, search) {
            cJSON *t = cJSON_GetObjectItemCaseSensitive(hit, "title");
            cJSON *s = cJSON_GetObjectItemCaseSensitive(hit, "snippet");
            if (!cJSON_IsString(t)) continue;
            cJSON *slim = cJSON_CreateObject();
            if (!slim) break;
            cJSON_AddStringToObject(slim, "title", t->valuestring);
            if (cJSON_IsString(s)) cJSON_AddStringToObject(slim, "snippet", s->valuestring);
            cJSON_AddItemToArray(results, slim);
        }
    }

    if (!results || !cJSON_PrintPreallocated(results, g_tool_result,
                                             (int)sizeof(g_tool_result), 0))
        tool_error("search results did not fit in the tool result buffer");

    cJSON_Delete(results);
    cJSON_Delete(json);
}

static void tool_post_message(const cJSON *args) {
    cJSON *title = cJSON_GetObjectItemCaseSensitive(args, "title");
    cJSON *refl  = cJSON_GetObjectItemCaseSensitive(args, "reflection");
    if (!cJSON_IsString(title) || !cJSON_IsString(refl)) {
        tool_error("need both 'title' and 'reflection'");
        return;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) { tool_error("arena exhausted"); return; }

    char content[2048];
    if (g_chat_id) {
        // Telegram. Sent as plain text on purpose: Markdown parse modes reject
        // unescaped -, ., (, ! and friends, which poetic output is full of, and
        // a 400 here would cost the whole wander.
        snprintf(content, sizeof(content), "\xF0\x9F\x8C\x8C Fl\xC3\xA2neur wandered upon: %s\n\n\"%s\"",
                 title->valuestring, refl->valuestring);
        cJSON_AddStringToObject(root, "chat_id", g_chat_id);
        cJSON_AddStringToObject(root, "text", content);
    } else {
        snprintf(content, sizeof(content),
                 "\xF0\x9F\x8C\x8C **Fl\xC3\xA2neur Wandered Upon:** *%s*\n> \"%s\"",
                 title->valuestring, refl->valuestring);
        cJSON_AddStringToObject(root, "content", content);
    }

    int ok = cJSON_PrintPreallocated(root, g_payload, (int)sizeof(g_payload), 0);
    cJSON_Delete(root);
    if (!ok) { tool_error("could not serialize the message payload"); return; }

    // Telegram replies {"ok":true,...}; Discord replies 204 with an empty body.
    // http_post_json has already rejected anything >= 400.
    if (!http_post_json(g_post_url, g_payload, NULL))
        tool_error("the channel rejected the post");
    else
        snprintf(g_tool_result, sizeof(g_tool_result), "{\"ok\":true,\"posted\":true}");
}

// `arguments` arrives as a JSON-encoded *string* and needs a second parse —
// the step most hand-written tool loops get wrong.
static void call_tool(const char *name, const char *arguments_json) {
    cJSON *args = arguments_json ? cJSON_Parse(arguments_json) : NULL;

    if (strcmp(name, "get_random_wiki") == 0) {
        tool_get_random_wiki();
    } else if (strcmp(name, "wiki_search") == 0) {
        if (args) tool_wiki_search(args); else tool_error("arguments were not valid JSON");
    } else if (strcmp(name, "post_message") == 0) {
        if (args) tool_post_message(args); else tool_error("arguments were not valid JSON");
    } else {
        // Report it to the model instead of dying; it can pick a real tool next turn.
        snprintf(g_tool_result, sizeof(g_tool_result), "{\"error\":\"no such tool: %.64s\"}", name);
    }

    if (args) cJSON_Delete(args);
}

// ---------------------------------------------------------------- llm

// One turn. The returned message lives in the arena and stays valid until the
// next arena_reset(), so there is nothing for the caller to release.
static cJSON *llm_turn(const cJSON *messages) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON_AddStringToObject(root, "model", g_model);
    cJSON_AddItemToObject(root, "messages", cJSON_Duplicate(messages, 1));
    cJSON_AddItemToObject(root, "tools", cJSON_Parse(TOOLS_JSON));
    cJSON_AddStringToObject(root, "tool_choice", "auto");

    int ok = cJSON_PrintPreallocated(root, g_payload, (int)sizeof(g_payload), 0);
    cJSON_Delete(root);
    if (!ok) {
        fprintf(stderr, "    [!] request exceeded PAYLOAD_BYTES (%d)\n", PAYLOAD_BYTES);
        return NULL;
    }

    char auth[256];
    if (g_key) snprintf(auth, sizeof(auth), "Authorization: Bearer %s", g_key);

    const char *resp = http_post_json(g_url, g_payload, g_key ? auth : NULL);
    if (!resp) return NULL;

    cJSON *json = cJSON_Parse(resp);
    if (!json) { fprintf(stderr, "    [!] LLM returned unparseable JSON\n"); return NULL; }

    cJSON *choices = cJSON_GetObjectItemCaseSensitive(json, "choices");
    cJSON *msg = NULL;
    if (cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
        cJSON *first = cJSON_GetArrayItem(choices, 0);
        cJSON *m = cJSON_GetObjectItemCaseSensitive(first, "message");
        if (m) msg = cJSON_Duplicate(m, 1);      // detach before json is dropped
    }
    if (!msg) fprintf(stderr, "    [!] LLM response had no message\n");

    cJSON_Delete(json);
    return msg;
}

// ---------------------------------------------------------------- the loop

static void add_message(cJSON *messages, const char *role, const char *content) {
    cJSON *m = cJSON_CreateObject();
    if (!m) return;
    cJSON_AddStringToObject(m, "role", role);
    cJSON_AddStringToObject(m, "content", content);
    cJSON_AddItemToArray(messages, m);
}

//   think   -> the model answers, or asks for tools
//   act     -> we run what it asked for
//   observe -> results go back into the conversation
//   repeat  -> until it answers, or MAX_STEPS, or the arena fills
//
// The conversation array is the agent's whole memory, and it grows every step —
// which is precisely why the arena needs a ceiling and a reset.
static void run_agent(const char *goal) {
    arena_reset();

    cJSON *messages = cJSON_CreateArray();
    if (!messages) { fprintf(stderr, "[!] arena too small to start.\n"); return; }

    add_message(messages, "system", SYSTEM_PROMPT);
    add_message(messages, "user", goal);

    for (int step = 1; step <= MAX_STEPS; step++) {
        printf("[*] step %d/%d — thinking... (arena %zu/%d KB)\n",
               step, MAX_STEPS, g_arena_used / 1024, ARENA_BYTES / 1024);

        cJSON *assistant = llm_turn(messages);
        if (g_arena_full) { fprintf(stderr, "[!] arena exhausted; abandoning this wander.\n"); break; }
        if (!assistant)   { fprintf(stderr, "[!] the model didn't answer; abandoning this wander.\n"); break; }

        // Echo the assistant message back verbatim, tool_calls array included.
        // Sending role:"tool" results without it is a 400 from every
        // OpenAI-compatible API.
        cJSON_AddItemToArray(messages, cJSON_Duplicate(assistant, 1));

        cJSON *tool_calls = cJSON_GetObjectItemCaseSensitive(assistant, "tool_calls");
        cJSON *content    = cJSON_GetObjectItemCaseSensitive(assistant, "content");

        if (!cJSON_IsArray(tool_calls) || cJSON_GetArraySize(tool_calls) == 0) {
            printf("[+] done: %s\n",
                   cJSON_IsString(content) && *content->valuestring
                       ? content->valuestring : "(no closing words)");
            break;
        }

        if (cJSON_IsString(content) && *content->valuestring)
            printf("    thought: %s\n", content->valuestring);

        cJSON *tc = NULL;
        cJSON_ArrayForEach(tc, tool_calls) {
            cJSON *id = cJSON_GetObjectItemCaseSensitive(tc, "id");
            cJSON *fn = cJSON_GetObjectItemCaseSensitive(tc, "function");
            cJSON *nm = fn ? cJSON_GetObjectItemCaseSensitive(fn, "name") : NULL;
            cJSON *ar = fn ? cJSON_GetObjectItemCaseSensitive(fn, "arguments") : NULL;
            if (!cJSON_IsString(nm)) continue;

            const char *raw_args = cJSON_IsString(ar) ? ar->valuestring : NULL;
            printf("    -> %s(%s)\n", nm->valuestring, raw_args ? raw_args : "");

            call_tool(nm->valuestring, raw_args);
            printf("    <- %.200s%s\n", g_tool_result,
                   strlen(g_tool_result) > 200 ? "..." : "");

            cJSON *tool_msg = cJSON_CreateObject();
            if (!tool_msg) break;
            cJSON_AddStringToObject(tool_msg, "role", "tool");
            if (cJSON_IsString(id))
                cJSON_AddStringToObject(tool_msg, "tool_call_id", id->valuestring);
            cJSON_AddStringToObject(tool_msg, "name", nm->valuestring);
            cJSON_AddStringToObject(tool_msg, "content", g_tool_result);
            cJSON_AddItemToArray(messages, tool_msg);
        }

        if (step == MAX_STEPS)
            fprintf(stderr, "[!] hit the %d-step ceiling without finishing.\n", MAX_STEPS);
    }

    printf("[*] arena high-water mark: %zu of %d KB\n",
           g_arena_peak / 1024, ARENA_BYTES / 1024);
    // No cJSON_Delete(messages) needed — arena_reset() reclaims everything at
    // the top of the next wander.
}

// ---------------------------------------------------------------- main

int main(int argc, char **argv) {
    int once = (argc > 1 && strcmp(argv[1], "--once") == 0);

    const char *provider = pick_provider();
    g_url   = env_or("LLM_BASE_URL", g_url);     // override for a proxy or another vendor
    g_model = env_or("LLM_MODEL", g_model);
    const char *sink = pick_sink();              // fail now, not eight steps in

    cJSON_Hooks hooks;
    hooks.malloc_fn = arena_alloc;
    hooks.free_fn   = arena_free;
    cJSON_InitHooks(&hooks);         // every cJSON node now lands in g_arena

    curl_global_init(CURL_GLOBAL_ALL);
    printf("[+] Flaneur agent (static) up. provider=%s model=%s posts_to=%s max_steps=%d\n",
           provider, g_model, sink, MAX_STEPS);
    printf("[+] fixed footprint: %d KB arena + %d KB http + %d KB payload = %d KB\n",
           ARENA_BYTES / 1024, HTTP_BUF_BYTES / 1024, PAYLOAD_BYTES / 1024,
           (ARENA_BYTES + HTTP_BUF_BYTES + PAYLOAD_BYTES) / 1024);

    do {
        run_agent(AGENT_GOAL);
        if (!once) {
            printf("[*] sleeping %d seconds...\n\n", SLEEP_SECONDS);
            sleep(SLEEP_SECONDS);
        }
    } while (!once);

    curl_global_cleanup();
    return 0;
}
