/*
 * MarketPulse - Real-Time Stock Monitoring and Insight Engine
 * Logger Implementation - Structured logging with levels and rotation
 */

#include "logger.h"
#include "procscope.h"
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>

/* Global logger configuration */
static LoggerConfig g_logger = {
    .min_level = LOG_INFO,
    .file = NULL,
    .filename = "",
    .use_colors = 1,
    .include_timestamp = 1,
    .include_pid = 1,
    .max_file_size = 10 * 1024 * 1024,  /* 10 MB */
    .rotation_count = 5
};

static int g_initialized = 0;

/*
 * Get log level string
 */
const char *log_level_string(LogLevel level) {
    switch (level) {
        case LOG_DEBUG: return "DEBUG";
        case LOG_INFO:  return "INFO ";
        case LOG_WARN:  return "WARN ";
        case LOG_ERROR: return "ERROR";
        case LOG_FATAL: return "FATAL";
        default:        return "?????";
    }
}

/*
 * Get log level color
 */
const char *log_level_color(LogLevel level) {
    switch (level) {
        case LOG_DEBUG: return "\033[36m";  /* Cyan */
        case LOG_INFO:  return "\033[32m";  /* Green */
        case LOG_WARN:  return "\033[33m";  /* Yellow */
        case LOG_ERROR: return "\033[31m";  /* Red */
        case LOG_FATAL: return "\033[35m";  /* Magenta */
        default:        return "\033[0m";
    }
}

/*
 * Initialize logger
 */
int logger_init(const char *filename, LogLevel min_level) {
    g_logger.min_level = min_level;
    g_logger.use_colors = isatty(STDERR_FILENO);
    
    if (filename != NULL && filename[0] != '\0') {
        strncpy(g_logger.filename, filename, sizeof(g_logger.filename) - 1);
        g_logger.file = fopen(filename, "a");
        if (g_logger.file == NULL) {
            perror("Failed to open log file");
            return -1;
        }
        g_logger.use_colors = 0;  /* No colors in file */
    }
    
    g_initialized = 1;
    return 0;
}

/*
 * Close logger
 */
void logger_close(void) {
    if (g_logger.file != NULL) {
        fclose(g_logger.file);
        g_logger.file = NULL;
    }
    g_initialized = 0;
}

/*
 * Set minimum log level
 */
void logger_set_level(LogLevel level) {
    g_logger.min_level = level;
}

/*
 * Enable/disable colors
 */
void logger_set_colors(int enabled) {
    g_logger.use_colors = enabled;
}

/*
 * Check and perform log rotation
 */
void logger_check_rotation(void) {
    struct stat st;
    char old_name[512], new_name[512];
    int i;
    
    if (g_logger.file == NULL || g_logger.filename[0] == '\0') {
        return;
    }
    
    if (fstat(fileno(g_logger.file), &st) == -1) {
        return;
    }
    
    if ((size_t)st.st_size < g_logger.max_file_size) {
        return;
    }
    
    /* Close current file */
    fclose(g_logger.file);
    
    /* Rotate files */
    for (i = g_logger.rotation_count - 1; i > 0; i--) {
        snprintf(old_name, sizeof(old_name), "%s.%d", g_logger.filename, i);
        snprintf(new_name, sizeof(new_name), "%s.%d", g_logger.filename, i + 1);
        rename(old_name, new_name);
    }
    
    /* Rename current to .1 */
    snprintf(new_name, sizeof(new_name), "%s.1", g_logger.filename);
    rename(g_logger.filename, new_name);
    
    /* Open new file */
    g_logger.file = fopen(g_logger.filename, "a");
}

/*
 * Write log entry
 */
void log_write(LogLevel level, const char *format, ...) {
    va_list args;
    char timestamp[32];
    time_t now;
    struct tm *tm_info;
    FILE *output;
    
    if (level < g_logger.min_level) {
        return;
    }
    
    /* Get timestamp */
    time(&now);
    tm_info = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
    
    /* Choose output */
    output = g_logger.file ? g_logger.file : stderr;
    
    /* Check rotation */
    if (g_logger.file) {
        logger_check_rotation();
    }
    
    /* Print log entry */
    if (g_logger.use_colors) {
        fprintf(output, "%s[%s]%s ", log_level_color(level), 
                log_level_string(level), "\033[0m");
    } else {
        fprintf(output, "[%s] ", log_level_string(level));
    }
    
    if (g_logger.include_timestamp) {
        fprintf(output, "%s ", timestamp);
    }
    
    if (g_logger.include_pid) {
        fprintf(output, "[%d] ", getpid());
    }
    
    va_start(args, format);
    vfprintf(output, format, args);
    va_end(args);
    
    fprintf(output, "\n");
    fflush(output);
}

/* Convenience functions */
void log_debug(const char *format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    log_write(LOG_DEBUG, "%s", buffer);
}

void log_info(const char *format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    log_write(LOG_INFO, "%s", buffer);
}

void log_warn(const char *format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    log_write(LOG_WARN, "%s", buffer);
}

void log_error(const char *format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    log_write(LOG_ERROR, "%s", buffer);
}

void log_fatal(const char *format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    log_write(LOG_FATAL, "%s", buffer);
}