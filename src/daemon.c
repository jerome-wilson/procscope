/*
 * MarketPulse - Real-Time Stock Monitoring and Insight Engine
 * Daemon Implementation - Background service functionality
 * 
 * System Programming Concepts:
 * - fork() twice - Standard daemon creation pattern
 * - setsid() - Create new session, become session leader
 * - File descriptor management - Close/redirect stdin/stdout/stderr
 * - PID file - Track running daemon instance
 */

#include "daemon.h"
#include "procscope.h"
#include "logger.h"
#include <stdarg.h>
#include <errno.h>
#include <sys/stat.h>

/* Daemon state */
static volatile int g_daemon_running = 0;

/*
 * Create PID file
 */
int create_pid_file(pid_t pid) {
    FILE *f = fopen(PID_FILE, "w");
    if (f == NULL) {
        perror("Failed to create PID file");
        return -1;
    }
    fprintf(f, "%d\n", pid);
    fclose(f);
    return 0;
}

/*
 * Remove PID file
 */
int remove_pid_file(void) {
    if (unlink(PID_FILE) == -1 && errno != ENOENT) {
        perror("Failed to remove PID file");
        return -1;
    }
    return 0;
}

/*
 * Read PID from file
 */
pid_t read_pid_file(void) {
    FILE *f;
    pid_t pid = 0;
    
    f = fopen(PID_FILE, "r");
    if (f == NULL) {
        return 0;
    }
    
    if (fscanf(f, "%d", &pid) != 1) {
        pid = 0;
    }
    fclose(f);
    
    return pid;
}

/*
 * Check if daemon is running
 */
int is_daemon_running(void) {
    pid_t pid = read_pid_file();
    
    if (pid <= 0) {
        return 0;
    }
    
    /* Check if process exists */
    if (kill(pid, 0) == 0) {
        return 1;  /* Process exists */
    }
    
    if (errno == ESRCH) {
        /* Process doesn't exist, stale PID file */
        remove_pid_file();
        return 0;
    }
    
    return 1;  /* Process exists but we can't signal it */
}

/*
 * Daemonize the process
 * Standard double-fork technique
 */
int daemonize(void) {
    pid_t pid;
    
    /* First fork - exit parent */
    pid = fork();
    if (pid < 0) {
        perror("First fork failed");
        return -1;
    }
    if (pid > 0) {
        /* Parent exits */
        _exit(0);
    }
    
    /* Child becomes session leader */
    if (setsid() < 0) {
        perror("setsid failed");
        return -1;
    }
    
    /* Second fork - prevent acquiring controlling terminal */
    pid = fork();
    if (pid < 0) {
        perror("Second fork failed");
        return -1;
    }
    if (pid > 0) {
        /* First child exits */
        _exit(0);
    }
    
    /* Now we're the daemon (grandchild) */
    
    /* Change working directory */
    if (chdir(WORKING_DIR) < 0) {
        perror("chdir failed");
        /* Non-fatal, continue */
    }
    
    /* Set file mode mask */
    umask(0);
    
    /* Close all open file descriptors */
    close_all_fds();
    
    /* Redirect standard file descriptors */
    redirect_stdio();
    
    /* Create PID file */
    create_pid_file(getpid());
    
    g_daemon_running = 1;
    
    return 0;
}

/*
 * Close all file descriptors
 */
void close_all_fds(void) {
    int fd;
    int max_fd = sysconf(_SC_OPEN_MAX);
    
    if (max_fd < 0) {
        max_fd = 1024;  /* Reasonable default */
    }
    
    for (fd = 0; fd < max_fd; fd++) {
        close(fd);
    }
}

/*
 * Redirect stdin/stdout/stderr
 */
void redirect_stdio(void) {
    int fd;
    
    /* stdin from /dev/null */
    fd = open("/dev/null", O_RDONLY);
    if (fd != STDIN_FILENO) {
        dup2(fd, STDIN_FILENO);
        close(fd);
    }
    
    /* stdout to log file */
    fd = open(LOG_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd != STDOUT_FILENO) {
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }
    
    /* stderr to stdout */
    dup2(STDOUT_FILENO, STDERR_FILENO);
}

