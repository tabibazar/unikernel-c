// supervisor.c — the night shift for the Cunningham swarm.
//
// The workers are deliberately dumb: a C loop that tests candidates and posts
// findings. Nothing about that needs a model. What does need one is the
// question "is worker 1 broken, and if so, what broke?" — because the answer
// isn't enumerable in advance. It crashed. The instance was reclaimed. DNS
// failed. It hit the instance cap. Or it is perfectly healthy and its slice of
// the number line simply had nothing worth announcing, in which case silence is
// the correct behaviour and intervening would be the bug.
//
// Each probe is chosen because of what the last one returned, and the stop
// condition is "I can rule out the alternatives" — which is what makes this an
// agent rather than a script with an LLM bolted on.
//
//   build: gcc -O2 -o supervisor supervisor.c -lcurl -lcjson
//   run:   GEMINI_API_KEY=... TELEGRAM_BOT_TOKEN=... TELEGRAM_CHAT_ID=... ./supervisor
//
// One diagnosis cycle per invocation, then exit. Wrap it in cron or a sleep
// loop if you want it permanent.

// popen/pclose/strtok_r are POSIX, and -std=c99 hides them. Without this an
// implicitly-declared popen() is assumed to return int, which truncates the
// FILE* on a 64-bit machine and crashes on first use.
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <curl/curl.h>
#include "cjson/cJSON.h"

// Defaults; both overridable so the supervisor can fall back to another
// provider — or a local Ollama — when one is down. Gemini returning 503 for an
// hour is exactly the situation this exists for.
#define LLM_URL_DEFAULT   "https://generativelanguage.googleapis.com/v1beta/openai/chat/completions"
#define LLM_MODEL_DEFAULT "gemini-flash-latest"
#define MAX_STEPS   10
#define SWARM_SIZE  3
#define MAX_INSTANCES 4          // the cloud's own per-user cap
#define LOG_TAIL    2000         // bytes of serial log the model is shown
#define MUTATION_BUDGET 1        // corrective actions allowed per run

// ---------------------------------------------------------------- safety
//
// The model never supplies a shell string, a path, or an id. It supplies a
// worker NAME, and only names matching cunningham-w<digit> are accepted; the
// id is resolved here, from the API's own output. That single rule is both the
// injection fence and the blast radius: this program is structurally incapable
// of touching an instance that isn't a swarm worker, whatever it is asked to do.

static int valid_worker_name(const char *n) {
    if (!n) return 0;
    if (strncmp(n, "cunningham-w", 12) != 0) return 0;
    if (!isdigit((unsigned char)n[12])) return 0;
    return n[13] == '\0';
}

static int valid_id(const char *s) {
    if (!s) return 0;
    size_t len = strlen(s);
    if (len < 16 || len > 40) return 0;
    for (size_t i = 0; i < len; i++)
        if (!islower((unsigned char)s[i]) && !isdigit((unsigned char)s[i])) return 0;
    return 1;
}

static int g_mutations_used = 0;

// ---------------------------------------------------------------- shell

// Runs a fixed command built entirely from constants and values this program
// validated itself. Never interpolates model output.
static int run_cmd(const char *cmd, char *out, size_t out_sz) {
    FILE *fp = popen(cmd, "r");
    if (!fp) { snprintf(out, out_sz, "could not run command"); return 0; }
    size_t n = fread(out, 1, out_sz - 1, fp);
    out[n] = '\0';
    pclose(fp);
    return 1;
}

// The supervisor needs nothing but HTTPS to two APIs, so it can run anywhere —
// a laptop, a VM, a cron host. Point it at a BareMetal-App checkout (for
// bm-api.sh) and a file holding the cloud key.
static const char *api_prefix(void) {
    static char pfx[1024];
    const char *home = getenv("HOME");
    const char *dir  = getenv("BM_API_DIR");
    const char *key  = getenv("BM_KEY_FILE");
    static char dbuf[512], kbuf[512];

    if (!dir) { snprintf(dbuf, sizeof(dbuf), "%s/BareMetal-App", home ? home : "."); dir = dbuf; }
    if (!key) { snprintf(kbuf, sizeof(kbuf), "%s/.bm_key", home ? home : "."); key = kbuf; }

    snprintf(pfx, sizeof(pfx), "cd '%s' && BM_API_KEY=$(cat '%s') ./bm-api.sh", dir, key);
    return pfx;
}

