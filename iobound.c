#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[]) {
  int i;

  printf(1, "I/O-bound test starting (PID %d)\n", getpid());

  for (i = 0; i < 50; i++) {
    printf(1, "Iteration %d\n", i);
    sleep(1);
  }

  printf(1, "I/O-bound test done\n");
  exit();
}
