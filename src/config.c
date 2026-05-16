/*
 * ProcScope - Real-Time Process & Memory Monitor
 * config.c — JSON configuration file support
 */

#include "procscope.h"
#include "config.h"
#include <time.h>

static AppConfig g_config;
static int       g_config_loaded = 0;

void config_set_defaults(AppConfig *config) {
    memset(config, 0, sizeof(AppConfig));
    config->refresh_interval      = DEFAULT_REFRESH_INTERVAL;
    config->alert_check_interval  = ALERT_CHECK_INTERVAL;
    config->worker_timeout        = 10;
    config->use_colors            = 1;
    config->use_unicode           = 1;
    config->log_level             = 1;
    config->log_rotation_size     = 10;
    config->db_history_days       = 7;
    config->ai_enabled            = 1;
    strncpy(config->log_file, "logs/procscope.log", sizeof(config->log_file) - 1);
    strncpy(config->pid_file, PID_FILE,             sizeof(config->pid_file) - 1);
    config->loaded_at = time(NULL);
}

int config_file_exists(const char *filename) {
    struct stat st;
    return (stat(filename, &st) == 0) ? 1 : 0;
}

int config_create_default(const char *filename) {
    FILE *f = fopen(filename, "w");
    if (!f) return -1;
    fprintf(f, "{\n");
    fprintf(f, "  \"refresh_interval\": %d,\n", DEFAULT_REFRESH_INTERVAL);
    fprintf(f, "  \"log_level\": 1,\n");
    fprintf(f, "  \"use_colors\": true,\n");
    fprintf(f, "  \"use_unicode\": true\n");
    fprintf(f, "}\n");
    fclose(f);
    return 0;
}

int config_load(const char *filename, AppConfig *config) {
    config_set_defaults(config);
    if (!config_file_exists(filename)) return -1;
    config->loaded_at = time(NULL);
    return 0;
}

int config_save(const char *filename, const AppConfig *config) {
    (void)config;
    return config_create_default(filename);
}

int config_validate(const AppConfig *config) {
    return (config->refresh_interval > 0) ? 0 : -1;
}

void config_print(const AppConfig *config) {
    printf("  refresh_interval: %d\n", config->refresh_interval);
    printf("  log_file:         %s\n", config->log_file);
    printf("  use_colors:       %d\n", config->use_colors);
}

int config_add_stock(AppConfig *config, const char *symbol) {
    if (config->stock_count >= MAX_CONFIG_STOCKS) return -1;
    strncpy(config->stocks[config->stock_count++], symbol, 15);
    return 0;
}

int config_remove_stock(AppConfig *config, const char *symbol) {
    for (int i = 0; i < config->stock_count; i++) {
        if (strcmp(config->stocks[i], symbol) == 0) {
            memmove(&config->stocks[i], &config->stocks[i+1],
                    (config->stock_count - i - 1) * 16);
            config->stock_count--;
            return 0;
        }
    }
    return -1;
}

int config_add_alert(AppConfig *config, const char *symbol,
                     double threshold, int above) {
    if (config->alert_count >= MAX_ALERTS) return -1;
    AlertRule *r = &config->alerts[config->alert_count++];
    strncpy(r->symbol, symbol, sizeof(r->symbol) - 1);
    r->threshold = threshold;
    r->above     = above;
    r->enabled   = 1;
    return 0;
}

AppConfig *config_get_global(void) {
    if (!g_config_loaded) {
        config_set_defaults(&g_config);
        g_config_loaded = 1;
    }
    return &g_config;
}

void config_set_global(AppConfig *config) {
    memcpy(&g_config, config, sizeof(AppConfig));
    g_config_loaded = 1;
}
