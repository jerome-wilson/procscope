/*
 * ProcScope - Real-Time Process & Memory Monitor
 * monitor.c — TUI display, live watch/top modes, keyboard handling
 *
 * System calls used:
 *   fork() + pipe() + waitpid() — parallel per-PID data collection workers
 *   select()                   — I/O multiplexing: keyboard + refresh timer
 *   tcgetattr/tcsetattr        — raw terminal mode (no echo, no line buffer)
 *   read()                     — keyboard input in raw mode
 */

#include "procscope.h"
#include <termios.h>
#include <sys/ioctl.h>

/* ════════════════════════════════════════════════════════════════════════
 * Terminal raw mode
 * ════════════════════════════════════════════════════════════════════════ */

static struct termios g_orig_termios;
static int g_raw_mode = 0;

static void restore_terminal(void) {
    if (g_raw_mode) {
        printf("\033[?1049l");   /* exit alternate screen buffer */
        fflush(stdout);
        tcsetattr(STDIN_FILENO, TCSANOW, &g_orig_termios);
        g_raw_mode = 0;
    }
}

static void setup_raw_terminal(void) {
    if (!isatty(STDIN_FILENO)) return;
    tcgetattr(STDIN_FILENO, &g_orig_termios);
    atexit(restore_terminal);
    struct termios raw = g_orig_termios;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    printf("\033[?1049h");       /* enter alternate screen buffer */
    fflush(stdout);
    g_raw_mode = 1;
}

/* ════════════════════════════════════════════════════════════════════════
 * Sparkline renderer (block elements ▁▂▃▄▅▆▇█)
 * ════════════════════════════════════════════════════════════════════════ */

static void render_sparkline(PriceHistory *history, char *buf, size_t size) {
    static const char *BARS[8] = {"▁","▂","▃","▄","▅","▆","▇","█"};
    int n = history->count < 12 ? history->count : 12;
    if (n == 0) {
        snprintf(buf, size, "            ");
        return;
    }

    double mn = history->prices[0], mx = history->prices[0];
    for (int i = 1; i < n; i++) {
        if (history->prices[i] < mn) mn = history->prices[i];
        if (history->prices[i] > mx) mx = history->prices[i];
    }

    size_t pos = 0;
    double range = mx - mn;
    for (int i = 0; i < n; i++) {
        int level = (range > 0.001)
                    ? (int)(((history->prices[i] - mn) / range) * 7.0)
                    : 3;
        if (level < 0) level = 0;
        if (level > 7) level = 7;
        const char *bar = BARS[level];
        size_t blen = strlen(bar);
        if (pos + blen + 1 >= size) break;
        memcpy(buf + pos, bar, blen);
        pos += blen;
    }
    /* Pad to 12 display columns */
    int spaces = 12 - n;
    for (int i = 0; i < spaces && pos + 1 < size; i++)
        buf[pos++] = ' ';
    buf[pos] = '\0';
}

/* ════════════════════════════════════════════════════════════════════════
 * Sort helpers
 * ════════════════════════════════════════════════════════════════════════ */

static SortMode g_sort = SORT_CPU;

static int cmp_cpu(const void *a, const void *b) {
    const ProcessData *pa = (const ProcessData *)a;
    const ProcessData *pb = (const ProcessData *)b;
    if (pb->cpu_percent > pa->cpu_percent) return 1;
    if (pb->cpu_percent < pa->cpu_percent) return -1;
    return 0;
}
static int cmp_rss(const void *a, const void *b) {
    const ProcessData *pa = (const ProcessData *)a;
    const ProcessData *pb = (const ProcessData *)b;
    return (int)(pb->rss_kb - pa->rss_kb);
}
static int cmp_pid(const void *a, const void *b) {
    return ((const ProcessData *)a)->pid - ((const ProcessData *)b)->pid;
}
static int cmp_name(const void *a, const void *b) {
    return strcmp(((const ProcessData *)a)->name,
                  ((const ProcessData *)b)->name);
}

