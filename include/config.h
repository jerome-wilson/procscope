/*
 * MarketPulse - Real-Time Stock Monitoring and Insight Engine
 * Configuration Header - JSON config file support
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <time.h>

#define CONFIG_FILE         "config/marketpulse.json"
#define MAX_CONFIG_STOCKS   50
#define MAX_ALERTS          20

/* Alert rule structure */
typedef struct {
    char symbol[16];
    double threshold;
    int above;              /* 1 = alert when above, 0 = when below */
    int enabled;
} AlertRule;

/* Application configuration */
typedef struct {
    /* API settings */
    char api_key[128];
    char api_host[128];
    int api_port;
    
    /* Timing settings */
    int refresh_interval;       /* seconds */
    int alert_check_interval;   /* seconds */
    int worker_timeout;         /* seconds */
    int rate_limit_requests;    /* requests per minute */
    
    /* Stock list */
    char stocks[MAX_CONFIG_STOCKS][16];
    int stock_count;
    
    /* Alert rules */
    AlertRule alerts[MAX_ALERTS];
    int alert_count;
    
    /* Logging */
    char log_file[256];
    int log_level;              /* 0=DEBUG, 1=INFO, 2=WARN, 3=ERROR */
    int log_rotation_size;      /* MB */
    
    /* Database */
    char db_file[256];
    int db_history_days;        /* Days to keep history */
    
    /* UI settings */
    int use_colors;
    int use_unicode;
    
    /* Daemon settings */
    int daemon_mode;
    char pid_file[256];
    
    /* AI settings */
    int ai_enabled;
    int ai_adaptive_polling;
    double ai_volatility_threshold;
    
    /* Loaded timestamp */
    time_t loaded_at;
} AppConfig;

/* Function declarations */
int config_load(const char *filename, AppConfig *config);
int config_save(const char *filename, const AppConfig *config);
void config_set_defaults(AppConfig *config);
int config_validate(const AppConfig *config);
void config_print(const AppConfig *config);

/* Config file operations */
int config_file_exists(const char *filename);
int config_create_default(const char *filename);

/* Runtime config updates */
int config_add_stock(AppConfig *config, const char *symbol);
int config_remove_stock(AppConfig *config, const char *symbol);
int config_add_alert(AppConfig *config, const char *symbol, double threshold, int above);

/* Global config access */
AppConfig *config_get_global(void);
void config_set_global(AppConfig *config);

#endif /* CONFIG_H */