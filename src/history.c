/*
 * MarketPulse - Persistent Price History via mmap
 *
 * System calls demonstrated:
 *   open()      - open/create the history file
 *   ftruncate() - pre-allocate file to required size
 *   mmap()      - map file into process address space
 *   msync()     - flush in-memory changes back to disk
 *   munmap()    - unmap when done
 *   close()     - release the file descriptor
 */

#include "history.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

static int    g_history_fd   = -1;
static size_t g_history_size = 0;

PriceHistoryFile *history_open(const char *path) {
    PriceHistoryFile *h;
    size_t size = sizeof(PriceHistoryFile);
    struct stat st;
    int is_new = 0;

    /* Ensure the data/ directory exists */
    mkdir("data", 0755);

    int fd = open(path, O_RDWR | O_CREAT, 0644);
    if (fd == -1) return NULL;

    /* If the file is smaller than our layout, pre-allocate */
    if (fstat(fd, &st) == 0 && st.st_size < (off_t)size) {
        is_new = 1;
        if (ftruncate(fd, (off_t)size) == -1) {
            close(fd);
            return NULL;
        }
    }

    h = (PriceHistoryFile *)mmap(NULL, size,
                                  PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (h == MAP_FAILED) {
        close(fd);
        return NULL;
    }

    /* Initialise if brand new or magic mismatch */
    if (is_new || h->magic != HISTORY_MAGIC) {
        memset(h, 0, size);
        h->magic       = HISTORY_MAGIC;
        h->version     = HISTORY_VERSION;
        h->stock_count = 0;
        msync(h, size, MS_SYNC);
    }

    g_history_fd   = fd;
    g_history_size = size;
    return h;
}

void history_close(PriceHistoryFile *h) {
    if (!h || h == MAP_FAILED) return;
    msync(h, g_history_size, MS_SYNC);
    munmap(h, g_history_size);
    if (g_history_fd != -1) {
        close(g_history_fd);
        g_history_fd = -1;
    }
}

void history_load(PriceHistoryFile *h, const char *symbol, PriceHistory *out) {
    int i;
    if (!h || !symbol || !out) return;

    for (i = 0; i < h->stock_count; i++) {
        if (strcmp(h->stocks[i].symbol, symbol) == 0) {
            out->count = h->stocks[i].count;
            out->index = h->stocks[i].index;
            memcpy(out->prices, h->stocks[i].prices, sizeof(out->prices));
            return;
        }
    }
    /* Symbol not found — caller keeps its zero-initialised PriceHistory */
}

void history_save(PriceHistoryFile *h, const char *symbol, const PriceHistory *src) {
    int i, slot = -1;
    if (!h || !symbol || !src) return;

    for (i = 0; i < h->stock_count; i++) {
        if (strcmp(h->stocks[i].symbol, symbol) == 0) {
            slot = i;
            break;
        }
    }

    if (slot == -1) {
        if (h->stock_count >= MAX_HISTORY_STOCKS) return;
        slot = h->stock_count++;
        strncpy(h->stocks[slot].symbol, symbol, 15);
        h->stocks[slot].symbol[15] = '\0';
    }

    h->stocks[slot].count       = src->count;
    h->stocks[slot].index       = src->index;
    h->stocks[slot].last_update = time(NULL);
    memcpy(h->stocks[slot].prices, src->prices, sizeof(h->stocks[slot].prices));

    /* Async flush — kernel writes to disk in the background */
    msync(h, g_history_size, MS_ASYNC);
}
