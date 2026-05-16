/*
 * ProcScope - Real-Time Process & Memory Monitor
 * system.c — system-wide memory and CPU status display
 */

#include "procscope.h"

int display_system_status(void) {
    SystemMemory mem;
    if (get_system_memory(&mem) != 0) {
        fprintf(stderr, "Error reading system memory\n");
        return 1;
    }

    char time_buf[32];
    get_current_time_string(time_buf, sizeof(time_buf));

    printf("\n");
    printf("  %s%sSystem Status%s  —  %s\n\n",
           COLOR_BOLD, COLOR_CYAN, COLOR_RESET, time_buf);

    /* RAM */
    double used_pct = (mem.total_kb > 0)
                    ? (double)mem.used_kb / mem.total_kb * 100.0 : 0.0;

    const char *ram_color = (used_pct > 85) ? COLOR_RED :
                            (used_pct > 65) ? COLOR_YELLOW : COLOR_GREEN;

    printf("  %sRAM%s\n", COLOR_BOLD, COLOR_RESET);
    printf("    Total:  %8.2f GB\n", mem.total_kb / (1024.0 * 1024.0));
    printf("    Used:   %s%8.2f GB  (%.1f%%)%s\n",
           ram_color, mem.used_kb / (1024.0 * 1024.0), used_pct, COLOR_RESET);
    printf("    Free:   %8.2f GB\n\n", mem.free_kb / (1024.0 * 1024.0));

    /* Swap */
    if (mem.swap_total_kb > 0) {
        double swap_pct = (double)mem.swap_used_kb / mem.swap_total_kb * 100.0;
        const char *swap_color = (swap_pct > 80) ? COLOR_RED :
                                 (swap_pct > 50) ? COLOR_YELLOW : COLOR_GREEN;
        printf("  %sSWAP%s\n", COLOR_BOLD, COLOR_RESET);
        printf("    Total:  %8.2f GB\n", mem.swap_total_kb / (1024.0 * 1024.0));
        printf("    Used:   %s%8.2f GB  (%.1f%%)%s\n\n",
               swap_color, mem.swap_used_kb / (1024.0 * 1024.0), swap_pct, COLOR_RESET);
    }

    /* Process count */
    pid_t pids[MAX_PROCESSES];
    int count = get_all_pids(pids, MAX_PROCESSES);
    printf("  %sProcesses:%s  %d running\n\n",
           COLOR_BOLD, COLOR_RESET, count > 0 ? count : 0);

    /* ASCII bar for RAM usage */
    int bar_width = 40;
    int filled    = (int)(used_pct / 100.0 * bar_width);
    printf("  RAM  [%s", ram_color);
    for (int i = 0; i < bar_width; i++)
        printf("%s", i < filled ? "█" : "░");
    printf("%s]  %.1f%%\n\n", COLOR_RESET, used_pct);

    return 0;
}
