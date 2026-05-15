/*
 * MarketPulse - Real-Time Stock Monitoring and Insight Engine
 * Daemon Header - Background service functionality
 * 
 * System Programming Concepts:
 * - fork() twice for daemon creation
 * - setsid() - Create new session
 * - File descriptor management
 * - PID file handling
 */

#ifndef DAEMON_H
#define DAEMON_H

#include <sys/types.h>

/* Daemon configuration (PID_FILE defined in procscope.h) */
#define LOG_FILE        "/tmp/procscope.log"
#define WORKING_DIR     "/tmp"

/* Daemon states */
typedef enum {
    DAEMON_STOPPED = 0,
    DAEMON_RUNNING = 1,
    DAEMON_STARTING = 2,
    DAEMON_STOPPING = 3
} DaemonState;

/* Function declarations */

/* Daemon lifecycle */
int daemon_start(void);
int daemon_stop(void);
int daemon_restart(void);
int daemon_status(void);

/* Daemonization process */
int daemonize(void);
int create_pid_file(pid_t pid);
int remove_pid_file(void);
pid_t read_pid_file(void);
int is_daemon_running(void);

/* Signal setup for daemon */
void daemon_setup_signals(void);
void daemon_signal_handler(int signum);

/* Logging for daemon mode */
void daemon_log(const char *format, ...);
void daemon_log_error(const char *format, ...);

/* Utility */
void redirect_stdio(void);
void close_all_fds(void);

#endif /* DAEMON_H */