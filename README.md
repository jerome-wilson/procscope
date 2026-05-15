# ProcScope — Real-Time Process & Memory Monitor

A systems-programming project written in C that reads live process and system data directly from the macOS kernel. No external libraries, no network calls — every metric comes from a raw kernel interface.

---

## What It Does

ProcScope is a terminal tool that lets you inspect running processes, monitor CPU and memory in real time, watch individual processes, receive threshold alerts, and stream process data to other programs through a named pipe.

It was built to demonstrate a broad range of POSIX and macOS-specific system calls in a single, coherent application.

---

## System Calls Used — 20 Distinct Calls

| # | System Call | Where Used | Purpose |
|---|---|---|---|
| 1 | `sysctl()` | `proc.c` | Enumerate all PIDs from the kernel process table (`KERN_PROC_ALL`); read per-process name/ppid/start time (`KERN_PROC_PID`) |
| 2 | `sysctlbyname()` | `proc.c` | Read physical RAM total (`hw.memsize`) and swap usage (`vm.swapusage`) |
| 3 | `proc_pidinfo()` | `proc.c`, `memmap.c` | Read per-process CPU ticks, RSS, vsize, thread count (`PROC_PIDTASKINFO`); enumerate open file descriptors (`PROC_PIDLISTFDS`); enumerate virtual memory regions (`PROC_PIDREGIONPATHINFO`) |
| 4 | `proc_pidpath()` | `proc.c` | Resolve a PID to its full executable path |
| 5 | `clock_gettime()` | `proc.c` | Nanosecond wall-clock timestamps (`CLOCK_MONOTONIC`) for accurate CPU delta computation |
| 6 | `fork()` | `monitor.c`, `alert.c`, `daemon.c` | Spawn parallel worker processes for data collection; double-fork for daemon creation |
| 7 | `pipe()` | `monitor.c`, `alert.c` | IPC channel between parent and worker children — workers write `ProcessData` structs, parent reads |
| 8 | `read()` | `monitor.c`, `alert.c` | Read process data structs from pipes; read keyboard input in raw terminal mode |
| 9 | `write()` | `monitor.c`, `stream.c` | Write process data back through pipes; write JSON records to the named pipe |
| 10 | `waitpid()` | `monitor.c`, `alert.c` | Reap worker child processes after data collection |
| 11 | `select()` | `monitor.c`, `alert.c` | I/O multiplexing — wait for keyboard input with a 1-second timeout, enabling live refresh without blocking |
| 12 | `tcgetattr()` / `tcsetattr()` | `monitor.c` | Switch terminal to raw mode (no echo, no line buffering) for keystroke-driven TUI |
| 13 | `sigaction()` | `alert.c`, `daemon.c` | Install handlers for `SIGINT`, `SIGTERM`, `SIGALRM`, `SIGHUP`, `SIGUSR1`, `SIGUSR2`, `SIGCHLD` |
| 14 | `signal()` | `alert.c` | Ignore `SIGPIPE` — prevents crash when a named pipe reader disconnects |
| 15 | `kill()` | `daemon.c` | Probe process existence with signal 0; send `SIGTERM` / `SIGKILL` for daemon shutdown |
| 16 | `mkfifo()` | `stream.c` | Create a named pipe (FIFO) at `/tmp/procscope.fifo` for inter-process streaming |
| 17 | `open()` / `unlink()` | `stream.c`, `daemon.c` | Open the FIFO write-end (blocks until a reader connects); remove the FIFO on exit |
| 18 | `setsid()` | `daemon.c` | Make the daemon a session leader (detach from controlling terminal) |
| 19 | `dup2()` | `daemon.c` | Redirect `stdin` → `/dev/null`, `stdout`/`stderr` → log file in daemon mode |
| 20 | `mach_timebase_info()` | `proc.c` | Convert Mach CPU tick counts to nanoseconds for correct CPU% on Apple Silicon |

Additionally used: `host_statistics64()` (Mach page-level VM stats), `umask()`, `chdir()`, `sysconf()`, `mmap()` (history persistence in `history.c`).

---

## Why This Project Is Relevant

Most tools that "show process information" sit on top of `/proc` on Linux or invoke shell commands like `ps` and `top`. ProcScope bypasses all of that and speaks directly to the kernel through `sysctl` and the macOS `libproc` API — the same interfaces the real `top`, `Activity Monitor`, and `Instruments` use internally.

**Every feature maps to a concrete systems concept:**