// ---------------------------------------------------------------- tools

static const char *TOOLS_JSON =
"["
"  {\"type\":\"function\",\"function\":{"
"     \"name\":\"list_workers\","
"     \"description\":\"List every swarm instance with its current status, plus how many instance slots are free. Start here.\","
"     \"parameters\":{\"type\":\"object\",\"properties\":{},\"required\":[]}}},"
"  {\"type\":\"function\",\"function\":{"
"     \"name\":\"worker_logs\","
"     \"description\":\"Read the tail of a worker's serial console. This is the only way to see what the program itself reported: network setup, findings, heartbeats, errors.\","
"     \"parameters\":{\"type\":\"object\",\"properties\":{"
"        \"name\":{\"type\":\"string\",\"description\":\"Worker name, e.g. cunningham-w1\"}},"
"        \"required\":[\"name\"]}}},"
"  {\"type\":\"function\",\"function\":{"
"     \"name\":\"restart_worker\","
"     \"description\":\"Bring a worker back to RUNNING: starts it if stopped, reboots it if wedged, and confirms the result. One corrective action per run. It will REFUSE if the worker is already healthy, so use it only when the evidence shows a real fault.\","
"     \"parameters\":{\"type\":\"object\",\"properties\":{"
"        \"name\":{\"type\":\"string\",\"description\":\"Worker name\"},"
"        \"because\":{\"type\":\"string\",\"description\":\"The specific evidence that this worker is faulty.\"}},"
"        \"required\":[\"name\",\"because\"]}}},"
"  {\"type\":\"function\",\"function\":{"
"     \"name\":\"send_report\","
"     \"description\":\"Send your findings to the operator on Telegram. Call this last, once, when you know what is going on.\","
"     \"parameters\":{\"type\":\"object\",\"properties\":{"
"        \"text\":{\"type\":\"string\",\"description\":\"3-5 lines: what you checked, what you found, what you did, whether the swarm is healthy.\"}},"
"        \"required\":[\"text\"]}}}"
"]";

static char g_result[8192];
static char g_scratch[65536];

static void tool_error(const char *msg) {
    snprintf(g_result, sizeof(g_result), "{\"error\":\"%s\"}", msg);
}

// Resolve a validated worker name to its instance id, from the API's own list.
static int resolve_id(const char *name, char *id_out, size_t id_sz) {
    char cmd[700];
    snprintf(cmd, sizeof(cmd), "%s instances list 2>/dev/null", api_prefix());
    if (!run_cmd(cmd, g_scratch, sizeof(g_scratch))) return 0;

    char *line = strtok(g_scratch, "\n");
    while (line) {
        char id[64] = {0}, nm[64] = {0};
        if (sscanf(line, "%63s %63s", id, nm) == 2 && strcmp(nm, name) == 0) {
            if (!valid_id(id)) return 0;
            snprintf(id_out, id_sz, "%s", id);
            return 1;
        }
        line = strtok(NULL, "\n");
    }
    return 0;
}

