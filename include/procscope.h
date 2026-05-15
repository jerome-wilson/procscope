/*
 * ProcScope - Real-Time Process & Memory Monitor
 * Main header: data structures, constants, function declarations
 *
 * Reads live process and memory data directly from the macOS kernel
 * via sysctl() and proc_pidinfo() — no network, no API.
 *
 * System calls demonstrated:
 *   sysctl, proc_pidinfo, fork, pipe, waitpid, signal, select,
 *   mmap, mkfifo, socket, setsid, kill, clock_gettime, read/write
 */

#ifndef PROCSCOPE_H
#define PROCSCOPE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <math.h>

/* macOS-specific process info API */
#include <libproc.h>
#include <mach/mach.h>
#include <mach/mach_host.h>

/* ============== Configuration ============== */

#define PROCSCOPE_VERSION "1.0.0"

/* Buffer / array limits */
#define MAX_PROCESSES     512
#define MAX_NAME_LEN      256
#define MAX_PATH_LEN      1024
#define MAX_SEGMENTS      256
#define MAX_WATCH_PIDS    32
#define MAX_PRICE_HISTORY 60   /* 60 samples of history per process */

/* Refresh timing */
#define DEFAULT_REFRESH_INTERVAL 2   /* seconds */
#define ALERT_CHECK_INTERVAL     1

/* Named pipe path */
#define STREAM_FIFO_PATH "/tmp/procscope.fifo"

/* PID file */
#define PID_FILE "/tmp/procscope.pid"

/* ============== Data Structures ============== */

/* Per-process snapshot */
typedef struct {
    pid_t   pid;
    pid_t   ppid;
    char    name[MAX_NAME_LEN];
    char    path[MAX_PATH_LEN];
    double  cpu_percent;      /* computed delta over sample interval */
    long    rss_kb;           /* resident set size in KB */
    long    vsize_kb;         /* virtual memory size in KB */
    int     fd_count;         /* open file descriptors */
    int     thread_count;
    time_t  start_time;
    /* raw CPU ticks for delta computation */
    uint64_t cpu_ticks_prev;
    uint64_t cpu_ticks_curr;
    uint64_t sample_time_ns;  /* nanoseconds of last sample */
    int     valid;
} ProcessData;

/* One virtual memory region within a process */
typedef struct {
    char     region[64];      /* __TEXT, __DATA, heap, stack, etc. */
    uint64_t start_addr;
    uint64_t end_addr;
    size_t   size_kb;
    char     perms[8];        /* r/w/x flags */
} MemSegment;

/* System-wide memory snapshot */
typedef struct {
    long total_kb;
    long used_kb;
    long free_kb;
    long swap_total_kb;
    long swap_used_kb;
} SystemMemory;

/* Price/value history for AI analysis (reused from marketpulse) */
typedef struct {
    double prices[MAX_PRICE_HISTORY];
    int count;
    int index;  /* circular buffer index */
} PriceHistory;

/* AI insight (same structure as marketpulse — math is domain-agnostic) */
typedef struct {
    char trend[32];
    char momentum[32];
    double moving_avg_5;
    double moving_avg_10;
    double volatility;
    char recommendation[128];
} AIInsight;

/* Alert configuration */
typedef struct {
    pid_t  pid;
    char   name[MAX_NAME_LEN];
    double cpu_threshold;     /* 0 = disabled */
    long   rss_threshold_kb;  /* 0 = disabled */
    int    triggered_cpu;
    int    triggered_rss;
} AlertConfig;

/* ============== Command Types ============== */

typedef enum {
    CMD_UNKNOWN,
    CMD_LIST,            /* procscope list */
    CMD_WATCH,           /* procscope watch <pid|name> ... */
    CMD_TOP,             /* procscope top [--cpu|--mem] [N] */
    CMD_MEM,             /* procscope mem <pid> */
    CMD_ALERT,           /* procscope alert <pid> --cpu N --mem N */
    CMD_STREAM,          /* procscope stream [pid...] */
    CMD_STATUS,          /* procscope status */
    CMD_DAEMON_START,
    CMD_DAEMON_STOP,
    CMD_DAEMON_STATUS,
    CMD_INSIGHTS,
    CMD_HELP,
    CMD_VERSION
} CommandType;