| Feature | Concept Demonstrated |
|---|---|
| Live CPU % in `top` mode | CPU delta computation using Mach time ticks + `clock_gettime` |
| Parallel data collection | `fork` + `pipe` + `waitpid` — concurrent workers, IPC |
| Keyboard-aware refresh | `select` I/O multiplexing |
| Raw terminal TUI | `tcgetattr` / `tcsetattr`, alternate screen buffer |
| Signal handling | `sigaction`, safe `sig_atomic_t` flag pattern |
| `stream` command | `mkfifo`, `open`, `write` — named pipe IPC |
| `daemon start/stop` | Double-fork, `setsid`, `dup2`, PID files, `kill` |
| `mem <PID>` | VM region enumeration via `proc_pidinfo(PROC_PIDREGIONPATHINFO)` |
| Process history / sparklines | Circular buffer + mmap-backed persistence |
| System status bar chart | Mach `host_statistics64` + `sysctlbyname` |

---

## Build

```bash
make          # build
make clean    # remove build artefacts
make debug    # debug build with -g3 -O0
make release  # optimised build with -O3
```

**Requirements:** macOS 11+, Xcode Command Line Tools (`xcode-select --install`). No third-party dependencies.

---

## Commands

```
./procscope <command> [options]
```

| Command | Description |
|---|---|
| `status` | System-wide RAM/swap bar chart and process count |
| `list` | All running processes sorted by memory usage |
| `top [N] [--cpu\|--mem]` | Live TUI of top N processes (default: 10 by CPU) |
| `watch <PID\|name> ...` | Live TUI scoped to specific processes by PID or name |
| `mem <PID>` | Virtual memory segment map for a process |
| `alert <PID> --cpu N --mem N` | Threshold alerts — fires when CPU% or RSS exceeds limit |
| `stream [PID...]` | Stream process data as newline-delimited JSON to a named pipe |
| `daemon start\|stop\|status` | Run ProcScope as a background daemon |

---

## TUI Keyboard Controls

Available in `top` and `watch` modes:

| Key | Action |
|---|---|
| `Q` | Quit |
| `S` | Cycle sort: CPU → RSS → PID → Name |
| `+` | Faster refresh (minimum 1 s) |
| `-` | Slower refresh (maximum 30 s) |
| `P` | Pause / resume |
| `I` | Open insights panel (cycle through processes) |
| `Esc` | Close insights panel |

---

## Data Pipeline

```
Kernel (sysctl / proc_pidinfo)
        │
        ├─ fork() N workers ──► pipe() ──► parent collects ProcessData[]
        │
        ├─ sort + render TUI  (select for keyboard, tcsetattr for raw input)
        │
        └─ stream mode: mkfifo ──► open() ──► write() JSON ──► cat /tmp/procscope.fifo
```

---

## Project Structure

```
procscope/
├── src/
│   ├── main.c       Command dispatch
│   ├── proc.c       Kernel data collection (sysctl, proc_pidinfo, clock_gettime)
│   ├── monitor.c    TUI, fork/pipe workers, select loop, raw terminal
│   ├── memmap.c     VM region enumeration (proc_pidinfo PROC_PIDREGIONPATHINFO)
│   ├── alert.c      Threshold alerting, signal handlers
│   ├── stream.c     Named pipe streaming (mkfifo, open, write)
│   ├── system.c     System status display (Mach VM stats, sysctl)
│   ├── daemon.c     Daemon lifecycle (double-fork, setsid, dup2, kill)
│   ├── history.c    Circular buffer + mmap-backed price history
│   ├── ai.c         Statistical analysis (moving average, volatility, momentum)
│   ├── cli.c        Argument parsing
│   ├── logger.c     Timestamped logging
│   ├── config.c     Configuration management
│   └── utils.c      Shared utilities
├── include/
│   ├── procscope.h  Master header — all structs, constants, declarations
│   ├── daemon.h
│   ├── logger.h
│   ├── config.h
│   ├── ipc.h
│   └── history.h
├── data/            mmap history files
├── logs/            Daemon log output
└── Makefile
```

---

## Example Session

```bash
# System overview
./procscope status

# All processes by memory
./procscope list

# Live top 10 by CPU — use S to sort, I for insights, Q to quit
./procscope top

# Watch a specific process by name
./procscope watch Safari

# Memory map of process 1234
./procscope mem 1234

# Alert when PID 1234 exceeds 50% CPU or 512 MB RAM
./procscope alert 1234 --cpu 50 --mem 512

# Stream JSON to another terminal
./procscope stream &
cat /tmp/procscope.fifo
```