static void tool_list_workers(void) {
    char cmd[700];
    snprintf(cmd, sizeof(cmd), "%s instances list 2>/dev/null", api_prefix());
    if (!run_cmd(cmd, g_scratch, sizeof(g_scratch))) { tool_error("could not reach the cloud API"); return; }

    cJSON *root = cJSON_CreateObject();
    cJSON *arr  = cJSON_CreateArray();
    int total = 0;
    int seen[10] = {0};

    char *save = NULL;
    for (char *line = strtok_r(g_scratch, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char id[64] = {0}, nm[64] = {0}, st[64] = {0};
        if (sscanf(line, "%63s %63s %63s", id, nm, st) < 3) continue;
        total++;
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "name", nm);
        cJSON_AddStringToObject(o, "status", st);
        cJSON_AddItemToArray(arr, o);
        if (valid_worker_name(nm)) seen[nm[12] - '0'] = 1;
    }
    cJSON_AddItemToObject(root, "instances", arr);
    cJSON_AddNumberToObject(root, "slots_free", MAX_INSTANCES - total);

    // Say plainly which expected workers are absent entirely — a missing
    // instance and a stopped one are different failures.
    cJSON *missing = cJSON_CreateArray();
    for (int i = 0; i < SWARM_SIZE; i++) {
        if (!seen[i]) {
            char want[32];
            snprintf(want, sizeof(want), "cunningham-w%d", i);
            cJSON_AddItemToArray(missing, cJSON_CreateString(want));
        }
    }
    cJSON_AddItemToObject(root, "missing_entirely", missing);
    cJSON_AddNumberToObject(root, "corrective_actions_left", MUTATION_BUDGET - g_mutations_used);

    if (!cJSON_PrintPreallocated(root, g_result, (int)sizeof(g_result), 0))
        tool_error("instance list did not fit");
    cJSON_Delete(root);
}

// What "healthy" means, decided here rather than by the model.
//
// The first version of this program left the judgement to the prompt, and the
// model promptly restarted a perfectly good worker because it had not announced
// a find recently — which is not a fault, it is the expected behaviour of a
// search whose hits are tens of minutes apart. Health is therefore computed
// mechanically: is the instance RUNNING, did its network come up, and is its log
// free of errors. Find counts are deliberately not part of it.
struct health {
    char status[32];
    int  net_ready;
    int  error_lines;
    int  found;
};

static int worker_health(const char *name, struct health *h) {
    memset(h, 0, sizeof(*h));

    char cmd[700];
    snprintf(cmd, sizeof(cmd), "%s instances list 2>/dev/null", api_prefix());
    if (!run_cmd(cmd, g_scratch, sizeof(g_scratch))) return 0;

    char id[64] = {0};
    char *save = NULL;
    for (char *line = strtok_r(g_scratch, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char lid[64] = {0}, nm[64] = {0}, st[64] = {0};
        if (sscanf(line, "%63s %63s %63s", lid, nm, st) < 3) continue;
        if (strcmp(nm, name) == 0) {
            snprintf(id, sizeof(id), "%s", lid);
            snprintf(h->status, sizeof(h->status), "%s", st);
            h->found = 1;
            break;
        }
    }
    if (!h->found || !valid_id(id)) return 0;

    snprintf(cmd, sizeof(cmd),
             "%s instances logs %s 2>/dev/null | grep -vE '^2026-|fc_api|fc_vcpu|anonymous-instance|^logs: task'",
             api_prefix(), id);
    if (!run_cmd(cmd, g_scratch, sizeof(g_scratch))) return 1;

    h->net_ready = strstr(g_scratch, "net: ready") != NULL;
    for (const char *p = g_scratch; (p = strstr(p, "[!]")); p += 3) h->error_lines++;
    return 1;
}

static int looks_healthy(const struct health *h) {
    return strcmp(h->status, "RUNNING") == 0 && h->net_ready && h->error_lines == 0;
}

static void tool_worker_logs(const cJSON *args) {
    cJSON *n = cJSON_GetObjectItemCaseSensitive(args, "name");
    if (!cJSON_IsString(n) || !valid_worker_name(n->valuestring)) {
        tool_error("name must be a swarm worker, e.g. cunningham-w1");
        return;
    }
    char id[64];
    if (!resolve_id(n->valuestring, id, sizeof(id))) { tool_error("no instance by that name exists"); return; }

    // Strip Firecracker's own API chatter; it is long, constant, and tells the
    // model nothing about the program's health.
    char cmd[900];
    snprintf(cmd, sizeof(cmd),
             "%s instances logs %s 2>/dev/null | grep -vE '^2026-|fc_api|fc_vcpu|anonymous-instance|^logs: task'",
             api_prefix(), id);
    if (!run_cmd(cmd, g_scratch, sizeof(g_scratch))) { tool_error("could not read logs"); return; }

    // Only the tail: serial output grows without bound and every byte of it
    // would become input tokens on every subsequent turn.
    size_t len = strlen(g_scratch);
    const char *tail = len > LOG_TAIL ? g_scratch + len - LOG_TAIL : g_scratch;

    struct health h;
    worker_health(n->valuestring, &h);

    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "worker", n->valuestring);
    cJSON_AddStringToObject(o, "status", h.status);
    cJSON_AddBoolToObject(o, "network_up", h.net_ready);
    cJSON_AddNumberToObject(o, "error_lines_in_log", h.error_lines);
    cJSON_AddStringToObject(o, "verdict", looks_healthy(&h) ? "healthy" : "needs attention");
    cJSON_AddStringToObject(o, "note",
        "Health is status + network + absence of errors. How many chains a worker has "
        "announced is NOT a health signal: finds of length 8+ are tens of minutes apart, "
        "and a worker that has found nothing may be working perfectly.");
    cJSON_AddStringToObject(o, "log_tail", tail);
    cJSON_AddBoolToObject(o, "truncated", len > LOG_TAIL);
    if (!cJSON_PrintPreallocated(o, g_result, (int)sizeof(g_result), 0))
        tool_error("log did not fit in the result buffer");
    cJSON_Delete(o);
}

