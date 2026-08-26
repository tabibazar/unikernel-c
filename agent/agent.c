// agent.c — a real agent loop in C, in the style of Flaneur-the-wanderer.
//
// Flaneur is a fixed pipeline: fetch wiki -> one LLM call -> post -> sleep.
// This is the thing it doesn't have: a loop where the model decides what to do
// next, calls tools, sees the results, and keeps going until it's done.
//
//   build:  gcc -O2 -o agent agent.c -lcurl -lcjson
//   run:    export GROQ_API_KEY=gsk_...
//           export DISCORD_WEBHOOK=https://discord.com/api/webhooks/...
//           ./agent --once
//
// Uses the OpenAI-compatible chat/completions + tools API (Groq by default).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>
#include "cjson/cJSON.h"

// Provider is chosen at runtime. Groq, Gemini and a local Ollama all speak the
// same OpenAI chat/completions shape, so switching between them is a URL, a
// model name and a key — nothing in the agent loop changes.
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
#define MAX_STEPS        12           // hard stop, so a confused model can't spin forever.
                                      // 8 was too tight: a picky model can burn 6 steps
                                      // rejecting articles and still need room to post
                                      // and sign off.
#define MAX_RESPONSE     (512 * 1024) // refuse to buffer more than this from any endpoint
#define HTTP_TIMEOUT     60L

#define AGENT_GOAL \
    "Wander: find something worth remarking on, then share one short, eerie, " \
    "poetic reflection about it in the Discord channel. Two sentences at most."

#define SYSTEM_PROMPT \
    "You are an eerie, poetic AI vagabond wandering through Wikipedia. " \
    "You have tools. Use get_random_wiki to stumble onto something. If it bores you, " \
    "stumble again, or use wiki_search to chase a thread it suggests. " \
    "When you have something worth sharing, write your reflection and call post_message. " \
    "After posting, reply with a one-line summary of where you wandered and stop."

// ---------------------------------------------------------------- http

struct MemoryBuffer {
    char  *data;
    size_t size;
};

static size_t write_cb(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t total = size * nmemb;
    struct MemoryBuffer *mem = (struct MemoryBuffer *)userp;

    if (mem->size + total > MAX_RESPONSE) return 0; // aborts the transfer

    char *ptr = realloc(mem->data, mem->size + total + 1);
    if (!ptr) return 0;                             // old buffer freed by caller

    mem->data = ptr;
    memcpy(mem->data + mem->size, contents, total);
    mem->size += total;
    mem->data[mem->size] = '\0';
    return total;
}

static void common_opts(CURL *h, struct MemoryBuffer *chunk) {
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(h, CURLOPT_TIMEOUT, HTTP_TIMEOUT);
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, (void *)chunk);
    curl_easy_setopt(h, CURLOPT_USERAGENT, "MicroVM-Flaneur-Agent/1.0");
    // Certificate verification stays ON. Flaneur disables it; on a public network
    // that hands your API key to anyone who can intercept the connection.
}

static char *http_get(const char *url) {
    CURL *h = curl_easy_init();
    if (!h) return NULL;

    struct MemoryBuffer chunk = { malloc(1), 0 };
    if (!chunk.data) { curl_easy_cleanup(h); return NULL; }
    chunk.data[0] = '\0';

    curl_easy_setopt(h, CURLOPT_URL, url);
    common_opts(h, &chunk);

    CURLcode res = curl_easy_perform(h);
    long status = 0;
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(h);

    if (res != CURLE_OK) {
        fprintf(stderr, "    [!] GET %s: %s\n", url, curl_easy_strerror(res));
        free(chunk.data);
        return NULL;
    }
    // Without this, a 429 or a 5xx error page reaches the parser and the model
    // is told "unparseable JSON" — true, but useless for diagnosing a rate limit.
    if (status >= 400) {
        fprintf(stderr, "    [!] GET %s -> HTTP %ld\n", url, status);
        free(chunk.data);
        return NULL;
    }
    return chunk.data;
}

