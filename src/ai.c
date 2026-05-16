/*
 * MarketPulse - Real-Time Stock Monitoring and Insight Engine
 * AI Insight Module - Technical analysis and trend detection
 * 
 * This module implements simple but effective technical analysis:
 * - Moving Averages (5-period and 10-period)
 * - Volatility Detection (Standard Deviation)
 * - Momentum Analysis (Rate of Change)
 * - Trend Classification (Bullish/Bearish/Neutral)
 * 
 * Note: This is a simplified implementation for educational purposes.
 * Real trading systems use more sophisticated algorithms.
 */

#include "procscope.h"
#include <math.h>

/* External function to get currency mode */
extern int get_currency_mode(void);

/*
 * Initialize price history structure
 * Uses a circular buffer for efficient memory usage
 */
void init_price_history(PriceHistory *history) {
    memset(history, 0, sizeof(PriceHistory));
    history->count = 0;
    history->index = 0;
}

/*
 * Add a new price to the history
 * Implements circular buffer - oldest prices are overwritten
 */
void add_price(PriceHistory *history, double price) {
    if (price <= 0) {
        return;  /* Invalid price */
    }
    
    history->prices[history->index] = price;
    history->index = (history->index + 1) % MAX_PRICE_HISTORY;
    
    if (history->count < MAX_PRICE_HISTORY) {
        history->count++;
    }
}

/*
 * Get price at a specific position (0 = most recent)
 * Returns -1 if position is invalid
 */
static double get_price_at(PriceHistory *history, int position) {
    int actual_index;
    
    if (position < 0 || position >= history->count) {
        return -1;
    }
    
    /* Calculate actual index in circular buffer */
    actual_index = (history->index - 1 - position + MAX_PRICE_HISTORY) % MAX_PRICE_HISTORY;
    return history->prices[actual_index];
}

/*
 * Calculate Simple Moving Average (SMA)
 * 
 * SMA = (P1 + P2 + ... + Pn) / n
 * 
 * Parameters:
 *   history - Price history
 *   periods - Number of periods to average
 * 
 * Returns: Moving average, or -1 if not enough data
 */
double calculate_moving_average(PriceHistory *history, int periods) {
    double sum = 0;
    int i;
    
    if (history->count < periods || periods <= 0) {
        return -1;
    }
    
    for (i = 0; i < periods; i++) {
        double price = get_price_at(history, i);
        if (price < 0) {
            return -1;
        }
        sum += price;
    }
    
    return sum / periods;
}

/*
 * Calculate Exponential Moving Average (EMA)
 * 
 * EMA = Price * k + EMA_prev * (1 - k)
 * where k = 2 / (periods + 1)
 * 
 * EMA gives more weight to recent prices
 */
static double calculate_ema(PriceHistory *history, int periods) {
    double k = 2.0 / (periods + 1);
    double ema;
    int i;
    
    if (history->count < periods || periods <= 0) {
        return -1;
    }
    
    /* Start with SMA for initial EMA */
    ema = calculate_moving_average(history, periods);
    if (ema < 0) {
        return -1;
    }
    
    /* Apply EMA formula for remaining prices */
    for (i = periods; i < history->count; i++) {
        double price = get_price_at(history, history->count - 1 - i);
        if (price > 0) {
            ema = price * k + ema * (1 - k);
        }
    }
    
    return ema;
}

/*
 * Calculate Volatility (Standard Deviation)
 * 
 * Volatility measures how much prices deviate from the average.
 * Higher volatility = more risk/opportunity
 * 
 * σ = sqrt(Σ(Pi - μ)² / n)
 */
double calculate_volatility(PriceHistory *history) {
    double mean, variance, sum_sq_diff;
    int i;
    
    if (history->count < 2) {
        return 0;
    }
    
    /* Calculate mean */
    mean = calculate_moving_average(history, history->count);
    if (mean < 0) {
        return 0;
    }
    
    /* Calculate sum of squared differences */
    sum_sq_diff = 0;
    for (i = 0; i < history->count; i++) {
        double price = get_price_at(history, i);
        if (price > 0) {
            double diff = price - mean;
            sum_sq_diff += diff * diff;
        }
    }
    
    /* Calculate variance and standard deviation */
    variance = sum_sq_diff / history->count;
    return sqrt(variance);
}

/*
 * Calculate Momentum (Rate of Change)
 * 
 * Momentum = (Current Price - Price n periods ago) / Price n periods ago * 100
 * 
 * Positive momentum = upward trend
 * Negative momentum = downward trend
 */
