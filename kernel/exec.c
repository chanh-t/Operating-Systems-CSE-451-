#include <cdefs.h>
#include <defs.h>
#include <elf.h>
#include <memlayout.h>
#include <mmu.h>
#include <param.h>
#include <proc.h>
#include <trap.h>
#include <x86_64.h>

int exec(char *path, char **argv) {
  // your code here
  int i = 0;
  while (true) {
    if (!strncmp(args[i], '\0', 1)) {
      i++;
      break;
    }
    i++;
  }

  struct vspace tmp;
  vspaceinit(&tmp);

  uint64_t rip;
  vspaceloadcode(&tmp, path, rip);

  vspaceinitstack(&tmp, SZ_2G);

  char* args[n];
  int total_size = 0;
  for (int i = 0; i < n; i++) {
    int len = strlen(argv[i]) + 1; // for null terminator
    total_size += len;

    vspacewritetova(&tmp, SZ_2G - total_size, argv[i], len);
    args[i] = SZ_2G - total_size;
  }

  while (total_size % 8 == 0) {
    vspacewritetova(&tmp, SZ_2G - total_size - 1, "\0", 1);
  }
  
  

  return -1;
}
