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
  int j = 0;
  while (true) {
    if (strncmp(argv[j], '\0', 1) == 0) {
      break;
    }
    j++;
  }

  struct vspace tmp;
  vspaceinit(&tmp);

  uint64_t rip;
  vspaceloadcode(&tmp, path, &rip);

  vspaceinitstack(&tmp, SZ_2G);

  char* args[j];
  int total_size = 0;
  for (int i = 0; i < j; i++) {
    int len = strlen(argv[i]) + 1; // for null terminator
    total_size += len;

    vspacewritetova(&tmp, SZ_2G - total_size, argv[i], len);
    args[i] = (char*)(SZ_2G - total_size);
  }

  int limit = SZ_2G - total_size - ((SZ_2G - total_size) % 8);

  int start_of_ptr = limit - sizeof(char*) * j;
  vspacewritetova(&tmp, start_of_ptr, args, sizeof(char*) * j);

  struct proc *p = myproc();
  //registers
  p->tf->rip = rip;
  p->tf->rsi = SZ_2G - total_size;
  p->tf->rdi = j;
  p->tf->rsp = SZ_2G;  // doesn't matter

  vspacecopy(&myproc()->vspace, &tmp);
  vspaceinstall(myproc());

  vspacefree(&tmp);

  return -1;
}
