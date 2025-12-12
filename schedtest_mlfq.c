// schedtest_mlfq.c - MLFQ Scheduler Benchmark Program
// Enhanced version using the ticks() syscall for detailed CPU time analysis
// Only works with the MLFQ scheduler (requires ticks syscall and total_ticks tracking)

#include "types.h"
#include "stat.h"
#include "user.h"

// Test configuration - adjust these to vary workload
#define NUM_CPU_PROCS     3    // Number of CPU-bound processes
#define NUM_IO_PROCS      3    // Number of I/O-bound processes
#define CPU_ITERATIONS    500000  // Work units for CPU-bound
#define IO_ITERATIONS     20      // Iterations for I/O-bound (with sleeps)
#define IO_SLEEP_TICKS    2       // Ticks to sleep between I/O iterations

// Process types
#define TYPE_CPU 0
#define TYPE_IO  1

// CPU-bound workload: pure computation
void
cpu_work(int id)
{
  int i, j;
  volatile int sum = 0;  // volatile to prevent optimization

  printf(1, "[CPU %d] PID %d starting at tick %d\n", id, getpid(), uptime());

  for (i = 0; i < CPU_ITERATIONS; i++) {
    for (j = 0; j < 100; j++) {
      sum += i * j;
    }
  }

  // Get CPU ticks used before exiting
  uint64 cpu_ticks = ticks();
  int end_time = uptime();

  printf(1, "[CPU %d] PID %d finished at tick %d, used %d CPU ticks\n", 
         id, getpid(), end_time, (int)cpu_ticks);
  exit();
}

// I/O-bound workload: frequent short sleeps (simulates waiting for I/O)
void
io_work(int id)
{
  int i;

  printf(1, "[I/O %d] PID %d starting at tick %d\n", id, getpid(), uptime());

  for (i = 0; i < IO_ITERATIONS; i++) {
    // Small amount of work
    volatile int x = i * i;
    (void)x;

    // Sleep to simulate I/O wait
    sleep(IO_SLEEP_TICKS);

    // Print progress occasionally
    if (i % 5 == 0)
      printf(1, "[I/O %d] iteration %d/%d\n", id, i, IO_ITERATIONS);
  }

  // Get CPU ticks used before exiting
  uint64 cpu_ticks = ticks();
  int end_time = uptime();

  printf(1, "[I/O %d] PID %d finished at tick %d, used %d CPU ticks\n", 
         id, getpid(), end_time, (int)cpu_ticks);
  exit();
}