double calculate_momentum(PriceHistory *history) {
    double current_price, old_price;
    int lookback = 5;  /* Compare with price 5 periods ago */
    
    if (history->count < lookback + 1) {
        return 0;
    }
    
    current_price = get_price_at(history, 0);
    old_price = get_price_at(history, lookback);
    
    if (current_price <= 0 || old_price <= 0) {
        return 0;
    }
    
    return ((current_price - old_price) / old_price) * 100;
}

/*
 * Calculate Relative Strength Index (RSI)
 * 
 * RSI = 100 - (100 / (1 + RS))
 * RS = Average Gain / Average Loss
 * 
 * RSI > 70 = Overbought (potential sell signal)
 * RSI < 30 = Oversold (potential buy signal)
 */
static double calculate_rsi(PriceHistory *history, int periods) {
    double gains = 0, losses = 0;
    int i;
    
    if (history->count < periods + 1) {
        return 50;  /* Neutral if not enough data */
    }
    
    for (i = 0; i < periods; i++) {
        double current = get_price_at(history, i);
        double previous = get_price_at(history, i + 1);
        
        if (current > 0 && previous > 0) {
            double change = current - previous;
            if (change > 0) {
                gains += change;
            } else {
                losses -= change;  /* Make positive */
            }
        }
    }
    
    if (losses == 0) {
        return 100;  /* All gains, maximum RSI */
    }
    
    double rs = (gains / periods) / (losses / periods);
    return 100 - (100 / (1 + rs));
}

/*
 * Detect trend direction based on moving average crossover
 * 
 * Golden Cross: Short MA crosses above Long MA = Bullish
 * Death Cross: Short MA crosses below Long MA = Bearish
 */
static int detect_ma_crossover(PriceHistory *history) {
    double ma_short = calculate_moving_average(history, 5);
    double ma_long = calculate_moving_average(history, 10);
    
    if (ma_short < 0 || ma_long < 0) {
        return 0;  /* Neutral - not enough data */
    }
    
    double diff_percent = ((ma_short - ma_long) / ma_long) * 100;
    
    if (diff_percent > 0.5) {
        return 1;   /* Bullish */
    } else if (diff_percent < -0.5) {
        return -1;  /* Bearish */
    }
    
    return 0;  /* Neutral */
}

/*
 * Analyze stock and generate insights
 * 
 * This is the main analysis function that combines multiple indicators
 * to generate a comprehensive market insight.
 */
void analyze_stock(PriceHistory *history, AIInsight *insight) {
    double ma5, ma10, volatility, momentum, rsi;
    int trend_signal;
    
    /* Initialize insight structure */
    memset(insight, 0, sizeof(AIInsight));
    strcpy(insight->trend, "Neutral");
    strcpy(insight->momentum, "Stable");
    strcpy(insight->recommendation, "Insufficient data for analysis");
    
    if (history->count < 3) {
        return;  /* Not enough data */
    }
    
    /* Calculate indicators */
    ma5 = calculate_moving_average(history, 5);
    ma10 = calculate_moving_average(history, 10);
    volatility = calculate_volatility(history);
    momentum = calculate_momentum(history);
    rsi = calculate_rsi(history, 14);
    trend_signal = detect_ma_crossover(history);
    
    /* Store calculated values */
    insight->moving_avg_5 = (ma5 > 0) ? ma5 : 0;
    insight->moving_avg_10 = (ma10 > 0) ? ma10 : 0;
    insight->volatility = volatility;
    
    /* Determine trend */
    if (trend_signal > 0 || momentum > 2) {
        strcpy(insight->trend, "Bullish");
    } else if (trend_signal < 0 || momentum < -2) {
        strcpy(insight->trend, "Bearish");
    } else {
        strcpy(insight->trend, "Neutral");
    }
    
    /* Determine momentum strength */
    if (fabs(momentum) > 5) {
        strcpy(insight->momentum, "Strong");
    } else if (fabs(momentum) > 2) {
        strcpy(insight->momentum, "Moderate");
    } else {
        strcpy(insight->momentum, "Weak");
    }
    
    /* Get currency symbol for recommendations */
    const char *curr = get_currency_mode() ? "₹" : "$";
    
    /* Generate recommendation based on combined signals */
    if (strcmp(insight->trend, "Bullish") == 0) {
        if (rsi > 70) {
            snprintf(insight->recommendation, sizeof(insight->recommendation),
                     "Upward trend detected but RSI (%.1f) suggests overbought conditions. "
                     "Consider taking profits.", rsi);
        } else if (volatility > ma5 * 0.02) {
            snprintf(insight->recommendation, sizeof(insight->recommendation),
                     "Bullish trend with high volatility (%.2f%%). "
                     "Potential for significant moves.", (volatility / ma5) * 100);
        } else {
            snprintf(insight->recommendation, sizeof(insight->recommendation),
                     "Steady upward trend detected. MA5 (%s%.2f) above MA10 (%s%.2f). "
                     "Momentum: %.1f%%", curr, ma5, curr, ma10, momentum);
        }
    } else if (strcmp(insight->trend, "Bearish") == 0) {
        if (rsi < 30) {
            snprintf(insight->recommendation, sizeof(insight->recommendation),
                     "Downward trend but RSI (%.1f) suggests oversold conditions. "
                     "Watch for potential reversal.", rsi);
        } else {
            snprintf(insight->recommendation, sizeof(insight->recommendation),
                     "Bearish trend detected. MA5 (%s%.2f) below MA10 (%s%.2f). "
                     "Momentum: %.1f%%", curr, ma5, curr, ma10, momentum);
        }
    } else {
        snprintf(insight->recommendation, sizeof(insight->recommendation),
                 "Market is consolidating. No clear trend. "
                 "Volatility: %.2f%%. Wait for breakout.", (volatility / ma5) * 100);
    }
}

