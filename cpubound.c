#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  int i, j;
  int sum = 0;

  printf(1, "CPU-bound test starting (PID %d)\n", getpid());

  for (i = 0; i < 1000000; i++) {
    for (j = 0; j < 300; j++) {
      sum += i * j;
    }
  }

  printf(1, "CPU-bound test done: sum=%d\n", sum);
  exit();
}
