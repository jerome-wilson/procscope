/*
 * ProcScope - Real-Time Process & Memory Monitor
 * ai_insights.c — Real Claude AI integration via popen() + curl
 *
 * System calls demonstrated here:
 *   popen() / pclose() — spawn curl subprocess and read its stdout
 *   getenv()           — read ANTHROPIC_API_KEY from the environment
 *   fopen() / fwrite() — write JSON request body to a temp file
 *   unlink()           — clean up the temp file after use
 */

#include "procscope.h"

#define AI_MAX_RESPONSE  8192
#define AI_WRAP_WIDTH    70
#define AI_TOP_N         10
#define AI_REQ_FILE      "/tmp/procscope_ai_req.json"
#define AI_MODEL         "claude-haiku-4-5"

/* ════════════════════════════════════════════════════════════════════════
 * Internal helpers
 * ════════════════════════════════════════════════════════════════════════ */

/* JSON-escape src into buf.  Returns bytes written (not including NUL). */
static int json_escape(const char *src, char *buf, size_t bufsize) {
    size_t pos = 0;
    for (; *src && pos + 6 < bufsize; src++) {
        unsigned char c = (unsigned char)*src;
        if      (c == '"')  { buf[pos++] = '\\'; buf[pos++] = '"'; }
        else if (c == '\\') { buf[pos++] = '\\'; buf[pos++] = '\\'; }
        else if (c == '\n') { buf[pos++] = '\\'; buf[pos++] = 'n'; }
        else if (c == '\r') { buf[pos++] = '\\'; buf[pos++] = 'r'; }
        else if (c == '\t') { buf[pos++] = '\\'; buf[pos++] = 't'; }
        else if (c < 0x20)  { buf[pos++] = ' '; }
        else                 { buf[pos++] = c; }
    }
    buf[pos] = '\0';
    return (int)pos;
}

/* Word-wrap text and print each line with a 2-space indent. */
static void print_wrapped_text(const char *text, int width) {
    int col = 0;
    int at_line_start = 1;
    const char *p = text;

    while (*p) {
        if (*p == '\n') {
            printf("\n");
            col = 0;
            at_line_start = 1;
            p++;
            continue;
        }
        if (*p == ' ' || *p == '\t') {
            p++;
            continue;
        }
        /* Scan word */
        const char *word = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
        int wlen = (int)(p - word);

        if (at_line_start) {
            printf("  ");
            fwrite(word, 1, wlen, stdout);
            col = wlen;
            at_line_start = 0;
        } else if (col + 1 + wlen > width) {
            printf("\n  ");
            fwrite(word, 1, wlen, stdout);
            col = wlen;
        } else {
            printf(" ");
            fwrite(word, 1, wlen, stdout);
            col += 1 + wlen;
        }
    }
    if (!at_line_start) printf("\n");
}

/* Extract the value of the first "text":"..." key in a JSON string.
   Returns bytes written, or -1 if not found. */
static int extract_text_field(const char *json, char *output, size_t output_size) {
    const char *pos = strstr(json, "\"text\":\"");
    if (!pos) return -1;
    pos += strlen("\"text\":\"");

    size_t i = 0;
    while (*pos && i + 1 < output_size) {
        if (*pos == '\\') {
            pos++;
            switch (*pos) {
                case '"':  output[i++] = '"';  break;
                case '\\': output[i++] = '\\'; break;
                case '/':  output[i++] = '/';  break;
                case 'n':  output[i++] = '\n'; break;
                case 'r':  output[i++] = '\r'; break;
                case 't':  output[i++] = '\t'; break;
                default:   if (*pos) output[i++] = *pos; break;
            }
            if (*pos) pos++;
        } else if (*pos == '"') {
            break;
        } else {
            output[i++] = *pos++;
        }
    }
    output[i] = '\0';
    return (int)i;
}

/* ════════════════════════════════════════════════════════════════════════
 * Core API call via popen() + curl
 * ════════════════════════════════════════════════════════════════════════ */

