/*
 * ProcScope - Real-Time Process & Memory Monitor
 * proc.c — kernel data collection via sysctl() and proc_pidinfo()
 *
 * System calls used:
 *   sysctl()      — read kernel process table and system memory stats
 *   proc_pidinfo()— per-process CPU ticks, RSS, vsize, FD list, VM regions
 *   proc_pidpath()— resolve executable path for a PID
 *   clock_gettime()— nanosecond timestamps for CPU delta computation
 */

#include "procscope.h"
#include <mach/mach_time.h>

/* ─────────────────────────────────────────────────────────────────────────
 * get_all_pids — return all current PIDs via sysctl(KERN_PROC_ALL)
 *
 * sysctl(CTL_KERN, KERN_PROC, KERN_PROC_ALL, ...) fills a buffer with
 * kinfo_proc structs. We extract the pid from each entry.
 * ─────────────────────────────────────────────────────────────────────── */
int get_all_pids(pid_t *pids, int max_count) {
    int    mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0 };
    size_t size   = 0;
    int    count  = 0;

    /* First call: query required buffer size */
    if (sysctl(mib, 4, NULL, &size, NULL, 0) < 0)
        return -1;

    struct kinfo_proc *procs = malloc(size);
    if (!procs) return -1;

    /* Second call: fill buffer */
    if (sysctl(mib, 4, procs, &size, NULL, 0) < 0) {
        free(procs);
        return -1;
    }

    int n = (int)(size / sizeof(struct kinfo_proc));
    for (int i = 0; i < n && count < max_count; i++) {
        pid_t pid = procs[i].kp_proc.p_pid;
        if (pid > 0)
            pids[count++] = pid;
    }

    free(procs);
    return count;
}

/* ─────────────────────────────────────────────────────────────────────────
 * get_process_data — fill ProcessData for one PID
 *
 * Uses:
 *   proc_pidinfo(PROC_PIDTASKINFO) — CPU ticks, RSS, vsize, threads
 *   sysctl(KERN_PROC_PID)         — process name, ppid, start time
 *   proc_pidpath()                — full executable path
 *   clock_gettime(CLOCK_MONOTONIC)— timestamp for CPU delta
 * ─────────────────────────────────────────────────────────────────────── */
int get_process_data(pid_t pid, ProcessData *out) {
    if (!out || pid <= 0) return -1;

    memset(out, 0, sizeof(ProcessData));
    out->pid   = pid;
    out->valid = 0;

    /* ── Task info: CPU ticks, RSS, vsize, threads ── */
    struct proc_taskinfo ti;
    int r = proc_pidinfo(pid, PROC_PIDTASKINFO, 0, &ti, sizeof(ti));
    if (r <= 0) return -1;

    out->rss_kb        = (long)(ti.pti_resident_size / 1024);
    out->vsize_kb      = (long)(ti.pti_virtual_size  / 1024);
    out->thread_count  = (int)ti.pti_threadnum;
    out->cpu_ticks_curr = ti.pti_total_user + ti.pti_total_system;

    /* Nanosecond timestamp for delta calculation */
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    out->sample_time_ns = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;

    /* ── Process name and ppid via sysctl(KERN_PROC_PID) ── */
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, (int)pid };
    struct kinfo_proc kp;
    size_t sz = sizeof(kp);
    if (sysctl(mib, 4, &kp, &sz, NULL, 0) == 0 && sz > 0) {
        strncpy(out->name, kp.kp_proc.p_comm, MAX_NAME_LEN - 1);
        out->ppid       = kp.kp_eproc.e_ppid;
        out->start_time = kp.kp_proc.p_starttime.tv_sec;
    } else {
        snprintf(out->name, sizeof(out->name), "pid%d", pid);
    }

    /* ── Executable path ── */
    proc_pidpath(pid, out->path, sizeof(out->path));

    /* ── Open file descriptor count ── */
    out->fd_count = get_fd_count(pid);

    out->valid = 1;
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * get_process_path — resolve full executable path for a PID
 * ─────────────────────────────────────────────────────────────────────── */
int get_process_path(pid_t pid, char *path, size_t size) {
    int r = proc_pidpath(pid, path, (uint32_t)size);
    return (r > 0) ? 0 : -1;
}

/* ─────────────────────────────────────────────────────────────────────────
 * get_fd_count — count open file descriptors for a process
 *
 * proc_pidinfo(PROC_PIDLISTFDS) returns an array of proc_fdinfo structs.
 * We divide total bytes by struct size to get fd count.
 * ─────────────────────────────────────────────────────────────────────── */
