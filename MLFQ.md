# Multi-Level Feedback Queue (MLFQ) Scheduler Implementation

This branch contains a complete implementation of a Multi-Level Feedback Queue scheduler for xv6, replacing the default round-robin process scheduler.

## Overview

The MLFQ scheduler dynamically adjusts process priorities based on observed behavior, providing superior responsiveness for interactive (I/O-bound) processes while maintaining fair CPU allocation for compute-intensive (CPU-bound) workloads.

**Key Features:**
- 4-level priority queue system (Queue 0 = highest, Queue 3 = lowest)
- Configurable time slices per priority level (5, 10, 20, 40 ticks)
- Automatic priority demotion based on CPU usage
- Periodic priority boosting to prevent starvation (every 100 ticks)
- Per-process CPU time tracking via new `ticks()` syscall

## Building and Running

### Build xv6 with MLFQ

```bash
cd xv6-MLFQ
make clean
make
```

### Run in QEMU

```bash
make qemu-nox
```

This starts xv6 in QEMU without graphical output. Type `Ctrl+A X` to exit.

## Benchmark Programs

Two benchmark programs are included to evaluate scheduler performance:

### schedtest - Universal Benchmark

Works on both round-robin and MLFQ schedulers. Measures turnaround time for mixed CPU-bound and I/O-bound workloads.

**Usage inside xv6:**
```
$ schedtest
```

**What it does:**
- Spawns 3 CPU-bound processes (500,000 iterations with nested loops)
- Spawns 3 I/O-bound processes (20 iterations with 2-tick sleeps)
- Measures wall-clock turnaround time using `uptime()` syscall
- Reports statistics comparing process types

### schedtest_mlfq - MLFQ-Specific Benchmark

Enhanced benchmark that uses the new `ticks()` syscall to report per-process CPU time consumption.

**Usage inside xv6:**
```
$ schedtest_mlfq
```

**What it does:**
- Same workload as `schedtest`
- Additionally reports actual CPU ticks consumed by each process
- Calculates scheduling overhead
- Provides detailed MLFQ analysis (priority levels, wait times, etc.)

## Modified Files

| File | Changes |
|------|---------|
| `proc.c` | Queue management, scheduler logic, yield/fork/exit/sleep/wakeup/kill modifications |
| `proc.h` | Added `priority`, `ticks_used`, `total_ticks`, `queue_next` to struct proc |
| `trap.c` | Timer interrupt handling for per-process CPU time tracking and priority boost checks |
| `param.h` | Added `NQUEUE=4` and `BOOST_ITVL=100` constants |
| `defs.h` | Added `check_priority_boost()` declaration |
| `sysproc.c` | Implemented `sys_ticks()` syscall |
| `syscall.c` | Registered `SYS_ticks` syscall handler |
| `syscall.h` | Added `SYS_ticks` syscall number |
| `user.h` | Added `ticks()` user-space function declaration |
| `usys.S` | Added `ticks` syscall stub |
| `Makefile` | Added benchmark programs to build |

## Performance Results

### Light Load (3 CPU-bound + 3 I/O-bound processes on 8-CPU system)

| Metric | Round-Robin | MLFQ | Improvement |
|--------|-------------|------|-------------|
| I/O turnaround | 50 ticks | 42 ticks | **16%** faster |
| CPU turnaround | 54 ticks | 55 ticks | ~0% (fair) |

### Heavy Load (15 CPU-bound + 15 I/O-bound processes on 8-CPU system)

| Metric | Round-Robin | MLFQ | Improvement |
|--------|-------------|------|-------------|
| I/O turnaround | 217 ticks | 98 ticks | **55%** faster |
| CPU turnaround | 261 ticks | 259 ticks | ~0% (fair) |

**Key Finding:** MLFQ provides dramatic improvements in I/O responsiveness under system load without penalizing CPU-bound throughput.

## Queue Architecture

```
Priority Level  Time Slice  Purpose
─────────────────────────────────────
Queue 0         5 ticks     Interactive processes (high I/O)
Queue 1         10 ticks    Mixed workload processes
Queue 2         20 ticks    CPU-bound but responsive
Queue 3         40 ticks    Long-running CPU-bound processes
```

**Scheduling algorithm:**
1. Scheduler always selects from the highest priority non-empty queue
2. Process runs for its designated time slice
3. If process yields early (I/O), it stays at current priority
4. If process uses full time slice, it's demoted to next lower queue
5. Every 100 ticks, all processes are boosted back to Queue 0

## New System Call: ticks()

Returns the total CPU ticks consumed by the calling process (uint64).

```c
#include "user.h"

uint64 t = ticks();  // Get CPU time consumed
```

This allows user programs to measure their actual CPU usage, distinct from wall-clock turnaround time.

## Comparison with Original xv6

**Original Round-Robin:**
- Single FIFO queue, all processes equal priority
- Linear scan of process table
- No I/O awareness, no priority adaptation
- Simple but poor interactive response time

**MLFQ Implementation:**
- Multiple priority queues, dynamic priority adjustment
- O(1) queue-based selection
- Intelligent I/O detection through priority demotion
- Complex but excellent interactive response time

## Testing Notes

- Console output from concurrent processes may be interleaved (expected)
- CPU-bound benchmarks run faster than expected due to 8 available CPUs
- To see dramatic MLFQ benefits, increase process count (15x15 test shows 55% improvement)
- All processes eventually complete; no starvation even under load

## Documentation

For complete design details, evaluation methodology, and implementation discussion, see:
- `../documentation/FINAL_PROJECT_REPORT.pdf` - Full project report
- `../documentation/BENCHMARK_ANALYSIS.md` - Detailed benchmark analysis

## Author

Conor McJannett  
CS 461: Operating Systems (42521) - Fall 2025

## References

- Arpaci-Dusseau & Arpaci-Dusseau: "Operating Systems: Three Easy Pieces" (Chapter 8)
- xv6 source code and documentation
- MIT PDOS xv6 teaching materials
