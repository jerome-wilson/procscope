/*
 * MarketPulse - Real-Time Stock Monitoring and Insight Engine
 * Utility functions implementation
 */

#include "procscope.h"
#include <sys/time.h>

/*
 * Get current time as formatted string
 * Format: HH:MM:SS
 */
void get_current_time_string(char *buffer, size_t size) {
    time_t now;
    struct tm *tm_info;
    
    time(&now);
    tm_info = localtime(&now);
    
    strftime(buffer, size, "%H:%M:%S", tm_info);
}

/*
 * Get current US Eastern Time as formatted string
 * Format: HH:MM:SS ET
 * Note: Approximation - doesn't handle DST perfectly
 */
void get_us_time_string(char *buffer, size_t size) {
    time_t now;
    struct tm *tm_info;
    int hour, minute, second;
    const char *period;
    
    time(&now);
    tm_info = gmtime(&now);
    
    /* Adjust for ET (UTC-5, or UTC-4 during DST) */
    /* Using UTC-5 as approximation */
    hour = (tm_info->tm_hour - 5 + 24) % 24;
    minute = tm_info->tm_min;
    second = tm_info->tm_sec;
    
    /* Convert to 12-hour format with AM/PM */
    if (hour == 0) {
        hour = 12;
        period = "AM";
    } else if (hour < 12) {
        period = "AM";
    } else if (hour == 12) {
        period = "PM";
    } else {
        hour -= 12;
        period = "PM";
    }
    
    snprintf(buffer, size, "%d:%02d:%02d %s ET", hour, minute, second, period);
}

/*
 * Get current Indian Standard Time as formatted string
 * Format: HH:MM:SS IST
 */
void get_ist_time_string(char *buffer, size_t size) {
    time_t now;
    struct tm *tm_info;
    int total_minutes, hour, minute, second;
    
    time(&now);
    tm_info = gmtime(&now);
    
    /* Adjust for IST (UTC+5:30) */
    total_minutes = tm_info->tm_hour * 60 + tm_info->tm_min + 330;  /* +5:30 = +330 minutes */
    hour = (total_minutes / 60) % 24;
    minute = total_minutes % 60;
    second = tm_info->tm_sec;
    
    snprintf(buffer, size, "%02d:%02d:%02d IST", hour, minute, second);
}

/* Global flag to track if we're displaying Indian stocks */
static int display_inr = 0;

/*
 * Set currency display mode
 * 0 = USD ($), 1 = INR (₹)
 */
void set_currency_mode(int use_inr) {
    display_inr = use_inr;
}

/*
 * Get current currency mode
 */
int get_currency_mode(void) {
    return display_inr;
}

/*
 * Format price with currency symbol
 * Example: $178.50 or ₹1,234.50
 */
void format_price(double price, char *buffer, size_t size) {
    if (price < 0) {
        snprintf(buffer, size, "N/A");
    } else if (display_inr) {
        /* Indian Rupee format with comma separators */
        if (price >= 10000000) {
            snprintf(buffer, size, "₹%.2fCr", price / 10000000);
        } else if (price >= 100000) {
            snprintf(buffer, size, "₹%.2fL", price / 100000);
        } else {
            snprintf(buffer, size, "₹%.2f", price);
        }
    } else {
        snprintf(buffer, size, "$%.2f", price);
    }
}

/*
 * Format price in INR (Indian Rupees)
 * Example: ₹1,234.50
 */
void format_price_inr(double price, char *buffer, size_t size) {
    if (price < 0) {
        snprintf(buffer, size, "N/A");
    } else if (price >= 10000000) {
        snprintf(buffer, size, "₹%.2fCr", price / 10000000);
    } else if (price >= 100000) {
        snprintf(buffer, size, "₹%.2fL", price / 100000);
    } else {
        snprintf(buffer, size, "₹%.2f", price);
    }
}

/*
 * Format change with sign and percentage
 * Example: +1.25 (+0.70%)
 */
