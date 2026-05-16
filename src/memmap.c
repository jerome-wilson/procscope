/*
 * ProcScope - Real-Time Process & Memory Monitor
 * memmap.c — per-process virtual memory segment reader and display
 *
 * System calls used:
 *   proc_pidinfo(PROC_PIDREGIONPATHINFO) — enumerate VM regions for a PID
 *
 * This shows what Activity Monitor cannot: the breakdown of a process's
 * address space into named segments (__TEXT, __DATA, heap, stack, etc.)
 * with sizes and permissions.
 */

#include "procscope.h"

/* ─────────────────────────────────────────────────────────────────────────
 * get_memory_map — enumerate VM regions for a PID
 *
 * proc_pidinfo(PROC_PIDREGIONPATHINFO, offset) returns one region at
 * `offset` (the start address). We loop, incrementing offset by region
 * size each iteration, until proc_pidinfo returns <= 0.
 * ─────────────────────────────────────────────────────────────────────── */
int get_memory_map(pid_t pid, MemSegment *segs, int max_segs, int *count) {
    *count = 0;
    if (pid <= 0 || !segs) return -1;

    uint64_t addr = 0;

    while (*count < max_segs) {
        struct proc_regionwithpathinfo rwp;
        memset(&rwp, 0, sizeof(rwp));

        int r = proc_pidinfo(pid, PROC_PIDREGIONPATHINFO, addr,
                             &rwp, sizeof(rwp));
        if (r <= 0) break;

        struct proc_regioninfo *ri = &rwp.prp_prinfo;
        MemSegment *seg = &segs[*count];

        /* Region name (may be empty for anonymous regions) */
        if (rwp.prp_vip.vip_path[0] != '\0') {
            /* Use basename of path */
            const char *slash = strrchr(rwp.prp_vip.vip_path, '/');
            strncpy(seg->region,
                    slash ? slash + 1 : rwp.prp_vip.vip_path,
                    sizeof(seg->region) - 1);
        } else {
            /* Classify anonymous regions by protection flags */
            int prot = ri->pri_protection;
            if ((prot & VM_PROT_EXECUTE) && (prot & VM_PROT_READ))
                strncpy(seg->region, "__TEXT",  sizeof(seg->region) - 1);
            else if ((prot & VM_PROT_WRITE) && (prot & VM_PROT_READ))
                strncpy(seg->region, "__DATA",  sizeof(seg->region) - 1);
            else if (prot & VM_PROT_READ)
                strncpy(seg->region, "readonly", sizeof(seg->region) - 1);
            else
                strncpy(seg->region, "anon",    sizeof(seg->region) - 1);
        }

        seg->start_addr = ri->pri_address;
        seg->end_addr   = ri->pri_address + ri->pri_size;
        seg->size_kb    = (size_t)(ri->pri_size / 1024);

        /* Build permission string */
        int prot = ri->pri_protection;
        snprintf(seg->perms, sizeof(seg->perms), "%s%s%s",
                 (prot & VM_PROT_READ)    ? "r" : "-",
                 (prot & VM_PROT_WRITE)   ? "w" : "-",
                 (prot & VM_PROT_EXECUTE) ? "x" : "-");

        (*count)++;
        addr = ri->pri_address + ri->pri_size;
    }

    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * display_memory_map — render the memory segment table
 *
 * Shows a colour-coded bar visualisation of each segment's relative size,
 * plus a table with addresses, sizes, and permissions.
 * ─────────────────────────────────────────────────────────────────────── */
void display_memory_map(pid_t pid, const char *name,
                        MemSegment *segs, int count) {
    printf("\n");
    printf("  %s%sMemory Map: %s (PID %d)%s\n\n",
           COLOR_BOLD, COLOR_CYAN, name, pid, COLOR_RESET);

    if (count == 0) {
        printf("  (no readable regions — process may require elevated privileges)\n\n");
        return;
    }

    /* Find total mapped size for bar chart scaling */
    size_t total_kb = 0;
    for (int i = 0; i < count; i++)
        total_kb += segs[i].size_kb;

    /* Column header */
    printf("  %s%-20s  %18s  %18s  %10s  %5s  %s%s\n",
           COLOR_BOLD,
           "REGION", "START", "END", "SIZE(KB)", "PERMS", "BAR",
           COLOR_RESET);
    printf("  %s%s%s\n", COLOR_CYAN,
           "───────────────────────────────────────────────────────────────────────────",
           COLOR_RESET);

    for (int i = 0; i < count; i++) {
        MemSegment *s = &segs[i];

        /* Colour by permission */
        const char *col;
        if (s->perms[2] == 'x')      col = COLOR_RED;     /* executable */
        else if (s->perms[1] == 'w') col = COLOR_YELLOW;  /* writable */
        else                          col = COLOR_GREEN;   /* read-only */

        /* Bar: 20 chars wide, proportional to size */
        char bar[24] = "";
        if (total_kb > 0) {
            int w = (int)((double)s->size_kb / (double)total_kb * 20.0);
            if (w < 1 && s->size_kb > 0) w = 1;
            for (int b = 0; b < w && b < 20; b++) bar[b] = '#';
            bar[w] = '\0';
        }

        printf("  %s%-20.20s%s  0x%016llx  0x%016llx  %10zu  %5s  %s%s%s\n",
               col, s->region, COLOR_RESET,
               (unsigned long long)s->start_addr,
               (unsigned long long)s->end_addr,
               s->size_kb, s->perms,
               col, bar, COLOR_RESET);
    }

    printf("  %s%s%s\n", COLOR_CYAN,
           "───────────────────────────────────────────────────────────────────────────",
           COLOR_RESET);

    /* Summary */
    char total_str[32];
    if (total_kb >= 1024*1024)
        snprintf(total_str, sizeof(total_str), "%.2f GB", total_kb / (1024.0*1024.0));
    else if (total_kb >= 1024)
        snprintf(total_str, sizeof(total_str), "%.1f MB", total_kb / 1024.0);
    else
        snprintf(total_str, sizeof(total_str), "%zu KB", total_kb);

    printf("  %d regions  |  Total mapped: %s%s%s\n\n",
           count, COLOR_BOLD, total_str, COLOR_RESET);
}