static int call_claude_api(char *output, size_t output_size) {
    /* Use $ANTHROPIC_API_KEY directly so the shell expands it — avoids
       any escaping issues with the key value itself. */
    const char *curl_cmd =
        "curl -s -X POST https://api.anthropic.com/v1/messages"
        " -H \"x-api-key: $ANTHROPIC_API_KEY\""
        " -H \"anthropic-version: 2023-06-01\""
        " -H \"content-type: application/json\""
        " -d @" AI_REQ_FILE;

    FILE *fp = popen(curl_cmd, "r");
    if (!fp) {
        snprintf(output, output_size,
                 "Error: popen() failed — is curl installed?");
        return -1;
    }

    static char resp_buf[AI_MAX_RESPONSE];
    size_t total = 0, n;
    while ((n = fread(resp_buf + total, 1,
                      sizeof(resp_buf) - total - 1, fp)) > 0)
        total += n;
    resp_buf[total] = '\0';
    pclose(fp);

    if (total == 0) {
        snprintf(output, output_size,
                 "No response from API — check network and ANTHROPIC_API_KEY.");
        return -1;
    }

    /* Detect API-level error */
    if (strstr(resp_buf, "\"type\":\"error\"")) {
        const char *msg_start = strstr(resp_buf, "\"message\":\"");
        if (msg_start) {
            msg_start += strlen("\"message\":\"");
            char err[256] = {0};
            size_t j = 0;
            while (*msg_start && *msg_start != '"' && j < sizeof(err) - 1)
                err[j++] = *msg_start++;
            err[j] = '\0';
            snprintf(output, output_size, "API error: %s", err);
        } else {
            snprintf(output, output_size,
                     "API error — check your API key (%.120s)", resp_buf);
        }
        return -1;
    }

    if (extract_text_field(resp_buf, output, output_size) <= 0) {
        snprintf(output, output_size,
                 "Could not parse API response (%.120s...)", resp_buf);
        return -1;
    }

    return 0;
}

/* ════════════════════════════════════════════════════════════════════════
 * Build request and run the insight query
 * ════════════════════════════════════════════════════════════════════════ */

int get_ai_process_insight(ProcessData *procs, int count,
                            SystemMemory *mem,
                            char *output, size_t output_size) {
    if (!getenv("ANTHROPIC_API_KEY") || getenv("ANTHROPIC_API_KEY")[0] == '\0') {
        snprintf(output, output_size,
            "ANTHROPIC_API_KEY is not set.\n"
            "Export your key first:  export ANTHROPIC_API_KEY=sk-ant-...");
        return -1;
    }

    /* ── Build the human-readable prompt ── */
    static char prompt_buf[4096];
    int plen = 0;

    double total_gb      = mem ? mem->total_kb     / (1024.0 * 1024.0) : 0;
    double used_gb       = mem ? mem->used_kb      / (1024.0 * 1024.0) : 0;
    double pct           = (total_gb > 0) ? (used_gb / total_gb * 100.0) : 0;
    double swap_used_mb  = mem ? mem->swap_used_kb  / 1024.0 : 0;
    double swap_total_mb = mem ? mem->swap_total_kb / 1024.0 : 0;

    plen += snprintf(prompt_buf + plen, sizeof(prompt_buf) - plen,
        "You are a macOS systems performance analyst. "
        "Analyze this live process snapshot and give a focused "
        "3-5 sentence insight: identify the top concern, "
        "flag any anomalies or unusually high resource usage, "
        "and give one concrete actionable recommendation. "
        "Be specific — use process names and numbers.\n\n"
        "System: %.1fGB / %.1fGB RAM used (%.0f%%), "
        "swap %.0fMB / %.0fMB\n\n"
        "Top processes by CPU:\n"
        "Rank  Name                  PID      CPU%%    RSS(MB)  Threads\n",
        used_gb, total_gb, pct, swap_used_mb, swap_total_mb);

    int n = (count < AI_TOP_N) ? count : AI_TOP_N;
    for (int i = 0; i < n; i++) {
        if (!procs[i].valid) continue;
        plen += snprintf(prompt_buf + plen, sizeof(prompt_buf) - plen,
            "%-5d %-21.21s %-8d %6.1f%%  %8.1f  %d\n",
            i + 1, procs[i].name, procs[i].pid,
            procs[i].cpu_percent, procs[i].rss_kb / 1024.0,
            procs[i].thread_count);
    }

    /* Top 5 by RSS */
    ProcessData rss_top[AI_TOP_N];
    int rss_n = (count < AI_TOP_N) ? count : AI_TOP_N;
    memcpy(rss_top, procs, sizeof(ProcessData) * rss_n);
    for (int i = 0; i < rss_n - 1; i++) {
        int best = i;
        for (int j = i + 1; j < rss_n; j++)
            if (rss_top[j].rss_kb > rss_top[best].rss_kb) best = j;
        if (best != i) {
            ProcessData tmp = rss_top[i];
            rss_top[i] = rss_top[best];
            rss_top[best] = tmp;
        }
    }

    int show_rss = (rss_n < 5) ? rss_n : 5;
    plen += snprintf(prompt_buf + plen, sizeof(prompt_buf) - plen,
        "\nTop 5 by RSS:\n"
        "Rank  Name                  PID      RSS(MB)  CPU%%\n");
    for (int i = 0; i < show_rss; i++) {
        if (!rss_top[i].valid) continue;
        plen += snprintf(prompt_buf + plen, sizeof(prompt_buf) - plen,
            "%-5d %-21.21s %-8d %8.1f  %.1f%%\n",
            i + 1, rss_top[i].name, rss_top[i].pid,
            rss_top[i].rss_kb / 1024.0, rss_top[i].cpu_percent);
    }

    /* ── JSON-escape the prompt and build request body ── */
    static char escaped_prompt[8192];
    json_escape(prompt_buf, escaped_prompt, sizeof(escaped_prompt));

    static char json_body[9216];
    snprintf(json_body, sizeof(json_body),
        "{\"model\":\"%s\",\"max_tokens\":512,"
        "\"messages\":[{\"role\":\"user\",\"content\":\"%s\"}]}",
        AI_MODEL, escaped_prompt);

    /* ── Write request to temp file ── */
    FILE *req_file = fopen(AI_REQ_FILE, "w");
    if (!req_file) {
        snprintf(output, output_size,
                 "Error: cannot write temp file " AI_REQ_FILE);
        return -1;
    }
    fputs(json_body, req_file);
    fclose(req_file);

    /* ── Call the API ── */
    int rc = call_claude_api(output, output_size);
    unlink(AI_REQ_FILE);
    return rc;
}

