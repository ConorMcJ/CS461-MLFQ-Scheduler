// schedtest.c - Scheduler Benchmark Program
// Tests CPU-bound and I/O-bound process performance
// Compatible with both Round-Robin and MLFQ schedulers

#include "types.h"
#include "stat.h"
#include "user.h"

#define NUM_CPU_PROCS 3    // Number of CPU-bound processes
#define NUM_IO_PROCS  3    // Number of I/O-bound processes
#define CPU_ITERATIONS 500000  // Work units for CPU-bound
#define IO_ITERATIONS  20      // Iterations for I/O-bound (with sleeps)
#define IO_SLEEP_TICKS 2       // Ticks to sleep between I/O iterations

// Shared completion tracking via exit codes isn't possible,
// so we'll have parent track child completion times

struct proc_stats {
  int pid;
  int type;        // 0 = CPU-bound, 1 = I/O-bound
  int start_time;
  int end_time;
  int turnaround;
};

// CPU-bound workload: pure computation
void
cpu_work(int id)
{
  int i, j;
  volatile int sum = 0;  // volatile to prevent optimization

  printf(1, "[CPU %d] PID %d starting\n", id, getpid());

  for (i = 0; i < CPU_ITERATIONS; i++) {
    for (j = 0; j < 100; j++) {
      sum += i * j;
    }
  }

  printf(1, "[CPU %d] PID %d finished (sum=%d)\n", id, getpid(), sum);
  exit();
}

// I/O-bound workload: frequent short sleeps (simulates waiting for I/O)
void
io_work(int id)
{
  int i;

  printf(1, "[I/O %d] PID %d starting\n", id, getpid());

  for (i = 0; i < IO_ITERATIONS; i++) {
    // Small amount of work
    volatile int x = i * i;
    (void)x;

    // Sleep to simulate I/O wait
    sleep(IO_SLEEP_TICKS);

    // Simulate I/O completion - print progress
    if (i % 5 == 0)
      printf(1, "[I/O %d] iteration %d/%d\n", id, i, IO_ITERATIONS);
  }

  printf(1, "[I/O %d] PID %d finished\n", id, getpid());
  exit();
}

int
main(int argc, char *argv[])
{
  int i;
  int pids[NUM_CPU_PROCS + NUM_IO_PROCS];
  int types[NUM_CPU_PROCS + NUM_IO_PROCS];  // 0=CPU, 1=IO
  int start_times[NUM_CPU_PROCS + NUM_IO_PROCS];
  int total_procs = NUM_CPU_PROCS + NUM_IO_PROCS;
  int test_start_time, test_end_time;

  printf(1, "\n========================================\n");
  printf(1, "   SCHEDULER BENCHMARK TEST\n");
  printf(1, "========================================\n");
  printf(1, "CPU-bound processes: %d\n", NUM_CPU_PROCS);
  printf(1, "I/O-bound processes: %d\n", NUM_IO_PROCS);
  printf(1, "CPU iterations: %d\n", CPU_ITERATIONS);
  printf(1, "I/O iterations: %d (sleep %d ticks each)\n", 
         IO_ITERATIONS, IO_SLEEP_TICKS);
  printf(1, "========================================\n\n");

  test_start_time = uptime();

  // Fork CPU-bound processes first
  for (i = 0; i < NUM_CPU_PROCS; i++) {
    start_times[i] = uptime();
    types[i] = 0;  // CPU-bound
    pids[i] = fork();
    if (pids[i] == 0) {
      // Child process
      cpu_work(i);
      // Never returns
    } else if (pids[i] < 0) {
      printf(1, "Fork failed!\n");
      exit();
    }
  }

  // Fork I/O-bound processes
  for (i = 0; i < NUM_IO_PROCS; i++) {
    int idx = NUM_CPU_PROCS + i;
    start_times[idx] = uptime();
    types[idx] = 1;  // I/O-bound
    pids[idx] = fork();
    if (pids[idx] == 0) {
      // Child process
      io_work(i);
      // Never returns
    } else if (pids[idx] < 0) {
      printf(1, "Fork failed!\n");
      exit();
    }
  }

  printf(1, "\nAll %d processes spawned. Waiting for completion...\n\n", total_procs);

  // Wait for all children and record completion times
  int end_times[NUM_CPU_PROCS + NUM_IO_PROCS];
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

  // Calculate and display results
  printf(1, "\n========================================\n");
  printf(1, "           RESULTS\n");
  printf(1, "========================================\n\n");

  int cpu_total_turnaround = 0;
  int io_total_turnaround = 0;
  int cpu_count = 0;
  int io_count = 0;

  printf(1, "Individual Process Results:\n");
  printf(1, "-------------------------------------------\n");
  printf(1, "PID   Type        Start   End     Turnaround\n");
  printf(1, "-------------------------------------------\n");

  for (i = 0; i < total_procs; i++) {
    int turnaround = end_times[i] - start_times[i];
    char *type_str = types[i] == 0 ? "CPU-bound" : "I/O-bound";

    printf(1, "%d     %s   %d      %d      %d ticks\n", 
           pids[i], type_str, start_times[i], end_times[i], turnaround);

    if (types[i] == 0) {
      cpu_total_turnaround += turnaround;
      cpu_count++;
    } else {
      io_total_turnaround += turnaround;
      io_count++;
    }
  }

  printf(1, "-------------------------------------------\n\n");

  // Summary statistics
  printf(1, "Summary Statistics:\n");
  printf(1, "-------------------------------------------\n");

  if (cpu_count > 0) {
    printf(1, "CPU-bound avg turnaround: %d ticks\n", 
           cpu_total_turnaround / cpu_count);
  }

  if (io_count > 0) {
    printf(1, "I/O-bound avg turnaround: %d ticks\n", 
           io_total_turnaround / io_count);
  }

  printf(1, "Total test time: %d ticks\n", test_end_time - test_start_time);
  printf(1, "-------------------------------------------\n");

  printf(1, "\n========================================\n");
  printf(1, "   BENCHMARK COMPLETE\n");
  printf(1, "========================================\n\n");

  exit();
}