static void sort_processes(ProcessData *procs, int count, SortMode mode) {
    switch (mode) {
        case SORT_CPU:  qsort(procs, count, sizeof(*procs), cmp_cpu);  break;
        case SORT_RSS:  qsort(procs, count, sizeof(*procs), cmp_rss);  break;
        case SORT_PID:  qsort(procs, count, sizeof(*procs), cmp_pid);  break;
        case SORT_NAME: qsort(procs, count, sizeof(*procs), cmp_name); break;
    }
}

/* ════════════════════════════════════════════════════════════════════════
 * Keyboard handler
 * ════════════════════════════════════════════════════════════════════════ */

static void handle_keypress(char c, ProcessData *procs, PriceHistory *histories,
                             int count, int *interval, int *paused,
                             int *show_insights, int *insight_idx,
                             SortMode *sort, int *needs_ai) {
    switch (c) {
        case 'q': case 'Q':
            keep_running = 0;
            break;
        case 's': case 'S':
            /* Cycle sort: CPU → RSS → PID → NAME → CPU */
            *sort = (SortMode)((*sort + 1) % 4);
            g_sort = *sort;
            sort_processes(procs, count, *sort);
            break;
        case '+': case '=':
            *interval -= 1;
            if (*interval < 1) *interval = 1;
            break;
        case '-':
            *interval += 1;
            if (*interval > 30) *interval = 30;
            break;
        case 'p': case 'P':
            *paused = !(*paused);
            break;
        case 'i': case 'I':
            if (!(*show_insights)) {
                *show_insights = 1;
                *insight_idx   = 0;
            } else {
                *insight_idx = (*insight_idx + 1) % count;
            }
            break;
        case 'a': case 'A':
            *needs_ai = 1;
            break;
        case '\033':
            *show_insights = 0;
            break;
        default:
            break;
    }
    (void)histories;
}

/* ════════════════════════════════════════════════════════════════════════
 * display_process_table — render the TUI table
 * ════════════════════════════════════════════════════════════════════════ */

void display_process_table(ProcessData *procs, PriceHistory *histories,
                           int count, SortMode sort) {
    static const char *sort_labels[] = { "CPU%", "RSS", "PID", "NAME" };

    clear_screen();

    /* Header row */
    printf("  %s%-8s  %-22s  %7s  %9s  %9s  %5s  %-12s  %s%s\n",
           COLOR_BOLD,
           "PID", "NAME", "CPU%", "RSS(MB)", "VSIZE(MB)", "FDS",
           "CHART", "TREND", COLOR_RESET);

    /* Separator: 2-space indent + 85 dashes */
    printf("  %s%s%s\n", COLOR_CYAN,
           "─────────────────────────────────────────────────────────────────────────────────────",
           COLOR_RESET);

    for (int i = 0; i < count; i++) {
        ProcessData *p = &procs[i];
        if (!p->valid) continue;

        /* CPU colour: red > 50%, yellow > 20%, green otherwise */
        const char *cpu_color;
        if (p->cpu_percent > 50.0)      cpu_color = COLOR_RED;
        else if (p->cpu_percent > 20.0) cpu_color = COLOR_YELLOW;
        else                             cpu_color = COLOR_GREEN;

        /* RSS colour: red > 1 GB, yellow > 256 MB */
        const char *rss_color;
        if      (p->rss_kb > 1024*1024) rss_color = COLOR_RED;
        else if (p->rss_kb > 256*1024)  rss_color = COLOR_YELLOW;
        else                             rss_color = COLOR_GREEN;

        /* Sparkline on CPU history */
        char chart_buf[128] = "            ";
        if (histories)
            render_sparkline(&histories[i], chart_buf, sizeof(chart_buf));

        double rss_mb   = p->rss_kb   / 1024.0;
        double vsize_mb = p->vsize_kb / 1024.0;

        /* Arrow based on CPU trend */
        const char *arrow = (histories && histories[i].count >= 2 &&
                             histories[i].prices[histories[i].count > 0
                               ? (histories[i].index - 1 + MAX_PRICE_HISTORY) % MAX_PRICE_HISTORY
                               : 0] >
                             histories[i].prices[histories[i].count > 1
                               ? (histories[i].index - 2 + MAX_PRICE_HISTORY) % MAX_PRICE_HISTORY
                               : 0])
                            ? ARROW_UP : ARROW_DOWN;

        printf("\n");
        printf("  %s%-8d%s", COLOR_CYAN, p->pid, COLOR_RESET);
        printf("  %-22.22s", p->name);
        printf("  %s%7.1f%s", cpu_color, p->cpu_percent, COLOR_RESET);
        printf("  %s%9.1f%s", rss_color, rss_mb,   COLOR_RESET);
        printf("  %9.1f",     vsize_mb);
        printf("  %5d",       p->fd_count);
        printf("  %s%s%s",    cpu_color, chart_buf, COLOR_RESET);
        printf("     %s\n",   arrow);
    }

    printf("\n");
    printf("  %s%s%s\n", COLOR_CYAN,
           "─────────────────────────────────────────────────────────────────────────────────────",
           COLOR_RESET);
    printf("  Sort: %s%s%s\n\n", COLOR_YELLOW, sort_labels[sort], COLOR_RESET);
}