/*
 * Print AI insight in formatted output
 */
void print_ai_insight(AIInsight *insight, const char *symbol) {
    const char *trend_color;
    const char *trend_icon;
    const char *currency_symbol;
    
    /* Determine currency based on mode */
    currency_symbol = get_currency_mode() ? "₹" : "$";
    
    /* Determine colors and icons based on trend */
    if (strcmp(insight->trend, "Bullish") == 0) {
        trend_color = COLOR_GREEN;
        trend_icon = "📈";
    } else if (strcmp(insight->trend, "Bearish") == 0) {
        trend_color = COLOR_RED;
        trend_icon = "📉";
    } else {
        trend_color = COLOR_YELLOW;
        trend_icon = "📊";
    }
    
    printf("\n");
    printf("  %s%s═══ AI Insight for %s ═══%s\n", 
           COLOR_BOLD, COLOR_MAGENTA, symbol, COLOR_RESET);
    printf("\n");
    
    printf("  %s Trend:      %s%s%s%s\n", 
           trend_icon, COLOR_BOLD, trend_color, insight->trend, COLOR_RESET);
    
    printf("  ⚡ Momentum:   %s%s%s\n", 
           COLOR_BOLD, insight->momentum, COLOR_RESET);
    
    if (insight->moving_avg_5 > 0) {
        printf("  📊 MA(5):      %s%.2f\n", currency_symbol, insight->moving_avg_5);
    }
    
    if (insight->moving_avg_10 > 0) {
        printf("  📊 MA(10):     %s%.2f\n", currency_symbol, insight->moving_avg_10);
    }
    
    if (insight->volatility > 0) {
        printf("  📈 Volatility: %s%.2f\n", currency_symbol, insight->volatility);
    }
    
    printf("\n");
    printf("  %s💡 Analysis:%s\n", COLOR_BOLD, COLOR_RESET);
    printf("  %s\n", insight->recommendation);
    printf("\n");
}

/*
 * Generate a quick one-line insight
 * Useful for compact display in watch mode
 */
void get_quick_insight(PriceHistory *history, char *buffer, size_t size) {
    double momentum;
    int trend;
    
    if (history->count < 3) {
        snprintf(buffer, size, "Gathering data...");
        return;
    }
    
    momentum = calculate_momentum(history);
    trend = detect_ma_crossover(history);
    
    if (trend > 0 && momentum > 0) {
        snprintf(buffer, size, "🟢 Bullish (momentum: +%.1f%%)", momentum);
    } else if (trend < 0 && momentum < 0) {
        snprintf(buffer, size, "🔴 Bearish (momentum: %.1f%%)", momentum);
    } else if (momentum > 2) {
        snprintf(buffer, size, "🟡 Rising (momentum: +%.1f%%)", momentum);
    } else if (momentum < -2) {
        snprintf(buffer, size, "🟡 Falling (momentum: %.1f%%)", momentum);
    } else {
        snprintf(buffer, size, "⚪ Stable (momentum: %.1f%%)", momentum);
    }
}

/*
 * Predict next price movement (very simplified)
 * Returns: 1 for up, -1 for down, 0 for uncertain
 */
int predict_direction(PriceHistory *history) {
    double momentum = calculate_momentum(history);
    int trend = detect_ma_crossover(history);
    double rsi = calculate_rsi(history, 14);
    
    int score = 0;
    
    /* Momentum signal */
    if (momentum > 1) score++;
    else if (momentum < -1) score--;
    
    /* Trend signal */
    score += trend;
    
    /* RSI signal (contrarian) */
    if (rsi > 70) score--;  /* Overbought, expect pullback */
    else if (rsi < 30) score++;  /* Oversold, expect bounce */
    
    if (score >= 2) return 1;   /* Likely up */
    if (score <= -2) return -1; /* Likely down */
    return 0;  /* Uncertain */
}