// Bring a worker back to RUNNING and confirm it. Refuses outright if the worker
// is already healthy — the model cannot argue its way past this, which is the
// point: the guardrail lives in the code, not in the prompt.
//
// "reboot" on this API leaves the instance STOPPED, so a restart that ends
// stopped is followed by an explicit start. Making the tool responsible for the
// end state is what lets it verify its own work instead of hoping.
static void tool_restart_worker(const cJSON *args) {
    if (g_mutations_used >= MUTATION_BUDGET) {
        tool_error("corrective action budget spent for this run; report what you found instead");
        return;
    }
    cJSON *n = cJSON_GetObjectItemCaseSensitive(args, "name");
    if (!cJSON_IsString(n) || !valid_worker_name(n->valuestring)) {
        tool_error("name must be a swarm worker, e.g. cunningham-w1");
        return;
    }
    const char *name = n->valuestring;

    struct health before;
    if (!worker_health(name, &before) || !before.found) {
        tool_error("no instance by that name exists");
        return;
    }
    if (looks_healthy(&before)) {
        snprintf(g_result, sizeof(g_result),
                 "{\"refused\":true,\"worker\":\"%s\",\"status\":\"%s\",\"reason\":"
                 "\"This worker is RUNNING with its network up and no errors in its log. "
                 "Restarting it would discard its progress for no reason. Absence of recent "
                 "finds is not a fault. Report it as healthy.\"}",
                 name, before.status);
        return;                                   // costs no budget: nothing happened
    }

    char id[64];
    if (!resolve_id(name, id, sizeof(id))) { tool_error("could not resolve instance id"); return; }

    g_mutations_used++;
    char cmd[900];
    const char *verb = strcmp(before.status, "RUNNING") == 0 ? "reboot" : "start";
    snprintf(cmd, sizeof(cmd), "%s instances %s %s 2>&1 | tail -2", api_prefix(), verb, id);
    run_cmd(cmd, g_scratch, sizeof(g_scratch));

    sleep(6);
    struct health after;
    worker_health(name, &after);

    // A reboot that parked it in STOPPED is not a restart. Finish the job.
    if (strcmp(after.status, "RUNNING") != 0) {
        snprintf(cmd, sizeof(cmd), "%s instances start %s 2>&1 | tail -2", api_prefix(), id);
        run_cmd(cmd, g_scratch, sizeof(g_scratch));
        sleep(6);
        worker_health(name, &after);
    }

    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "worker", name);
    cJSON_AddStringToObject(o, "status_before", before.status);
    cJSON_AddStringToObject(o, "status_after", after.status);
    cJSON_AddBoolToObject(o, "now_running", strcmp(after.status, "RUNNING") == 0);
    cJSON_AddStringToObject(o, "next",
        "Confirm with list_workers, then report what you did and whether it worked.");
    if (!cJSON_PrintPreallocated(o, g_result, (int)sizeof(g_result), 0))
        tool_error("result did not fit");
    cJSON_Delete(o);
}

