/*
 * ProcScope - Real-Time Process & Memory Monitor
 * stream.c — stream process data as newline-delimited JSON to a named pipe
 *
 * System calls used:
 *   mkfifo()     — create named pipe (FIFO) at /tmp/procscope.fifo
 *   open()       — open the FIFO (blocks until a reader connects)
 *   write()      — write JSON records to the pipe
 *   unlink()     — remove the FIFO on exit
 *   fork()       — parallel data collection workers
 *   signal()     — SIGINT/SIGTERM for clean shutdown
 */

#include "procscope.h"

int run_stream_mode(ParsedCommand *cmd) {
    /* Create the named pipe */
    unlink(STREAM_FIFO_PATH);
    if (mkfifo(STREAM_FIFO_PATH, 0600) < 0) {
        perror("mkfifo");
        return 1;
    }

    printf("  Stream started at %s%s%s\n",
           COLOR_CYAN, STREAM_FIFO_PATH, COLOR_RESET);
    printf("  Read with: %scat %s%s\n",
           COLOR_BOLD, STREAM_FIFO_PATH, COLOR_RESET);
    printf("  Press Ctrl-C to stop.\n\n");

    /* Open FIFO — blocks here until a reader opens the other end */
    int fifo_fd = open(STREAM_FIFO_PATH, O_WRONLY);
    if (fifo_fd < 0) {
        perror("open fifo");
        unlink(STREAM_FIFO_PATH);
        return 1;
    }

    /* Determine which PIDs to stream */
    pid_t stream_pids[MAX_WATCH_PIDS];
    int   stream_count = 0;

    if (cmd->target_count > 0) {
        for (int i = 0; i < cmd->target_count && stream_count < MAX_WATCH_PIDS; i++) {
            if (cmd->pids[i] > 0) {
                stream_pids[stream_count++] = cmd->pids[i];
            } else if (cmd->names[i][0]) {
                pid_t found[MAX_WATCH_PIDS];
                int n = find_pids_by_name(cmd->names[i], found, MAX_WATCH_PIDS);
                for (int j = 0; j < n && stream_count < MAX_WATCH_PIDS; j++)
                    stream_pids[stream_count++] = found[j];
            }
        }
    }

    /* If no targets given, stream the top 10 by RSS each cycle */
    int stream_all = (stream_count == 0);

    ProcessData prev[MAX_PROCESSES], curr[MAX_PROCESSES];
    memset(prev, 0, sizeof(prev));

    while (keep_running) {
        pid_t active_pids[MAX_PROCESSES];
        int   active_count = 0;

        if (stream_all) {
            active_count = get_all_pids(active_pids, MAX_PROCESSES);
            if (active_count > 10) active_count = 10; /* top 10 only */
        } else {
            memcpy(active_pids, stream_pids, sizeof(pid_t) * stream_count);
            active_count = stream_count;
        }

        /* Collect data */
        for (int i = 0; i < active_count; i++) {
            get_process_data(active_pids[i], &curr[i]);
            if (prev[i].valid) compute_cpu_percent(&prev[i], &curr[i]);
        }

        /* Emit one JSON record per process, one per line */
        char line[512];
        for (int i = 0; i < active_count; i++) {
            if (!curr[i].valid) continue;

            char time_buf[32];
            get_current_time_string(time_buf, sizeof(time_buf));

            int n = snprintf(line, sizeof(line),
                "{\"ts\":\"%s\","
                "\"pid\":%d,"
                "\"ppid\":%d,"
                "\"name\":\"%s\","
                "\"cpu_pct\":%.2f,"
                "\"rss_mb\":%.2f,"
                "\"vsize_mb\":%.2f,"
                "\"threads\":%d,"
                "\"fds\":%d}\n",
                time_buf,
                curr[i].pid,
                curr[i].ppid,
                curr[i].name,
                curr[i].cpu_percent,
                curr[i].rss_kb   / 1024.0,
                curr[i].vsize_kb / 1024.0,
                curr[i].thread_count,
                curr[i].fd_count);

            if (write(fifo_fd, line, n) < 0) {
                /* Reader disconnected */
                keep_running = 0;
                break;
            }
        }

        memcpy(prev, curr, sizeof(ProcessData) * active_count);
        sleep_ms(DEFAULT_REFRESH_INTERVAL * 1000);
    }

    close(fifo_fd);
    unlink(STREAM_FIFO_PATH);
    printf("  Stream closed.\n");
    return 0;
}