/* ════════════════════════════════════════════════════════════════════════
 * Parallel data collection via fork() + pipe()
 *
 * For each PID, fork a child that calls get_process_data() and writes
 * a ProcessData struct back through a pipe. Parent collects all results.
 * ════════════════════════════════════════════════════════════════════════ */

static int collect_parallel(pid_t *pids, ProcessData *prev,
                             ProcessData *curr, int count) {
    int pipes[MAX_WATCH_PIDS][2];
    pid_t children[MAX_WATCH_PIDS];

    /* Fork one worker per PID */
    for (int i = 0; i < count; i++) {
        if (pipe(pipes[i]) < 0) { pipes[i][0] = pipes[i][1] = -1; continue; }
        children[i] = fork();
        if (children[i] == 0) {
            /* Child: collect data and write to pipe */
            close(pipes[i][0]);
            ProcessData pd;
            get_process_data(pids[i], &pd);
            if (prev && prev[i].valid)
                compute_cpu_percent(&prev[i], &pd);
            write(pipes[i][1], &pd, sizeof(pd));
            close(pipes[i][1]);
            _exit(0);
        }
        close(pipes[i][1]);
    }

    /* Parent: read results and wait for children */
    for (int i = 0; i < count; i++) {
        if (pipes[i][0] < 0) { curr[i].valid = 0; continue; }
        ssize_t n = read(pipes[i][0], &curr[i], sizeof(ProcessData));
        close(pipes[i][0]);
        if (n != (ssize_t)sizeof(ProcessData)) curr[i].valid = 0;
        int status;
        waitpid(children[i], &status, 0);
    }
    return 0;
}

/* ════════════════════════════════════════════════════════════════════════
 * run_list_mode — one-shot process list (no TUI)
 * ════════════════════════════════════════════════════════════════════════ */

int run_list_mode(void) {
    pid_t pids[MAX_PROCESSES];
    int count = get_all_pids(pids, MAX_PROCESSES);
    if (count <= 0) { fprintf(stderr, "Error reading process list\n"); return 1; }

    ProcessData procs[MAX_PROCESSES];
    for (int i = 0; i < count; i++)
        get_process_data(pids[i], &procs[i]);

    /* Sort by RSS descending for the static list */
    sort_processes(procs, count, SORT_RSS);

    printf("  %s%-8s  %-28s  %9s  %9s  %5s%s\n",
           COLOR_BOLD, "PID", "NAME", "RSS(MB)", "VSIZE(MB)", "FDS", COLOR_RESET);
    printf("  %s%s%s\n", COLOR_CYAN,
           "─────────────────────────────────────────────────────────────",
           COLOR_RESET);

    for (int i = 0; i < count; i++) {
        if (!procs[i].valid) continue;
        printf("  %-8d  %-28.28s  %9.1f  %9.1f  %5d\n",
               procs[i].pid, procs[i].name,
               procs[i].rss_kb / 1024.0,
               procs[i].vsize_kb / 1024.0,
               procs[i].fd_count);
    }
    printf("\n  %d processes\n", count);
    return 0;
}

/* ════════════════════════════════════════════════════════════════════════
 * run_top_mode — live TUI of top N processes by CPU or RSS
 *
 * Each refresh:
 *  1. get_all_pids()
 *  2. collect_parallel() — fork N workers, read results over pipes
 *  3. sort + display_process_table()
 *  4. select() with 1-second timeout for keyboard input
 * ════════════════════════════════════════════════════════════════════════ */

