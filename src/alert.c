/*
 * ProcScope - Real-Time Process & Memory Monitor
 * alert.c — threshold-based alerting for CPU% and RSS
 *
 * System calls used:
 *   signal() / sigaction() — SIGINT, SIGTERM, SIGUSR1, SIGUSR2
 *   fork() + pipe()        — worker collects data, sends back over pipe
 *   select()               — sleep interruptible by signals
 *   kill()                 — send signal to monitored process (future)
 */

#include "procscope.h"

void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM)
        keep_running = 0;
    else if (signum == SIGALRM)
        alert_triggered = 1;
}

void setup_signal_handlers(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGALRM, &sa, NULL);

    /* Ignore SIGPIPE (broken pipes from named pipe consumers) */
    signal(SIGPIPE, SIG_IGN);

    /* SIGUSR1 / SIGUSR2 for daemon control */
    sa.sa_handler = signal_handler;
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGUSR2, &sa, NULL);
}

/* ─────────────────────────────────────────────────────────────────────────
 * run_alert_mode — poll a process and fire when threshold is crossed
 * ─────────────────────────────────────────────────────────────────────── */
int run_alert_mode(ParsedCommand *cmd) {
    pid_t target_pid = cmd->pids[0];

    /* If a name was given, resolve to PID */
    if (target_pid == 0 && cmd->names[0][0]) {
        pid_t found[MAX_WATCH_PIDS];
        int n = find_pids_by_name(cmd->names[0], found, 1);
        if (n <= 0) {
            fprintf(stderr, "Error: no process named '%s' found\n", cmd->names[0]);
            return 1;
        }
        target_pid = found[0];
    }

    if (target_pid <= 0) {
        fprintf(stderr, "Error: invalid target\n");
        return 1;
    }

    double cpu_thresh = cmd->cpu_threshold;
    long   rss_thresh = cmd->rss_threshold_kb;

    printf("  %sMonitoring PID %d%s", COLOR_BOLD, target_pid, COLOR_RESET);
    if (cpu_thresh > 0.0)
        printf("  CPU alert: %.1f%%", cpu_thresh);
    if (rss_thresh > 0)
        printf("  RSS alert: %.0f MB", rss_thresh / 1024.0);
    printf("\n  Press Ctrl-C to stop.\n\n");

    ProcessData prev, curr;
    memset(&prev, 0, sizeof(prev));

    int cpu_triggered = 0, rss_triggered = 0;

    while (keep_running) {
        /* Fork a worker to collect data */
        int fd[2];
        if (pipe(fd) < 0) { sleep_ms(ALERT_CHECK_INTERVAL * 1000); continue; }

        pid_t worker = fork();
        if (worker == 0) {
            close(fd[0]);
            get_process_data(target_pid, &curr);
            if (prev.valid) compute_cpu_percent(&prev, &curr);
            write(fd[1], &curr, sizeof(curr));
            close(fd[1]);
            _exit(0);
        }
        close(fd[1]);
        ssize_t n = read(fd[0], &curr, sizeof(curr));
        close(fd[0]);
        int status;
        waitpid(worker, &status, 0);

        if (n != (ssize_t)sizeof(curr) || !curr.valid) {
            printf("  %sProcess %d has exited.%s\n",
                   COLOR_YELLOW, target_pid, COLOR_RESET);
            break;
        }

        char time_buf[32];
        get_current_time_string(time_buf, sizeof(time_buf));

        printf("  [%s] PID %d (%s)  CPU: %s%.1f%%%s  RSS: %s%.1f MB%s\n",
               time_buf, target_pid, curr.name,
               curr.cpu_percent > (cpu_thresh > 0 ? cpu_thresh : 100) ? COLOR_RED : COLOR_GREEN,
               curr.cpu_percent, COLOR_RESET,
               curr.rss_kb > (rss_thresh > 0 ? rss_thresh : LONG_MAX) ? COLOR_RED : COLOR_GREEN,
               curr.rss_kb / 1024.0, COLOR_RESET);

        /* Check thresholds */
        if (cpu_thresh > 0.0 && curr.cpu_percent > cpu_thresh && !cpu_triggered) {
            printf("\n  %s! ALERT: PID %d CPU %.1f%% exceeded threshold %.1f%%%s\n\n",
                   COLOR_RED, target_pid, curr.cpu_percent, cpu_thresh, COLOR_RESET);
            cpu_triggered = 1;
        } else if (cpu_thresh > 0.0 && curr.cpu_percent <= cpu_thresh * 0.9) {
            cpu_triggered = 0; /* re-arm when drops below 90% of threshold */
        }

        if (rss_thresh > 0 && curr.rss_kb > rss_thresh && !rss_triggered) {
            printf("\n  %s! ALERT: PID %d RSS %.1f MB exceeded threshold %.0f MB%s\n\n",
                   COLOR_RED, target_pid, curr.rss_kb / 1024.0,
                   rss_thresh / 1024.0, COLOR_RESET);
            rss_triggered = 1;
        } else if (rss_thresh > 0 && curr.rss_kb <= (long)(rss_thresh * 0.9)) {
            rss_triggered = 0;
        }

        memcpy(&prev, &curr, sizeof(prev));

        /* Interruptible sleep */
        struct timeval tv = { ALERT_CHECK_INTERVAL, 0 };
        fd_set fds;
        FD_ZERO(&fds);
        select(0, NULL, NULL, NULL, &tv);
    }

    return 0;
}
