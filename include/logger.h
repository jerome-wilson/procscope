/*
 * MarketPulse - Real-Time Stock Monitoring and Insight Engine
 * Logger Header - Structured logging system
 */

#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>
#include <time.h>

/* Log levels */
typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO = 1,
    LOG_WARN = 2,
    LOG_ERROR = 3,
    LOG_FATAL = 4
} LogLevel;

/* Logger configuration */
typedef struct {
    LogLevel min_level;
    FILE *file;
    char filename[256];
    int use_colors;
    int include_timestamp;
    int include_pid;
    size_t max_file_size;
    int rotation_count;
} LoggerConfig;

/* Function declarations */
int logger_init(const char *filename, LogLevel min_level);
void logger_close(void);
void logger_set_level(LogLevel level);
void logger_set_colors(int enabled);

/* Logging functions */
void log_debug(const char *format, ...);
void log_info(const char *format, ...);
void log_warn(const char *format, ...);
void log_error(const char *format, ...);
void log_fatal(const char *format, ...);

/* Generic log function */
void log_write(LogLevel level, const char *format, ...);

/* Log rotation */
void logger_rotate(void);
void logger_check_rotation(void);

/* Utility */
const char *log_level_string(LogLevel level);
const char *log_level_color(LogLevel level);

#endif /* LOGGER_H */