int run_top_mode(ParsedCommand *cmd) {
    int   top_n   = cmd->top_n > 0 ? cmd->top_n : 10;
    int   current_interval = cmd->refresh_interval;
    int   paused  = 0;
    int   show_insights = 0;
    int   insight_idx   = 0;
    int   needs_ai      = 0;
    int   display_count = 0;
    SortMode sort = cmd->sort_mode;
    g_sort = sort;

    ProcessData prev[MAX_PROCESSES], curr[MAX_PROCESSES];
    PriceHistory histories[MAX_PROCESSES];
    pid_t pids[MAX_PROCESSES];

    memset(prev, 0, sizeof(prev));
    for (int i = 0; i < MAX_PROCESSES; i++)
        init_price_history(&histories[i]);

    setup_raw_terminal();

    while (keep_running) {
        if (!paused) {
            int count = get_all_pids(pids, MAX_PROCESSES);
            if (count > MAX_PROCESSES) count = MAX_PROCESSES;

            /* First pass: populate curr without CPU% (need two samples) */
            collect_parallel(pids, prev, curr, count);

            sort_processes(curr, count, sort);

            display_count = count < top_n ? count : top_n;

            /* Update CPU history for sparklines */
            for (int i = 0; i < display_count; i++) {
                if (curr[i].valid)
                    add_price(&histories[i], curr[i].cpu_percent);
            }

            display_process_table(curr, histories, display_count, sort);

            /* ── Insights panel ── */
            if (show_insights && display_count > 0) {
                int idx = insight_idx % display_count;
                if (histories[idx].count >= 3) {
                    AIInsight insight;
                    analyze_stock(&histories[idx], &insight);
                    print_ai_insight(&insight, curr[idx].name);
                    printf("  %s[I]%s Next process  %s[Esc]%s Close\n\n",
                           COLOR_BOLD, COLOR_RESET, COLOR_BOLD, COLOR_RESET);
                } else {
                    printf("  %s(Not enough history for %s yet)%s\n"
                           "  %s[I]%s Next process  %s[Esc]%s Close\n\n",
                           COLOR_YELLOW, curr[idx].name, COLOR_RESET,
                           COLOR_BOLD, COLOR_RESET, COLOR_BOLD, COLOR_RESET);
                }
            }

            /* Keybindings hint */
            printf("  %s[Q]%s Quit  %s[S]%s Sort  "
                   "%s[+]%s Faster  %s[-]%s Slower  %s[P]%s Pause  "
                   "%s[I]%s Insights  %s[A]%s AI"
                   "  — refresh: %s%ds%s\n",
                   COLOR_BOLD, COLOR_RESET,
                   COLOR_BOLD, COLOR_RESET,
                   COLOR_BOLD, COLOR_RESET,
                   COLOR_BOLD, COLOR_RESET,
                   COLOR_BOLD, COLOR_RESET,
                   COLOR_BOLD, COLOR_RESET,
                   COLOR_BOLD, COLOR_RESET,
                   COLOR_CYAN, current_interval, COLOR_RESET);

            /* Save curr as prev for next CPU delta */
            memcpy(prev, curr, sizeof(ProcessData) * count);
        }

        /* Keyboard-aware sleep: select() with 1s slices */
        for (int elapsed = 0; elapsed < current_interval && keep_running; elapsed++) {
            struct timeval tv = { 1, 0 };
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(STDIN_FILENO, &fds);
            if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
                char ch = 0;
                if (read(STDIN_FILENO, &ch, 1) == 1) {
                    int dc = top_n;
                    handle_keypress(ch, curr, histories, dc,
                                    &current_interval, &paused,
                                    &show_insights, &insight_idx, &sort,
                                    &needs_ai);
                    if (needs_ai) break;
                }
            }
        }

        /* ── AI overlay ── */
        if (needs_ai && display_count > 0) {
            clear_screen();
            printf("\n  %s%s Querying Claude AI — please wait...%s\n\n",
                   COLOR_BOLD, COLOR_YELLOW, COLOR_RESET);
            fflush(stdout);

            SystemMemory mem;
            get_system_memory(&mem);
            static char ai_buf[8192];
            int ok = get_ai_process_insight(curr, display_count, &mem,
                                            ai_buf, sizeof(ai_buf));
            if (ok == 0) {
                display_ai_response(ai_buf);
            } else {
                printf("\n  %s%s%s\n", COLOR_RED, ai_buf, COLOR_RESET);
            }

            printf("\n  %s[Any key]%s Dismiss\n", COLOR_BOLD, COLOR_RESET);
            fflush(stdout);

            char dismiss = 0;
            while (keep_running && dismiss == 0) {
                struct timeval tv = { 1, 0 };
                fd_set fds;
                FD_ZERO(&fds);
                FD_SET(STDIN_FILENO, &fds);
                if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0)
                    read(STDIN_FILENO, &dismiss, 1);
            }
            needs_ai = 0;
            continue;
        }

        /* Paused: keep reading keys until unpaused */
        while (paused && keep_running) {
            struct timeval tv = { 1, 0 };
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(STDIN_FILENO, &fds);
            if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
                char ch = 0;
                if (read(STDIN_FILENO, &ch, 1) == 1) {
                    int dc = top_n;
                    handle_keypress(ch, curr, histories, dc,
                                    &current_interval, &paused,
                                    &show_insights, &insight_idx, &sort,
                                    &needs_ai);
                }
            }
        }
    }

    restore_terminal();
    return 0;
}

