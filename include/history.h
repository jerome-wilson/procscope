/*
 * MarketPulse - Persistent Price History via mmap
 *
 * System calls: open, ftruncate, mmap, msync, munmap, close
 *
 * Persists price history to data/price_history.dat using a memory-mapped
 * file so charts survive across sessions. The kernel syncs the mapping to
 * disk via msync(); reads and writes go directly to memory.
 */

#ifndef HISTORY_H
#define HISTORY_H

#include "procscope.h"
#include <sys/mman.h>

#define HISTORY_MAGIC       0x48495354   /* "HIST" */
#define HISTORY_VERSION     1
#define MAX_HISTORY_STOCKS  50
#define HISTORY_FILE        "data/price_history.dat"

/* One stock's price history stored inside the mmap'd file */
typedef struct {
    char   symbol[16];
    int    count;
    int    index;                        /* circular buffer head */
    double prices[MAX_PRICE_HISTORY];
    time_t last_update;
} MmapStockHistory;

/* Root layout of data/price_history.dat */
typedef struct {
    int    magic;
    int    version;
    int    stock_count;
    char   _pad[4];
    MmapStockHistory stocks[MAX_HISTORY_STOCKS];
} PriceHistoryFile;

/* Open (or create) the history file and mmap it. Returns NULL on error. */
PriceHistoryFile *history_open(const char *path);

/* msync + munmap + close */
void history_close(PriceHistoryFile *h);

/* Copy saved prices for symbol into out->prices[], out->count, out->index */
void history_load(PriceHistoryFile *h, const char *symbol, PriceHistory *out);

/* Write src->prices[], count, index for symbol into the mmap region + msync */
void history_save(PriceHistoryFile *h, const char *symbol, const PriceHistory *src);

#endif /* HISTORY_H */
