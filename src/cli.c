/*
 * ProcScope - Real-Time Process & Memory Monitor
 * cli.c — command-line argument parsing
 */

#include "procscope.h"

void print_usage(const char *prog) {
    printf("\n");
    printf("%s%sProcScope%s - Real-Time Process & Memory Monitor\n",
           COLOR_BOLD, COLOR_CYAN, COLOR_RESET);
    printf("Version %s\n\n", PROCSCOPE_VERSION);

    printf("%sUSAGE:%s\n", COLOR_BOLD, COLOR_RESET);
    printf("  %s list                     List all running processes\n", prog);
    printf("  %s top [N] [--mem]          Top N processes by CPU (default 10)\n", prog);
    printf("  %s watch <PID|name> ...     Live TUI watch for one or more processes\n", prog);
    printf("  %s mem <PID>                Memory segment breakdown for a process\n", prog);
    printf("  %s alert <PID> [opts]       Alert on CPU%% or RSS threshold\n", prog);
    printf("  %s stream [PID...]          Stream data as JSON to named pipe\n", prog);
    printf("  %s status                   System memory overview\n", prog);
    printf("  %s insights                 AI-powered process analysis (needs ANTHROPIC_API_KEY)\n", prog);
    printf("  %s daemon start|stop|status Daemon control\n", prog);
    printf("  %s --help                   Show this help\n", prog);
    printf("  %s --version                Show version\n\n", prog);

    printf("%sEXAMPLES:%s\n", COLOR_BOLD, COLOR_RESET);
    printf("  %s list                     List every process with CPU%% and RSS\n", prog);
    printf("  %s top 20                   Watch top 20 processes by CPU\n", prog);
    printf("  %s top --mem                Watch top 10 processes by memory\n", prog);
    printf("  %s watch 1234               Watch process 1234 live\n", prog);
    printf("  %s watch Safari             Watch all Safari processes\n", prog);
    printf("  %s mem 1234                 Show memory segments of PID 1234\n", prog);
    printf("  %s alert 1234 --cpu 80      Alert when PID 1234 exceeds 80%% CPU\n", prog);
    printf("  %s alert 1234 --mem 500     Alert when PID 1234 exceeds 500 MB RSS\n", prog);
    printf("  %s stream                   Stream all top processes as JSON\n", prog);

    printf("\n%sINTERACTIVE KEYS (watch/top):%s\n", COLOR_BOLD, COLOR_RESET);
    printf("  [Q] Quit  [S] Sort  [+/-] Speed  [P] Pause  [I] Insights  [A] AI  [Esc] Close panel\n");

    printf("\n%sSYSTEM CALLS DEMONSTRATED:%s\n", COLOR_BOLD, COLOR_RESET);
    printf("  sysctl  proc_pidinfo  fork  pipe  signal  select  mmap  mkfifo  socket  setsid\n\n");
}

void print_version(void) {
    printf("ProcScope version %s\n", PROCSCOPE_VERSION);
    printf("Built: %s\n", __DATE__);
    printf("Platform: macOS (sysctl + libproc)\n\n");
    printf("Systems Programming Project\n");
    printf("Syscalls: sysctl(), proc_pidinfo(), fork(), pipe(), signal(), select(),\n");
    printf("          mmap(), mkfifo(), socket(), setsid(), kill(), clock_gettime()\n");
}

/* Parse a PID-or-name token from the command line.
   Returns 1 if it looks like a PID (all digits), 0 if a name string. */
static int parse_pid_or_name(const char *token, ParsedCommand *cmd) {
    if (cmd->target_count >= MAX_WATCH_PIDS) return 0;
    int is_num = 1;
    for (const char *p = token; *p; p++) {
        if (*p < '0' || *p > '9') { is_num = 0; break; }
    }
    if (is_num) {
        cmd->pids[cmd->target_count] = (pid_t)atoi(token);
        cmd->names[cmd->target_count][0] = '\0';
    } else {
        cmd->pids[cmd->target_count] = 0;
        strncpy(cmd->names[cmd->target_count], token, MAX_NAME_LEN - 1);
    }
    cmd->target_count++;
    return is_num;
}