/* ════════════════════════════════════════════════════════════════════════
 * Display
 * ════════════════════════════════════════════════════════════════════════ */

void display_ai_response(const char *text) {
    const char *sep =
        "──────────────────────────────────────────────────────────────────────";

    printf("\n");
    printf("  %s%s Claude AI Process Insights%s  %s[%s]%s\n",
           COLOR_BOLD, COLOR_MAGENTA, COLOR_RESET,
           COLOR_DIM, AI_MODEL, COLOR_RESET);
    printf("  %s%s%s\n\n", COLOR_CYAN, sep, COLOR_RESET);
    print_wrapped_text(text, AI_WRAP_WIDTH);
    printf("\n  %s%s%s\n", COLOR_CYAN, sep, COLOR_RESET);
}

/* ════════════════════════════════════════════════════════════════════════
 * Standalone 'insights' command
 * ════════════════════════════════════════════════════════════════════════ */

int run_insights_mode(void) {
    pid_t pids[MAX_PROCESSES];
    int count = get_all_pids(pids, MAX_PROCESSES);
    if (count <= 0) {
        fprintf(stderr, "Error: cannot read process list\n");
        return 1;
    }
    if (count > MAX_PROCESSES) count = MAX_PROCESSES;

    ProcessData procs[MAX_PROCESSES];
    for (int i = 0; i < count; i++)
        get_process_data(pids[i], &procs[i]);

    /* Sort by RSS for a static (no-CPU-delta) snapshot */
    for (int i = 0; i < count - 1 && i < AI_TOP_N; i++) {
        int best = i;
        for (int j = i + 1; j < count; j++)
            if (procs[j].rss_kb > procs[best].rss_kb) best = j;
        if (best != i) {
            ProcessData tmp = procs[i];
            procs[i] = procs[best];
            procs[best] = tmp;
        }
    }

    SystemMemory mem;
    get_system_memory(&mem);

    print_header();
    printf("\n  %s%s Analyzing system — querying Claude AI...%s\n\n",
           COLOR_BOLD, COLOR_CYAN, COLOR_RESET);
    fflush(stdout);

    static char response[AI_MAX_RESPONSE];
    int top_n = (count < AI_TOP_N) ? count : AI_TOP_N;
    int result = get_ai_process_insight(procs, top_n, &mem,
                                        response, sizeof(response));
    if (result == 0) {
        display_ai_response(response);
    } else {
        printf("  %s%s%s\n", COLOR_RED, response, COLOR_RESET);
    }
    printf("\n");
    return result == 0 ? 0 : 1;
}