int get_fd_count(pid_t pid) {
    int sz = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, NULL, 0);
    if (sz <= 0) return 0;
    return sz / (int)sizeof(struct proc_fdinfo);
}

/* ─────────────────────────────────────────────────────────────────────────
 * get_system_memory — total/used/free RAM and swap via sysctl + Mach
 *
 * sysctl("hw.memsize")         — physical RAM total
 * host_statistics64(HOST_VM_INFO64) — page counts (free, active, inactive, wired)
 * sysctl("vm.swapusage")       — swap total and used
 * ─────────────────────────────────────────────────────────────────────── */
int get_system_memory(SystemMemory *mem) {
    if (!mem) return -1;
    memset(mem, 0, sizeof(SystemMemory));

    /* Total physical RAM */
    uint64_t total_bytes = 0;
    size_t sz = sizeof(total_bytes);
    sysctlbyname("hw.memsize", &total_bytes, &sz, NULL, 0);
    mem->total_kb = (long)(total_bytes / 1024);

    /* Page-level breakdown via Mach */
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    vm_statistics64_data_t vmstat;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                          (host_info64_t)&vmstat, &count) == KERN_SUCCESS) {
        vm_size_t page_size;
        host_page_size(mach_host_self(), &page_size);
        long free_kb  = (long)((vmstat.free_count * page_size) / 1024);
        mem->free_kb  = free_kb;
        mem->used_kb  = mem->total_kb - free_kb;
    }

    /* Swap usage */
    struct xsw_usage swu;
    sz = sizeof(swu);
    if (sysctlbyname("vm.swapusage", &swu, &sz, NULL, 0) == 0) {
        mem->swap_total_kb = (long)(swu.xsu_total / 1024);
        mem->swap_used_kb  = (long)(swu.xsu_used  / 1024);
    }

    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * find_pids_by_name — find all PIDs whose process name contains `name`
 * Case-insensitive substring match.
 * ─────────────────────────────────────────────────────────────────────── */
int find_pids_by_name(const char *name, pid_t *pids, int max) {
    pid_t all[MAX_PROCESSES];
    int   total = get_all_pids(all, MAX_PROCESSES);
    if (total <= 0) return 0;

    int found = 0;
    char upper_target[MAX_NAME_LEN];
    strncpy(upper_target, name, sizeof(upper_target) - 1);
    upper_target[sizeof(upper_target) - 1] = '\0';
    str_to_upper(upper_target);

    for (int i = 0; i < total && found < max; i++) {
        ProcessData pd;
        if (get_process_data(all[i], &pd) != 0) continue;
        char upper_name[MAX_NAME_LEN];
        strncpy(upper_name, pd.name, sizeof(upper_name) - 1);
        upper_name[sizeof(upper_name) - 1] = '\0';
        str_to_upper(upper_name);
        if (strstr(upper_name, upper_target))
            pids[found++] = all[i];
    }
    return found;
}

/* ─────────────────────────────────────────────────────────────────────────
 * compute_cpu_percent — compute CPU% from two ProcessData snapshots
 *
 * Formula:
 *   delta_ticks = curr_ticks - prev_ticks    (in Mach absolute time units)
 *   delta_ns    = curr_time_ns - prev_time_ns
 *   cpu%        = (delta_ticks_ns / delta_ns) * 100
 *
 * Mach time units are nanoseconds on Apple Silicon and most modern Macs
 * (mach_timebase_info numer/denom = 1/1). We apply the conversion factor.
 * ─────────────────────────────────────────────────────────────────────── */
void compute_cpu_percent(ProcessData *prev, ProcessData *curr) {
    if (!prev || !curr || !prev->valid || !curr->valid) {
        curr->cpu_percent = 0.0;
        return;
    }

    if (curr->sample_time_ns <= prev->sample_time_ns) {
        curr->cpu_percent = 0.0;
        return;
    }

    /* Convert Mach CPU ticks to nanoseconds */
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);

    uint64_t delta_ticks = curr->cpu_ticks_curr - prev->cpu_ticks_curr;
    uint64_t delta_ticks_ns = delta_ticks * tb.numer / tb.denom;
    uint64_t delta_wall_ns  = curr->sample_time_ns - prev->sample_time_ns;

    if (delta_wall_ns == 0) {
        curr->cpu_percent = 0.0;
        return;
    }

    double pct = ((double)delta_ticks_ns / (double)delta_wall_ns) * 100.0;
    /* Clamp to [0, 100 * thread_count] — can exceed 100% on multi-core */
    if (pct < 0.0) pct = 0.0;
    curr->cpu_percent = pct;

    /* Carry forward ticks for next delta */
    curr->cpu_ticks_prev = prev->cpu_ticks_curr;
}