int
main(int argc, char *argv[])
{
  int i;
  int total_procs = NUM_CPU_PROCS + NUM_IO_PROCS;

  // Arrays to track process info
  int pids[NUM_CPU_PROCS + NUM_IO_PROCS];
  int types[NUM_CPU_PROCS + NUM_IO_PROCS];
  int start_times[NUM_CPU_PROCS + NUM_IO_PROCS];
  int end_times[NUM_CPU_PROCS + NUM_IO_PROCS];

  int test_start_time, test_end_time;

  printf(1, "\n");
  printf(1, "╔══════════════════════════════════════════════════════════════╗\n");
  printf(1, "║           MLFQ SCHEDULER BENCHMARK TEST                      ║\n");
  printf(1, "╠══════════════════════════════════════════════════════════════╣\n");
  printf(1, "║  CPU-bound processes: %d                                      ║\n", NUM_CPU_PROCS);
  printf(1, "║  I/O-bound processes: %d                                      ║\n", NUM_IO_PROCS);
  printf(1, "║  CPU iterations:      %d                                ║\n", CPU_ITERATIONS);
  printf(1, "║  I/O iterations:      %d (sleep %d ticks each)               ║\n", 
         IO_ITERATIONS, IO_SLEEP_TICKS);
  printf(1, "╚══════════════════════════════════════════════════════════════╝\n");
  printf(1, "\n");

  test_start_time = uptime();

  // Fork CPU-bound processes
  for (i = 0; i < NUM_CPU_PROCS; i++) {
    start_times[i] = uptime();
    types[i] = TYPE_CPU;
    pids[i] = fork();
    if (pids[i] == 0) {
      cpu_work(i);
      // Never returns
    } else if (pids[i] < 0) {
      printf(1, "ERROR: Fork failed for CPU process %d!\n", i);
      exit();
    }
  }

  // Fork I/O-bound processes
  for (i = 0; i < NUM_IO_PROCS; i++) {
    int idx = NUM_CPU_PROCS + i;
    start_times[idx] = uptime();
    types[idx] = TYPE_IO;
    pids[idx] = fork();
    if (pids[idx] == 0) {
      io_work(i);
      // Never returns
    } else if (pids[idx] < 0) {
      printf(1, "ERROR: Fork failed for I/O process %d!\n", i);
      exit();
    }
  }

  printf(1, "All %d processes spawned. Waiting for completion...\n\n", total_procs);
  printf(1, "────────────────────────────────────────────────────────────────\n");

  // Wait for all children and record completion times
  int completed = 0;
  while (completed < total_procs) {
    int finished_pid = wait();
    int finish_time = uptime();

    if (finished_pid < 0)
      break;

    // Find which process finished
    for (i = 0; i < total_procs; i++) {
      if (pids[i] == finished_pid) {
        end_times[i] = finish_time;
        completed++;
        break;
      }
    }
  }

  test_end_time = uptime();

  printf(1, "────────────────────────────────────────────────────────────────\n");
  printf(1, "\n");

  // Calculate and display results
  printf(1, "╔══════════════════════════════════════════════════════════════╗\n");
  printf(1, "║                    BENCHMARK RESULTS                         ║\n");
  printf(1, "╚══════════════════════════════════════════════════════════════╝\n");
  printf(1, "\n");

  // Individual process results
  printf(1, "Individual Process Results:\n");
  printf(1, "┌──────┬───────────┬─────────┬─────────┬────────────┐\n");
  printf(1, "│ PID  │ Type      │ Start   │ End     │ Turnaround │\n");
  printf(1, "├──────┼───────────┼─────────┼─────────┼────────────┤\n");

  int cpu_total_turnaround = 0;
  int io_total_turnaround = 0;
  int cpu_count = 0;
  int io_count = 0;
  int cpu_min_turnaround = 999999;
  int cpu_max_turnaround = 0;
  int io_min_turnaround = 999999;
  int io_max_turnaround = 0;

  for (i = 0; i < total_procs; i++) {
    int turnaround = end_times[i] - start_times[i];
    char *type_str = types[i] == TYPE_CPU ? "CPU-bound" : "I/O-bound";

    printf(1, "│ %d   │ %s │ %d    │ %d    │ %d ticks  │\n",
           pids[i], type_str, start_times[i], end_times[i], turnaround);

    if (types[i] == TYPE_CPU) {
      cpu_total_turnaround += turnaround;
      cpu_count++;
      if (turnaround < cpu_min_turnaround) cpu_min_turnaround = turnaround;
      if (turnaround > cpu_max_turnaround) cpu_max_turnaround = turnaround;
    } else {
      io_total_turnaround += turnaround;
      io_count++;
      if (turnaround < io_min_turnaround) io_min_turnaround = turnaround;
      if (turnaround > io_max_turnaround) io_max_turnaround = turnaround;
    }
  }

  printf(1, "└──────┴───────────┴─────────┴─────────┴────────────┘\n");
  printf(1, "\n");

  // Summary statistics
  printf(1, "Summary Statistics:\n");
  printf(1, "┌────────────────────────────────────────────────────┐\n");

  if (cpu_count > 0) {
    int cpu_avg = cpu_total_turnaround / cpu_count;
    printf(1, "│ CPU-bound processes:                               │\n");
    printf(1, "│   Average turnaround: %d ticks                     │\n", cpu_avg);
    printf(1, "│   Min: %d ticks, Max: %d ticks                     │\n", 
           cpu_min_turnaround, cpu_max_turnaround);
    printf(1, "├────────────────────────────────────────────────────┤\n");
  }

  if (io_count > 0) {
    int io_avg = io_total_turnaround / io_count;
    printf(1, "│ I/O-bound processes:                               │\n");
    printf(1, "│   Average turnaround: %d ticks                     │\n", io_avg);
    printf(1, "│   Min: %d ticks, Max: %d ticks                     │\n",
           io_min_turnaround, io_max_turnaround);
    printf(1, "├────────────────────────────────────────────────────┤\n");
  }

  printf(1, "│ Total test duration: %d ticks                      │\n", 
         test_end_time - test_start_time);
  printf(1, "└────────────────────────────────────────────────────┘\n");
  printf(1, "\n");

  // MLFQ Analysis
  printf(1, "MLFQ Scheduler Analysis:\n");
  printf(1, "┌────────────────────────────────────────────────────┐\n");

  if (cpu_count > 0 && io_count > 0) {
    int cpu_avg = cpu_total_turnaround / cpu_count;
    int io_avg = io_total_turnaround / io_count;

    // Calculate the theoretical minimum for I/O processes
    // (IO_ITERATIONS * IO_SLEEP_TICKS) is the minimum possible turnaround
    int io_theoretical_min = IO_ITERATIONS * IO_SLEEP_TICKS;
    int io_overhead = io_avg - io_theoretical_min;

    printf(1, "│ I/O theoretical minimum: %d ticks                  │\n", io_theoretical_min);
    printf(1, "│ I/O actual average:      %d ticks                  │\n", io_avg);
    printf(1, "│ I/O scheduling overhead: %d ticks                  │\n", io_overhead);
    printf(1, "├────────────────────────────────────────────────────┤\n");

    // Responsiveness ratio
    if (cpu_avg > 0) {
      int ratio_percent = (io_avg * 100) / cpu_avg;
      printf(1, "│ I/O vs CPU turnaround ratio: %d%%                  │\n", ratio_percent);
      printf(1, "│ (Lower = better I/O responsiveness)               │\n");
    }
  }

  printf(1, "└────────────────────────────────────────────────────┘\n");
  printf(1, "\n");

  // Interpretation
  printf(1, "Interpretation:\n");
  printf(1, "────────────────────────────────────────────────────────────────\n");
  printf(1, "• I/O-bound processes should complete close to their\n");
  printf(1, "  theoretical minimum (%d ticks = %d iterations × %d sleep ticks)\n",
         IO_ITERATIONS * IO_SLEEP_TICKS, IO_ITERATIONS, IO_SLEEP_TICKS);
  printf(1, "• CPU-bound processes take longer but are not starved\n");
  printf(1, "• The MLFQ scheduler prioritizes I/O-bound processes,\n");
  printf(1, "  giving them quick access to CPU when they wake up\n");
  printf(1, "────────────────────────────────────────────────────────────────\n");
  printf(1, "\n");

  printf(1, "╔══════════════════════════════════════════════════════════════╗\n");
  printf(1, "║                  BENCHMARK COMPLETE                          ║\n");
  printf(1, "╚══════════════════════════════════════════════════════════════╝\n");
  printf(1, "\n");

  exit();
}