void format_change(double change, double change_percent, char *buffer, size_t size) {
    const char *sign = (change >= 0) ? "+" : "";
    snprintf(buffer, size, "%s%.2f (%s%.2f%%)", 
             sign, change, sign, change_percent);
}

/*
 * Get trend arrow based on price change
 */
const char *get_trend_arrow(double change) {
    if (change > 0.01) {
        return ARROW_UP;
    } else if (change < -0.01) {
        return ARROW_DOWN;
    } else {
        return ARROW_FLAT;
    }
}

/*
 * Get color code based on price change
 */
const char *get_trend_color(double change) {
    if (change > 0.01) {
        return COLOR_GREEN;
    } else if (change < -0.01) {
        return COLOR_RED;
    } else {
        return COLOR_YELLOW;
    }
}

/*
 * Print application header
 */
void print_header(void) {
    printf("\n");
    printf("%s%s", COLOR_BOLD, COLOR_CYAN);
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║       ProcScope - Real-Time Process & Memory Monitor         ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    printf("%s", COLOR_RESET);
    printf("\n");
}

/*
 * Print separator line
 */
void print_separator(void) {
    printf("%s", COLOR_CYAN);
    printf("──────────────────────────────────────────────────────────────────\n");
    printf("%s", COLOR_RESET);
}

/*
 * Check if US stock market is currently open
 * NYSE/NASDAQ: 9:30 AM - 4:00 PM ET (Monday-Friday)
 * Note: This is a simplified check, doesn't account for holidays
 */
int is_market_open(void) {
    time_t now;
    struct tm *tm_info;
    int hour, minute, day_of_week;
    int market_minutes;
    
    time(&now);
    
    /* Convert to Eastern Time (approximate - doesn't handle DST perfectly) */
    /* For accurate results, would need timezone library */
    tm_info = gmtime(&now);
    
    /* Adjust for ET (UTC-5 or UTC-4 during DST) */
    /* Using UTC-5 as approximation */
    hour = (tm_info->tm_hour - 5 + 24) % 24;
    minute = tm_info->tm_min;
    day_of_week = tm_info->tm_wday;
    
    /* Check if weekend */
    if (day_of_week == 0 || day_of_week == 6) {
        return 0;  /* Market closed on weekends */
    }
    
    /* Convert to minutes since midnight */
    market_minutes = hour * 60 + minute;
    
    /* Market open: 9:30 AM (570 minutes) to 4:00 PM (960 minutes) */
    if (market_minutes >= 570 && market_minutes < 960) {
        return 1;  /* Market is open */
    }
    
    return 0;  /* Market is closed */
}

/*
 * Check if Indian stock market is currently open
 * NSE/BSE: 9:15 AM - 3:30 PM IST (Monday-Friday)
 * Note: This is a simplified check, doesn't account for holidays
 */
int is_indian_market_open(void) {
    time_t now;
    struct tm *tm_info;
    int hour, minute, day_of_week;
    int market_minutes;
    
    time(&now);
    
    /* Convert to IST (UTC+5:30) */
    tm_info = gmtime(&now);
    
    /* Adjust for IST (UTC+5:30) */
    int total_minutes = tm_info->tm_hour * 60 + tm_info->tm_min + 330;  /* +5:30 = +330 minutes */
    hour = (total_minutes / 60) % 24;
    minute = total_minutes % 60;
    day_of_week = tm_info->tm_wday;
    
    /* Adjust day if we crossed midnight */
    if (total_minutes >= 1440) {
        day_of_week = (day_of_week + 1) % 7;
    }
    
    /* Check if weekend */
    if (day_of_week == 0 || day_of_week == 6) {
        return 0;  /* Market closed on weekends */
    }
    
    /* Convert to minutes since midnight */
    market_minutes = hour * 60 + minute;
    
    /* Market open: 9:15 AM (555 minutes) to 3:30 PM (930 minutes) */
    if (market_minutes >= 555 && market_minutes < 930) {
        return 1;  /* Market is open */
    }
    
    return 0;  /* Market is closed */
}