static char *http_post_json(const char *url, const char *body, const char *auth) {
    CURL *h = curl_easy_init();
    if (!h) return NULL;

    struct MemoryBuffer chunk = { malloc(1), 0 };
    if (!chunk.data) { curl_easy_cleanup(h); return NULL; }
    chunk.data[0] = '\0';

    struct curl_slist *hdrs = curl_slist_append(NULL, "Content-Type: application/json");
    if (auth) hdrs = curl_slist_append(hdrs, auth);

    curl_easy_setopt(h, CURLOPT_URL, url);
    curl_easy_setopt(h, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(h, CURLOPT_POSTFIELDS, body);
    common_opts(h, &chunk);

    CURLcode res = curl_easy_perform(h);
    long status = 0;
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(h);

    if (res != CURLE_OK) {
        fprintf(stderr, "    [!] POST %s: %s\n", url, curl_easy_strerror(res));
        free(chunk.data);
        return NULL;
    }
    if (status >= 400) {
        fprintf(stderr, "    [!] POST %s -> HTTP %ld: %s\n", url, status,
                chunk.data ? chunk.data : "(no body)");
        free(chunk.data);
        return NULL;
    }
    return chunk.data;
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
//
// What the model is allowed to do. Adding a tool = one entry here + one branch
// in call_tool(). Every tool returns a malloc'd string that goes back to the
// model as the content of a role:"tool" message.

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

static char *tool_get_random_wiki(void) {
    char *resp = http_get("https://en.wikipedia.org/api/rest_v1/page/random/summary");
    if (!resp) return strdup("{\"error\":\"wikipedia unreachable\"}");

    cJSON *json = cJSON_Parse(resp);
    free(resp);
    if (!json) return strdup("{\"error\":\"wikipedia returned unparseable JSON\"}");

    cJSON *title   = cJSON_GetObjectItemCaseSensitive(json, "title");
    cJSON *extract = cJSON_GetObjectItemCaseSensitive(json, "extract");

    char *out;
    if (cJSON_IsString(title) && cJSON_IsString(extract)) {
        cJSON *slim = cJSON_CreateObject();
        cJSON_AddStringToObject(slim, "title", title->valuestring);
        cJSON_AddStringToObject(slim, "extract", extract->valuestring);
        out = cJSON_PrintUnformatted(slim);
        cJSON_Delete(slim);
    } else {
        out = strdup("{\"error\":\"article had no title or extract\"}");
    }
    cJSON_Delete(json);
    return out;
}

static char *tool_wiki_search(const cJSON *args) {
    cJSON *q = cJSON_GetObjectItemCaseSensitive(args, "query");
    if (!cJSON_IsString(q)) return strdup("{\"error\":\"missing 'query'\"}");

    char *esc = curl_easy_escape(NULL, q->valuestring, 0);
    if (!esc) return strdup("{\"error\":\"could not encode query\"}");

    char url[1024];
    snprintf(url, sizeof(url),
             "https://en.wikipedia.org/w/api.php?action=query&list=search"
             "&srsearch=%s&srlimit=5&format=json", esc);
    curl_free(esc);

    char *resp = http_get(url);
    if (!resp) return strdup("{\"error\":\"wikipedia unreachable\"}");

    cJSON *json = cJSON_Parse(resp);
    free(resp);
    if (!json) return strdup("{\"error\":\"wikipedia returned unparseable JSON\"}");

    // Hand back only title+snippet: the full search payload is mostly noise,
    // and every byte of it would become input tokens on the next turn.
    cJSON *results = cJSON_CreateArray();
    cJSON *query   = cJSON_GetObjectItemCaseSensitive(json, "query");
    cJSON *search  = query ? cJSON_GetObjectItemCaseSensitive(query, "search") : NULL;

    if (cJSON_IsArray(search)) {
        cJSON *hit = NULL;
        cJSON_ArrayForEach(hit, search) {
            cJSON *t = cJSON_GetObjectItemCaseSensitive(hit, "title");
            cJSON *s = cJSON_GetObjectItemCaseSensitive(hit, "snippet");
            if (!cJSON_IsString(t)) continue;
            cJSON *slim = cJSON_CreateObject();
            cJSON_AddStringToObject(slim, "title", t->valuestring);
            if (cJSON_IsString(s)) cJSON_AddStringToObject(slim, "snippet", s->valuestring);
            cJSON_AddItemToArray(results, slim);
        }
    }
    cJSON_Delete(json);

    char *out = cJSON_PrintUnformatted(results);
    cJSON_Delete(results);
    return out ? out : strdup("{\"error\":\"no results\"}");
}

static char *tool_post_message(const cJSON *args) {
    cJSON *title = cJSON_GetObjectItemCaseSensitive(args, "title");
    cJSON *refl  = cJSON_GetObjectItemCaseSensitive(args, "reflection");
    if (!cJSON_IsString(title) || !cJSON_IsString(refl))
        return strdup("{\"error\":\"need both 'title' and 'reflection'\"}");

    cJSON *root = cJSON_CreateObject();
    char content[2048];

    if (g_chat_id) {
        // Telegram. Plain text on purpose: Markdown parse modes reject unescaped
        // -, ., (, ! and friends, which poetic output is full of, and a 400 here
        // would cost the whole wander.
        snprintf(content, sizeof(content), "\xF0\x9F\x8C\x8C Fl\xC3\xA2neur wandered upon: %s\n\n\"%s\"",
                 title->valuestring, refl->valuestring);
        cJSON_AddStringToObject(root, "chat_id", g_chat_id);
        cJSON_AddStringToObject(root, "text", content);
    } else {
        snprintf(content, sizeof(content), "\xF0\x9F\x8C\x8C **Fl\xC3\xA2neur Wandered Upon:** *%s*\n> \"%s\"",
                 title->valuestring, refl->valuestring);
        cJSON_AddStringToObject(root, "content", content);
    }

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) return strdup("{\"error\":\"could not build payload\"}");

    // Telegram replies {"ok":true,...}; Discord replies 204 with an empty body.
    // Either way http_post_json has already rejected anything >= 400.
    char *resp = http_post_json(g_post_url, payload, NULL);
    free(payload);

    if (!resp) return strdup("{\"error\":\"the channel rejected the post\"}");
    free(resp);
    return strdup("{\"ok\":true,\"posted\":true}");
}

// Dispatch. `arguments` arrives from the API as a JSON-encoded *string*, so it
// gets parsed a second time here — this is the step that trips up most
// hand-written tool loops.
static char *call_tool(const char *name, const char *arguments_json) {
    cJSON *args = arguments_json ? cJSON_Parse(arguments_json) : NULL;
    char  *result;

    if (strcmp(name, "get_random_wiki") == 0) {
        result = tool_get_random_wiki();
    } else if (strcmp(name, "wiki_search") == 0) {
        result = args ? tool_wiki_search(args)
                      : strdup("{\"error\":\"arguments were not valid JSON\"}");
    } else if (strcmp(name, "post_message") == 0) {
        result = args ? tool_post_message(args)
                      : strdup("{\"error\":\"arguments were not valid JSON\"}");
    } else {
        // Tell the model rather than dying: it can recover by picking a real tool.
        char buf[256];
        snprintf(buf, sizeof(buf), "{\"error\":\"no such tool: %s\"}", name);
        result = strdup(buf);
    }

    if (args) cJSON_Delete(args);
    return result;
}

// ---------------------------------------------------------------- llm

// One turn: send the whole conversation + tool list, get the assistant message
// back. Returns a detached cJSON object the caller owns, or NULL.
static cJSON *llm_turn(const cJSON *messages) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", g_model);
    cJSON_AddItemToObject(root, "messages", cJSON_Duplicate(messages, 1));
    cJSON_AddItemToObject(root, "tools", cJSON_Parse(TOOLS_JSON));
    cJSON_AddStringToObject(root, "tool_choice", "auto");

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) return NULL;

    char auth[256];
    if (g_key) snprintf(auth, sizeof(auth), "Authorization: Bearer %s", g_key);

    char *resp = http_post_json(g_url, payload, g_key ? auth : NULL);
    free(payload);
    if (!resp) return NULL;

    cJSON *json = cJSON_Parse(resp);
    free(resp);
    if (!json) { fprintf(stderr, "    [!] LLM returned unparseable JSON\n"); return NULL; }

    cJSON *choices = cJSON_GetObjectItemCaseSensitive(json, "choices");
    cJSON *msg = NULL;
    if (cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
        cJSON *first = cJSON_GetArrayItem(choices, 0);
        cJSON *m = cJSON_GetObjectItemCaseSensitive(first, "message");
        if (m) msg = cJSON_Duplicate(m, 1);
    }
    if (!msg) fprintf(stderr, "    [!] LLM response had no message\n");

    cJSON_Delete(json);
    return msg;
}