static size_t discard_cb(void *p, size_t s, size_t n, void *u) { (void)p; (void)u; return s * n; }

static int g_reported = 0;

static long send_report_text(const char *body) {
    const char *token = getenv("TELEGRAM_BOT_TOKEN");
    const char *chat  = getenv("TELEGRAM_CHAT_ID");
    if (!token || !chat) return 0;

    char text[3000];
    snprintf(text, sizeof(text), "\xF0\x9F\x91\x81 swarm supervisor\n\n%s", body);

    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "chat_id", chat);
    cJSON_AddStringToObject(req, "text", text);
    char *payload = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!payload) return 0;

    char url[512];
    snprintf(url, sizeof(url), "https://api.telegram.org/bot%s/sendMessage", token);

    CURL *h = curl_easy_init();
    long status = 0;
    if (h) {
        struct curl_slist *hdrs = curl_slist_append(NULL, "Content-Type: application/json");
        curl_easy_setopt(h, CURLOPT_URL, url);
        curl_easy_setopt(h, CURLOPT_POST, 1L);
        curl_easy_setopt(h, CURLOPT_POSTFIELDS, payload);
        curl_easy_setopt(h, CURLOPT_HTTPHEADER, hdrs);
        curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, discard_cb);
        curl_easy_setopt(h, CURLOPT_TIMEOUT, 30L);
        curl_easy_perform(h);
        curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
        curl_slist_free_all(hdrs);
        curl_easy_cleanup(h);
    }
    free(payload);
    printf("[+] report sent (HTTP %ld)\n", status);
    if (status && status < 400) g_reported = 1;
    return status;
}

static void tool_send_report(const cJSON *args) {
    cJSON *t = cJSON_GetObjectItemCaseSensitive(args, "text");
    if (!cJSON_IsString(t)) { tool_error("need 'text'"); return; }

    long status = send_report_text(t->valuestring);
    snprintf(g_result, sizeof(g_result),
             (status && status < 400) ? "{\"ok\":true,\"delivered\":true}"
                                      : "{\"error\":\"telegram rejected the report\"}");
}

static void call_tool(const char *name, const char *arguments_json) {
    cJSON *args = arguments_json ? cJSON_Parse(arguments_json) : NULL;

    if (strcmp(name, "list_workers") == 0)        tool_list_workers();
    else if (strcmp(name, "worker_logs") == 0)    { if (args) tool_worker_logs(args); else tool_error("bad arguments"); }
    else if (strcmp(name, "restart_worker") == 0) { if (args) tool_restart_worker(args); else tool_error("bad arguments"); }
    else if (strcmp(name, "send_report") == 0)    { if (args) tool_send_report(args); else tool_error("bad arguments"); }
    else snprintf(g_result, sizeof(g_result), "{\"error\":\"no such tool: %.40s\"}", name);

    if (args) cJSON_Delete(args);
}

// ---------------------------------------------------------------- llm

static struct { char *data; size_t size; } g_resp;

static size_t collect_cb(void *contents, size_t size, size_t nmemb, void *userp) {
    (void)userp;
    size_t total = size * nmemb;
    char *p = realloc(g_resp.data, g_resp.size + total + 1);
    if (!p) return 0;
    g_resp.data = p;
    memcpy(g_resp.data + g_resp.size, contents, total);
    g_resp.size += total;
    g_resp.data[g_resp.size] = '\0';
    return total;
}

static const char *llm_url(void) {
    const char *u = getenv("LLM_BASE_URL");
    return (u && *u) ? u : LLM_URL_DEFAULT;
}

static const char *llm_model(void) {
    const char *m = getenv("LLM_MODEL");
    return (m && *m) ? m : LLM_MODEL_DEFAULT;
}

// One attempt. Returns the assistant message, or NULL with *retryable set when
// the failure is the kind that going again might fix.
static cJSON *llm_attempt(const cJSON *messages, const char *key, int *retryable);