/* ════════════════════════════════════════════════════════════════════════
 * run_watch_mode — live TUI for specific PIDs / process names
 * ════════════════════════════════════════════════════════════════════════ */

int run_watch_mode(ParsedCommand *cmd) {
    /* Resolve names to PIDs */
    pid_t watch_pids[MAX_WATCH_PIDS];
    int   watch_count = 0;

    for (int i = 0; i < cmd->target_count && watch_count < MAX_WATCH_PIDS; i++) {
        if (cmd->pids[i] > 0) {
            watch_pids[watch_count++] = cmd->pids[i];
        } else if (cmd->names[i][0]) {
            pid_t found[MAX_WATCH_PIDS];
            int   n = find_pids_by_name(cmd->names[i], found, MAX_WATCH_PIDS);
            for (int j = 0; j < n && watch_count < MAX_WATCH_PIDS; j++)
                watch_pids[watch_count++] = found[j];
        }
    }

    if (watch_count == 0) {
        fprintf(stderr, "Error: no matching processes found\n");
        return 1;
    }

    ProcessData prev[MAX_WATCH_PIDS], curr[MAX_WATCH_PIDS];
    PriceHistory histories[MAX_WATCH_PIDS];
    memset(prev, 0, sizeof(prev));
    for (int i = 0; i < watch_count; i++)
        init_price_history(&histories[i]);

    int current_interval = cmd->refresh_interval;
    int paused           = 0;
    int show_insights    = 0;
    int insight_idx      = 0;
    int needs_ai         = 0;
    SortMode sort        = SORT_CPU;

    setup_raw_terminal();

    while (keep_running) {
        if (!paused) {
            collect_parallel(watch_pids, prev, curr, watch_count);

            /* Remove stale (exited) processes */
            int alive = 0;
            for (int i = 0; i < watch_count; i++) {
                if (curr[i].valid) {
                    if (i != alive) {
                        curr[alive] = curr[i];
                        histories[alive] = histories[i];
                        watch_pids[alive] = watch_pids[i];
                    }
                    add_price(&histories[alive], curr[alive].cpu_percent);
                    alive++;
                }
            }
            watch_count = alive;

            if (watch_count == 0) {
                clear_screen();
                printf("\n  All watched processes have exited. Press Q to quit.\n");
            } else {
                display_process_table(curr, histories, watch_count, sort);

                if (show_insights) {
                    int idx = insight_idx % watch_count;
                    if (histories[idx].count >= 3) {
                        AIInsight insight;
                        analyze_stock(&histories[idx], &insight);
                        print_ai_insight(&insight, curr[idx].name);
                        printf("  %s[I]%s Next process  %s[Esc]%s Close\n\n",
                               COLOR_BOLD, COLOR_RESET, COLOR_BOLD, COLOR_RESET);
                    } else {
                        printf("  %s(Not enough history for %s yet)%s\n"
                               "  %s[I]%s Next process  %s[Esc]%s Close\n\n",
                               COLOR_YELLOW, curr[idx].name, COLOR_RESET,
                               COLOR_BOLD, COLOR_RESET, COLOR_BOLD, COLOR_RESET);
                    }
                }

                printf("  %s[Q]%s Quit  %s[S]%s Sort  "
                       "%s[+]%s Faster  %s[-]%s Slower  %s[P]%s Pause  "
                       "%s[I]%s Insights  %s[A]%s AI"
                       "  — refresh: %s%ds%s\n",
                       COLOR_BOLD, COLOR_RESET,
                       COLOR_BOLD, COLOR_RESET,
                       COLOR_BOLD, COLOR_RESET,
                       COLOR_BOLD, COLOR_RESET,
                       COLOR_BOLD, COLOR_RESET,
                       COLOR_BOLD, COLOR_RESET,
                       COLOR_BOLD, COLOR_RESET,
                       COLOR_CYAN, current_interval, COLOR_RESET);
            }

            memcpy(prev, curr, sizeof(ProcessData) * watch_count);
        }

        for (int elapsed = 0; elapsed < current_interval && keep_running; elapsed++) {
            struct timeval tv = { 1, 0 };
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(STDIN_FILENO, &fds);
            if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
                char ch = 0;
                if (read(STDIN_FILENO, &ch, 1) == 1) {
                    handle_keypress(ch, curr, histories, watch_count,
                                    &current_interval, &paused,
                                    &show_insights, &insight_idx, &sort,
                                    &needs_ai);
                    if (needs_ai) break;
                }
            }
        }

        /* ── AI overlay ── */
        if (needs_ai && watch_count > 0) {
            clear_screen();
            printf("\n  %s%s Querying Claude AI — please wait...%s\n\n",
                   COLOR_BOLD, COLOR_YELLOW, COLOR_RESET);
            fflush(stdout);

            SystemMemory mem;
            get_system_memory(&mem);
            static char ai_buf[8192];
            int ok = get_ai_process_insight(curr, watch_count, &mem,
                                            ai_buf, sizeof(ai_buf));
            if (ok == 0) {
                display_ai_response(ai_buf);
            } else {
                printf("\n  %s%s%s\n", COLOR_RED, ai_buf, COLOR_RESET);
            }

            printf("\n  %s[Any key]%s Dismiss\n", COLOR_BOLD, COLOR_RESET);
            fflush(stdout);

            char dismiss = 0;
            while (keep_running && dismiss == 0) {
                struct timeval tv = { 1, 0 };
                fd_set fds;
                FD_ZERO(&fds);
                FD_SET(STDIN_FILENO, &fds);
                if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0)
                    read(STDIN_FILENO, &dismiss, 1);
            }
            needs_ai = 0;
            continue;
        }

        while (paused && keep_running) {
            struct timeval tv = { 1, 0 };
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(STDIN_FILENO, &fds);
            if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
                char ch = 0;
                if (read(STDIN_FILENO, &ch, 1) == 1)
                    handle_keypress(ch, curr, histories, watch_count,
                                    &current_interval, &paused,
                                    &show_insights, &insight_idx, &sort,
                                    &needs_ai);
            }
        }
    }

    restore_terminal();
    return 0;
}

/* ════════════════════════════════════════════════════════════════════════
 * run_mem_mode — one-shot memory segment display for a PID
 * ════════════════════════════════════════════════════════════════════════ */

int run_mem_mode(ParsedCommand *cmd) {
    pid_t pid = cmd->pids[0];
    ProcessData pd;
    if (get_process_data(pid, &pd) != 0) {
        fprintf(stderr, "Error: cannot read data for PID %d\n", pid);
        return 1;
    }

    MemSegment segs[MAX_SEGMENTS];
    int seg_count = 0;
    get_memory_map(pid, segs, MAX_SEGMENTS, &seg_count);
    display_memory_map(pid, pd.name, segs, seg_count);
    return 0;
}

/* clear_screen defined in utils.c */
