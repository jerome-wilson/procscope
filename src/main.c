/*
 * ProcScope - Real-Time Process & Memory Monitor
 * main.c — entry point and command dispatch
 */

#include "procscope.h"
#include "daemon.h"

/* Defined in alert.c */
extern void setup_signal_handlers(void);

/* Global flags (used by signal handlers across modules) */
volatile sig_atomic_t keep_running    = 1;
volatile sig_atomic_t alert_triggered = 0;

int main(int argc, char *argv[]) {
    ParsedCommand cmd;
    int result = 0;

    setup_signal_handlers();

    if (parse_command(argc, argv, &cmd) != 0)
        return 1;

    switch (cmd.type) {
        case CMD_HELP:
            print_usage(argv[0]);
            break;

        case CMD_VERSION:
            print_version();
            break;

        case CMD_LIST:
            print_header();
            result = run_list_mode();
            break;

        case CMD_TOP:
            print_header();
            result = run_top_mode(&cmd);
            break;

        case CMD_WATCH:
            print_header();
            result = run_watch_mode(&cmd);
            break;

        case CMD_MEM:
            print_header();
            result = run_mem_mode(&cmd);
            break;

        case CMD_ALERT:
            print_header();
            result = run_alert_mode(&cmd);
            break;

        case CMD_STREAM:
            result = run_stream_mode(&cmd);
            break;

        case CMD_STATUS:
            result = display_system_status();
            break;

        case CMD_INSIGHTS:
            result = run_insights_mode();
            break;

        case CMD_DAEMON_START:
            if (is_daemon_running()) {
                printf("ProcScope daemon is already running.\n");
                result = 1;
            } else {
                printf("Starting ProcScope daemon...\n");
                result = daemonize();
                if (result == 0) {
                    /* In child: become a top-mode monitor */
                    cmd.type = CMD_TOP;
                    cmd.top_n = 10;
                    result = run_top_mode(&cmd);
                }
            }
            break;

        case CMD_DAEMON_STOP:
            result = daemon_stop();
            break;

        case CMD_DAEMON_STATUS:
            result = daemon_status();
            break;

        case CMD_UNKNOWN:
        default:
            fprintf(stderr, "Error: unknown command\n");
            print_usage(argv[0]);
            result = 1;
            break;
    }

    return result;
}