// Hosted models fail transiently — 503 "high demand", 429, and plain timeouts
// are routine, not exceptional. A supervisor that gives up on the first one is
// less reliable than the thing it supervises.
static cJSON *llm_turn(const cJSON *messages, const char *key) {
    int backoff = 3;
    for (int attempt = 1; attempt <= 5; attempt++) {
        int retryable = 0;
        cJSON *msg = llm_attempt(messages, key, &retryable);
        if (msg) return msg;
        if (!retryable) return NULL;
        if (attempt == 5) break;
        fprintf(stderr, "    [~] provider busy, retrying in %ds (attempt %d/5)\n", backoff, attempt);
        sleep((unsigned)backoff);
        backoff *= 2;
    }
    return NULL;
}

static cJSON *llm_attempt(const cJSON *messages, const char *key, int *retryable) {
    *retryable = 0;
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", llm_model());
    cJSON_AddItemToObject(root, "messages", cJSON_Duplicate(messages, 1));
    cJSON_AddItemToObject(root, "tools", cJSON_Parse(TOOLS_JSON));
    cJSON_AddStringToObject(root, "tool_choice", "auto");
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) return NULL;

    free(g_resp.data); g_resp.data = NULL; g_resp.size = 0;

    char auth[512];
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s", key ? key : "");

    CURL *h = curl_easy_init();
    if (!h) { free(payload); return NULL; }
    struct curl_slist *hdrs = curl_slist_append(NULL, "Content-Type: application/json");
    if (key && *key) hdrs = curl_slist_append(hdrs, auth);   // a local Ollama needs none
    curl_easy_setopt(h, CURLOPT_URL, llm_url());
    curl_easy_setopt(h, CURLOPT_POST, 1L);
    curl_easy_setopt(h, CURLOPT_POSTFIELDS, payload);
    curl_easy_setopt(h, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, collect_cb);
    curl_easy_setopt(h, CURLOPT_TIMEOUT, 180L);
    CURLcode res = curl_easy_perform(h);
    long status = 0;
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(h);
    free(payload);

    if (res != CURLE_OK) {
        fprintf(stderr, "[!] llm: %s\n", curl_easy_strerror(res));
        *retryable = 1;                       // timeouts and dropped connections
        return NULL;
    }
    if (status >= 400) {
        fprintf(stderr, "[!] llm HTTP %ld: %.200s\n", status, g_resp.data ? g_resp.data : "");
        *retryable = (status == 429 || status >= 500);   // rate limit or their fault
        return NULL;
    }

    cJSON *json = cJSON_Parse(g_resp.data);
    if (!json) { fprintf(stderr, "[!] llm returned unparseable JSON\n"); return NULL; }

    cJSON *choices = cJSON_GetObjectItemCaseSensitive(json, "choices");
    cJSON *msg = NULL;
    if (cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
        cJSON *m = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(choices, 0), "message");
        if (m) msg = cJSON_Duplicate(m, 1);
    }
    cJSON_Delete(json);
    return msg;
}

// ---------------------------------------------------------------- loop

#define SYSTEM_PROMPT \
    "You are the night-shift supervisor for a swarm of three prime-search workers " \
    "running as unikernels on BareMetal Cloud, named cunningham-w0, cunningham-w1 and " \
    "cunningham-w2.\n\n" \
    "Work from evidence. Start by listing the workers, then read the serial log of any " \
    "worker whose state you cannot explain. Choose each check because of what the last " \
    "one told you.\n\n" \
    "Health means: the instance is RUNNING, its network came up, and its log has no " \
    "error lines. That is all. How many chains a worker has announced is NOT evidence " \
    "about its health — finds of length 8 or more are tens of minutes apart, so a worker " \
    "that has found nothing is almost certainly working correctly. Each tool result " \
    "carries a computed verdict; trust it over your own impression, and note that " \
    "restart_worker will refuse to touch a healthy worker.\n\n" \
    "You may take at most one corrective action per run. If you take one, you must call " \
    "list_workers again afterwards to confirm it worked — never report a fix you have " \
    "not verified.\n\n" \
    "Finish by calling send_report exactly once: what you checked, what you found, what " \
    "you did if anything, and whether the swarm is healthy. Be specific and brief."

