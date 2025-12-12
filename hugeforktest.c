#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  int i;
  int cpid;
  cpid = fork();
  if (cpid) {
    for (i = 0; i < 10; i++) {
      printf(1, "iobound iteration %d\n", i);
      sleep(10);
    }
    wait();
  } else {
    for (i = 0; i < 500; i++) {
      printf(1, "cpubound iteration %d\n", i);
    }
  }
  exit();
}