int parse_command(int argc, char *argv[], ParsedCommand *cmd) {
    memset(cmd, 0, sizeof(ParsedCommand));
    cmd->type             = CMD_UNKNOWN;
    cmd->top_n            = 10;
    cmd->sort_mode        = SORT_CPU;
    cmd->refresh_interval = DEFAULT_REFRESH_INTERVAL;

    if (argc < 2) { cmd->type = CMD_HELP; return 0; }

    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0 ||
        strcmp(argv[1], "help")   == 0) {
        cmd->type = CMD_HELP; return 0;
    }
    if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0 ||
        strcmp(argv[1], "version")   == 0) {
        cmd->type = CMD_VERSION; return 0;
    }

    /* ── list ── */
    if (strcmp(argv[1], "list") == 0 || strcmp(argv[1], "ls") == 0) {
        cmd->type = CMD_LIST;
        return 0;
    }

    /* ── top [N] [--mem] ── */
    if (strcmp(argv[1], "top") == 0) {
        cmd->type = CMD_TOP;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--mem") == 0 || strcmp(argv[i], "--memory") == 0)
                cmd->sort_mode = SORT_RSS;
            else if (strcmp(argv[i], "--cpu") == 0)
                cmd->sort_mode = SORT_CPU;
            else if (strcmp(argv[i], "--pid") == 0)
                cmd->sort_mode = SORT_PID;
            else {
                int n = atoi(argv[i]);
                if (n > 0) cmd->top_n = n;
            }
        }
        return 0;
    }

    /* ── watch <PID|name> ... ── */
    if (strcmp(argv[1], "watch") == 0) {
        cmd->type = CMD_WATCH;
        if (argc < 3) {
            fprintf(stderr, "Error: 'watch' requires at least one PID or process name\n");
            return -1;
        }
        for (int i = 2; i < argc; i++)
            parse_pid_or_name(argv[i], cmd);
        if (cmd->target_count == 0) {
            fprintf(stderr, "Error: no valid PIDs or names provided\n");
            return -1;
        }
        return 0;
    }

    /* ── mem <PID> ── */
    if (strcmp(argv[1], "mem") == 0 || strcmp(argv[1], "memory") == 0) {
        cmd->type = CMD_MEM;
        if (argc < 3) {
            fprintf(stderr, "Error: 'mem' requires a PID\n");
            return -1;
        }
        cmd->pids[0] = (pid_t)atoi(argv[2]);
        if (cmd->pids[0] <= 0) {
            fprintf(stderr, "Error: invalid PID '%s'\n", argv[2]);
            return -1;
        }
        cmd->target_count = 1;
        return 0;
    }

    /* ── alert <PID> [--cpu N] [--mem N] ── */
    if (strcmp(argv[1], "alert") == 0) {
        cmd->type = CMD_ALERT;
        if (argc < 3) {
            fprintf(stderr, "Error: 'alert' requires a PID\n");
            return -1;
        }
        /* First arg can be PID or name */
        parse_pid_or_name(argv[2], cmd);
        for (int i = 3; i < argc; i++) {
            if ((strcmp(argv[i], "--cpu") == 0) && i + 1 < argc)
                cmd->cpu_threshold = atof(argv[++i]);
            else if ((strcmp(argv[i], "--mem") == 0 ||
                      strcmp(argv[i], "--memory") == 0) && i + 1 < argc)
                cmd->rss_threshold_kb = atol(argv[++i]) * 1024; /* arg in MB */
        }
        if (cmd->cpu_threshold <= 0.0 && cmd->rss_threshold_kb <= 0) {
            fprintf(stderr, "Error: specify --cpu <pct> and/or --mem <MB>\n");
            return -1;
        }
        return 0;
    }

    /* ── stream [PID...] ── */
    if (strcmp(argv[1], "stream") == 0) {
        cmd->type = CMD_STREAM;
        for (int i = 2; i < argc; i++)
            parse_pid_or_name(argv[i], cmd);
        return 0;
    }

    /* ── status ── */
    if (strcmp(argv[1], "status") == 0) {
        cmd->type = CMD_STATUS;
        return 0;
    }

    /* ── insights / ai ── */
    if (strcmp(argv[1], "insights") == 0 || strcmp(argv[1], "ai") == 0) {
        cmd->type = CMD_INSIGHTS;
        return 0;
    }

    /* ── daemon start|stop|status ── */
    if (strcmp(argv[1], "daemon") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: 'daemon' requires start|stop|status\n");
            return -1;
        }
        if (strcmp(argv[2], "start")  == 0) cmd->type = CMD_DAEMON_START;
        else if (strcmp(argv[2], "stop")   == 0) cmd->type = CMD_DAEMON_STOP;
        else if (strcmp(argv[2], "status") == 0) cmd->type = CMD_DAEMON_STATUS;
        else {
            fprintf(stderr, "Error: unknown daemon subcommand '%s'\n", argv[2]);
            return -1;
        }
        return 0;
    }

    fprintf(stderr, "Error: unknown command '%s'\n", argv[1]);
    fprintf(stderr, "Run '%s --help' for usage\n", argv[0]);
    return -1;
}