static void add_message(cJSON *msgs, const char *role, const char *content) {
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "role", role);
    cJSON_AddStringToObject(m, "content", content);
    cJSON_AddItemToArray(msgs, m);
}

int main(void) {
    // Line-buffer stdout: this program's whole value is the trace it prints,
    // and block buffering hides that until exit when output is redirected.
    setvbuf(stdout, NULL, _IOLBF, 0);

    const char *key = getenv("GEMINI_API_KEY");
    if ((!key || !*key) && !getenv("LLM_BASE_URL")) {
        fprintf(stderr, "[!] set GEMINI_API_KEY, or LLM_BASE_URL for a keyless provider\n");
        return 1;
    }
    printf("[+] supervisor: model=%s\n", llm_model());

    curl_global_init(CURL_GLOBAL_ALL);

    cJSON *messages = cJSON_CreateArray();
    add_message(messages, "system", SYSTEM_PROMPT);
    add_message(messages, "user", "Check the swarm. Report what you find.");

    for (int step = 1; step <= MAX_STEPS; step++) {
        printf("[*] step %d/%d\n", step, MAX_STEPS);

        cJSON *assistant = llm_turn(messages, key);
        if (!assistant) { fprintf(stderr, "[!] no answer from the model\n"); break; }

        cJSON_AddItemToArray(messages, cJSON_Duplicate(assistant, 1));

        cJSON *calls   = cJSON_GetObjectItemCaseSensitive(assistant, "tool_calls");
        cJSON *content = cJSON_GetObjectItemCaseSensitive(assistant, "content");

        if (!cJSON_IsArray(calls) || cJSON_GetArraySize(calls) == 0) {
            const char *final = cJSON_IsString(content) ? content->valuestring : "(nothing said)";
            printf("[+] done: %s\n", final);
            // The operator hears about every run, whether or not the model
            // remembered to call send_report. A supervisor whose findings depend
            // on the model's diligence is not a supervisor.
            if (!g_reported && cJSON_IsString(content) && *content->valuestring) {
                printf("[~] model finished without reporting; sending its conclusion anyway\n");
                send_report_text(final);
            }
            cJSON_Delete(assistant);
            break;
        }
        if (cJSON_IsString(content) && *content->valuestring)
            printf("    thinking: %s\n", content->valuestring);

        cJSON *tc = NULL;
        cJSON_ArrayForEach(tc, calls) {
            cJSON *id = cJSON_GetObjectItemCaseSensitive(tc, "id");
            cJSON *fn = cJSON_GetObjectItemCaseSensitive(tc, "function");
            cJSON *nm = fn ? cJSON_GetObjectItemCaseSensitive(fn, "name") : NULL;
            cJSON *ar = fn ? cJSON_GetObjectItemCaseSensitive(fn, "arguments") : NULL;
            if (!cJSON_IsString(nm)) continue;

            printf("    -> %s(%s)\n", nm->valuestring, cJSON_IsString(ar) ? ar->valuestring : "");
            call_tool(nm->valuestring, cJSON_IsString(ar) ? ar->valuestring : NULL);
            printf("    <- %.300s%s\n", g_result, strlen(g_result) > 300 ? "..." : "");

            cJSON *tm = cJSON_CreateObject();
            cJSON_AddStringToObject(tm, "role", "tool");
            if (cJSON_IsString(id)) cJSON_AddStringToObject(tm, "tool_call_id", id->valuestring);
            cJSON_AddStringToObject(tm, "name", nm->valuestring);
            cJSON_AddStringToObject(tm, "content", g_result);
            cJSON_AddItemToArray(messages, tm);
        }
        cJSON_Delete(assistant);
    }

    printf("[+] run complete: %d corrective action(s) taken\n", g_mutations_used);
    cJSON_Delete(messages);
    free(g_resp.data);
    curl_global_cleanup();
    return 0;
}