/*
 * Sleep for specified milliseconds
 * Uses usleep() which takes microseconds
 */
void sleep_ms(int milliseconds) {
    usleep(milliseconds * 1000);
}

/*
 * Clear terminal screen
 * Uses ANSI escape codes
 */
void clear_screen(void) {
    /* ANSI escape code to clear screen and move cursor to top-left */
    printf("\033[2J\033[H");
    fflush(stdout);
}

/*
 * Convert string to uppercase (in place)
 */
void str_to_upper(char *str) {
    while (*str) {
        if (*str >= 'a' && *str <= 'z') {
            *str = *str - 'a' + 'A';
        }
        str++;
    }
}

/*
 * Trim whitespace from string (in place)
 */
void str_trim(char *str) {
    char *start = str;
    char *end;
    
    /* Trim leading whitespace */
    while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') {
        start++;
    }
    
    /* If string is all whitespace */
    if (*start == '\0') {
        str[0] = '\0';
        return;
    }
    
    /* Trim trailing whitespace */
    end = start + strlen(start) - 1;
    while (end > start && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        end--;
    }
    
    /* Null terminate */
    *(end + 1) = '\0';
    
    /* Move trimmed string to beginning if needed */
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
}

/*
 * Safe string copy with null termination
 */
void safe_strcpy(char *dest, const char *src, size_t dest_size) {
    if (dest_size == 0) return;
    
    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

/*
 * Print error message with formatting
 */
void print_error(const char *message) {
    fprintf(stderr, "%s%sError: %s%s\n", COLOR_BOLD, COLOR_RED, message, COLOR_RESET);
}

/*
 * Print warning message with formatting
 */
void print_warning(const char *message) {
    printf("%s%sWarning: %s%s\n", COLOR_BOLD, COLOR_YELLOW, message, COLOR_RESET);
}

/*
 * Print success message with formatting
 */
void print_success(const char *message) {
    printf("%s%s%s%s\n", COLOR_BOLD, COLOR_GREEN, message, COLOR_RESET);
}

/*
 * Print info message with formatting
 */
void print_info(const char *message) {
    printf("%s%s%s%s\n", COLOR_BOLD, COLOR_CYAN, message, COLOR_RESET);
}

/*
 * Get current timestamp in milliseconds
 */
long long get_timestamp_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

/*
 * Format large numbers with commas
 * Example: 1234567 -> "1,234,567"
 */
void format_number_with_commas(long long number, char *buffer, size_t size) {
    char temp[32];
    int len, i, j;
    int negative = 0;
    
    if (number < 0) {
        negative = 1;
        number = -number;
    }
    
    snprintf(temp, sizeof(temp), "%lld", number);
    len = strlen(temp);
    
    j = 0;
    if (negative && j < (int)size - 1) {
        buffer[j++] = '-';
    }
    
    for (i = 0; i < len && j < (int)size - 1; i++) {
        if (i > 0 && (len - i) % 3 == 0 && j < (int)size - 1) {
            buffer[j++] = ',';
        }
        buffer[j++] = temp[i];
    }
    buffer[j] = '\0';
}

/*
 * Print a progress spinner (for loading indication)
 * Call repeatedly to animate
 */
void print_spinner(int frame) {
    const char *spinner = "|/-\\";
    printf("\r%c Loading...", spinner[frame % 4]);
    fflush(stdout);
}

/*
 * Print a simple progress bar
 */
void print_progress_bar(int current, int total, int width) {
    int filled = (current * width) / total;
    int i;
    
    printf("\r[");
    for (i = 0; i < width; i++) {
        if (i < filled) {
            printf("%s█%s", COLOR_GREEN, COLOR_RESET);
        } else {
            printf("░");
        }
    }
    printf("] %d%%", (current * 100) / total);
    fflush(stdout);
}