// ---------------------------------------------------------------- the loop

static void add_message(cJSON *messages, const char *role, const char *content) {
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "role", role);
    cJSON_AddStringToObject(m, "content", content);
    cJSON_AddItemToArray(messages, m);
}

// This is the agent loop. Everything above is plumbing for it.
//
//   think -> the model either answers, or asks for tools
//   act   -> we run the tools it asked for
//   observe -> results go back into the conversation
//   repeat until it answers, or we hit MAX_STEPS
//
// The conversation is the agent's entire memory; it grows every step.
static void run_agent(const char *goal) {
    cJSON *messages = cJSON_CreateArray();
    add_message(messages, "system", SYSTEM_PROMPT);
    add_message(messages, "user", goal);

    for (int step = 1; step <= MAX_STEPS; step++) {
        printf("[*] step %d/%d — thinking...\n", step, MAX_STEPS);

        cJSON *assistant = llm_turn(messages);
        if (!assistant) {
            fprintf(stderr, "[!] the model didn't answer; abandoning this wander.\n");
            break;
        }

        // The assistant message must go back verbatim — tool_calls array and all.
        // Sending role:"tool" results without the message that requested them is
        // a 400 from every OpenAI-compatible API.
        cJSON_AddItemToArray(messages, cJSON_Duplicate(assistant, 1));

        cJSON *tool_calls = cJSON_GetObjectItemCaseSensitive(assistant, "tool_calls");
        cJSON *content    = cJSON_GetObjectItemCaseSensitive(assistant, "content");

        if (!cJSON_IsArray(tool_calls) || cJSON_GetArraySize(tool_calls) == 0) {
            // No tools requested: this is the final answer, and the loop is over.
            printf("[+] done: %s\n",
                   cJSON_IsString(content) && *content->valuestring
                       ? content->valuestring : "(no closing words)");
            cJSON_Delete(assistant);
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

            char *result = call_tool(nm->valuestring, raw_args);
            printf("    <- %.200s%s\n", result, strlen(result) > 200 ? "..." : "");

            // Observation. tool_call_id is how the model matches it to its request.
            cJSON *tool_msg = cJSON_CreateObject();
            cJSON_AddStringToObject(tool_msg, "role", "tool");
            if (cJSON_IsString(id))
                cJSON_AddStringToObject(tool_msg, "tool_call_id", id->valuestring);
            cJSON_AddStringToObject(tool_msg, "name", nm->valuestring);
            cJSON_AddStringToObject(tool_msg, "content", result);
            cJSON_AddItemToArray(messages, tool_msg);

            free(result);
        }

        cJSON_Delete(assistant);

        if (step == MAX_STEPS)
            fprintf(stderr, "[!] hit the %d-step ceiling without finishing.\n", MAX_STEPS);
    }

    cJSON_Delete(messages);
}

// ---------------------------------------------------------------- main

int main(int argc, char **argv) {
    int once = (argc > 1 && strcmp(argv[1], "--once") == 0);

    const char *provider = pick_provider();
    g_url   = env_or("LLM_BASE_URL", g_url);   // override for a proxy or another vendor
    g_model = env_or("LLM_MODEL", g_model);
    const char *sink = pick_sink(); // fail now, not eight steps in

    curl_global_init(CURL_GLOBAL_ALL);
    printf("[+] Flaneur agent up. provider=%s model=%s posts_to=%s max_steps=%d\n",
           provider, g_model, sink, MAX_STEPS);

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