/*
 * Daemon signal handler
 */
void daemon_signal_handler(int signum) {
    switch (signum) {
        case SIGTERM:
        case SIGINT:
            daemon_log("Received shutdown signal");
            g_daemon_running = 0;
            break;
        case SIGHUP:
            daemon_log("Received SIGHUP - reloading configuration");
            /* Trigger config reload */
            break;
        case SIGUSR1:
            daemon_log("Received SIGUSR1 - status request");
            break;
        case SIGUSR2:
            daemon_log("Received SIGUSR2 - debug toggle");
            break;
    }
}

/*
 * Setup signal handlers for daemon
 */
void daemon_setup_signals(void) {
    struct sigaction sa;
    
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = daemon_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGUSR2, &sa, NULL);
    
    /* Ignore SIGPIPE */
    signal(SIGPIPE, SIG_IGN);
    
    /* Ignore child signals (auto-reap) */
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = SA_NOCLDWAIT;
    sigaction(SIGCHLD, &sa, NULL);
}

/*
 * Log message (daemon mode)
 */
void daemon_log(const char *format, ...) {
    va_list args;
    time_t now;
    struct tm *tm_info;
    char timestamp[32];
    
    time(&now);
    tm_info = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
    
    fprintf(stdout, "[%s] [%d] ", timestamp, getpid());
    
    va_start(args, format);
    vfprintf(stdout, format, args);
    va_end(args);
    
    fprintf(stdout, "\n");
    fflush(stdout);
}

/*
 * Log error (daemon mode)
 */
void daemon_log_error(const char *format, ...) {
    va_list args;
    time_t now;
    struct tm *tm_info;
    char timestamp[32];
    
    time(&now);
    tm_info = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
    
    fprintf(stderr, "[%s] [%d] ERROR: ", timestamp, getpid());
    
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    
    fprintf(stderr, "\n");
    fflush(stderr);
}

/*
 * Get daemon status
 */
int daemon_status(void) {
    pid_t pid = read_pid_file();
    
    if (pid <= 0) {
        printf("MarketPulse daemon is not running\n");
        return 1;
    }
    
    if (kill(pid, 0) == 0) {
        printf("MarketPulse daemon is running (PID: %d)\n", pid);
        return 0;
    }
    
    printf("MarketPulse daemon is not running (stale PID file)\n");
    remove_pid_file();
    return 1;
}

/*
 * Stop daemon
 */
int daemon_stop(void) {
    pid_t pid = read_pid_file();
    int timeout = 10;
    
    if (pid <= 0) {
        printf("MarketPulse daemon is not running\n");
        return 1;
    }
    
    printf("Stopping MarketPulse daemon (PID: %d)...\n", pid);
    
    /* Send SIGTERM */
    if (kill(pid, SIGTERM) == -1) {
        if (errno == ESRCH) {
            printf("Process not found, removing stale PID file\n");
            remove_pid_file();
            return 0;
        }
        perror("Failed to send signal");
        return -1;
    }
    
    /* Wait for process to exit */
    while (timeout > 0) {
        usleep(500000);  /* 0.5 seconds */
        if (kill(pid, 0) == -1 && errno == ESRCH) {
            printf("Daemon stopped successfully\n");
            remove_pid_file();
            return 0;
        }
        timeout--;
    }
    
    /* Force kill */
    printf("Daemon not responding, sending SIGKILL...\n");
    kill(pid, SIGKILL);
    usleep(100000);
    remove_pid_file();
    
    return 0;
}

/*
 * Restart daemon
 */
int daemon_restart(void) {
    daemon_stop();
    sleep(1);
    return daemon_start();
}

/*
 * Start daemon (placeholder - actual implementation in master.c)
 */
int daemon_start(void) {
    if (is_daemon_running()) {
        printf("MarketPulse daemon is already running\n");
        return 1;
    }
    
    printf("Starting MarketPulse daemon...\n");
    /* Actual start is handled by master_start_daemon() */
    return 0;
}