typedef enum {
    SORT_CPU,
    SORT_RSS,
    SORT_PID,
    SORT_NAME
} SortMode;

typedef struct {
    CommandType type;
    pid_t  pids[MAX_WATCH_PIDS];
    char   names[MAX_WATCH_PIDS][MAX_NAME_LEN];
    int    target_count;
    int    top_n;              /* for CMD_TOP */
    SortMode sort_mode;        /* for CMD_TOP */
    double cpu_threshold;      /* for CMD_ALERT */
    long   rss_threshold_kb;   /* for CMD_ALERT */
    int    refresh_interval;
} ParsedCommand;

/* ============== Color Codes ============== */

#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_WHITE   "\033[37m"
#define COLOR_BOLD    "\033[1m"
#define COLOR_DIM     "\033[2m"

#define ARROW_UP   "▲"
#define ARROW_DOWN "▼"
#define ARROW_FLAT "►"

/* ============== Global State ============== */

extern volatile sig_atomic_t keep_running;
extern volatile sig_atomic_t alert_triggered;

/* ============== Function Declarations ============== */

/* cli.c */
int  parse_command(int argc, char *argv[], ParsedCommand *cmd);
void print_usage(const char *prog);
void print_version(void);

/* proc.c */
int  get_all_pids(pid_t *pids, int max_count);
int  get_process_data(pid_t pid, ProcessData *out);
int  get_process_path(pid_t pid, char *path, size_t size);
int  get_fd_count(pid_t pid);
int  get_system_memory(SystemMemory *mem);
int  find_pids_by_name(const char *name, pid_t *pids, int max);
void compute_cpu_percent(ProcessData *prev, ProcessData *curr);

/* memmap.c */
int  get_memory_map(pid_t pid, MemSegment *segs, int max_segs, int *count);
void display_memory_map(pid_t pid, const char *name,
                        MemSegment *segs, int count);

/* monitor.c */
int  run_list_mode(void);
int  run_watch_mode(ParsedCommand *cmd);
int  run_top_mode(ParsedCommand *cmd);
int  run_mem_mode(ParsedCommand *cmd);
void display_process_table(ProcessData *procs, PriceHistory *histories,
                           int count, SortMode sort);
void clear_screen(void);

/* alert.c */
int  run_alert_mode(ParsedCommand *cmd);
void setup_signal_handlers(void);
void signal_handler(int signum);

/* stream.c */
int  run_stream_mode(ParsedCommand *cmd);

/* system.c (status command) */
int  display_system_status(void);

/* ai.c (unchanged from marketpulse) */
void   init_price_history(PriceHistory *h);
void   add_price(PriceHistory *h, double price);
void   analyze_stock(PriceHistory *h, AIInsight *insight);
double calculate_moving_average(PriceHistory *h, int periods);
double calculate_volatility(PriceHistory *h);
double calculate_momentum(PriceHistory *h);
void   print_ai_insight(AIInsight *insight, const char *label);

/* utils.c (unchanged from marketpulse) */
void        get_current_time_string(char *buf, size_t size);
void        format_price(double price, char *buf, size_t size);
void        format_change(double change, double pct, char *buf, size_t size);
const char *get_trend_arrow(double change);
const char *get_trend_color(double change);
void        print_header(void);
void        print_separator(void);
void        sleep_ms(int ms);
void        str_to_upper(char *s);
void        safe_strcpy(char *dst, const char *src, size_t n);
long long   get_timestamp_ms(void);
void        format_number_with_commas(long long n, char *buf, size_t size);

/* daemon.c (unchanged from marketpulse) */
int  daemonize(void);
int  is_daemon_running(void);
int  daemon_stop(void);
int  daemon_status(void);

/* ai_insights.c — real Claude API via popen()+curl */
int  run_insights_mode(void);
int  get_ai_process_insight(ProcessData *procs, int count, SystemMemory *mem,
                             char *output, size_t output_size);
void display_ai_response(const char *text);

#endif /* PROCSCOPE_H */
