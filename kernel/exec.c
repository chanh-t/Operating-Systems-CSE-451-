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

  struct vspace old = myproc()->vspace;

  int size = 0;
  while (true) {
    if (argv[size] == NULL) {
      break;
    }
    size++;
  }

  struct vspace tmp;
  if (vspaceinit(&tmp) == -1) {
    return -1;
  }

  uint64_t rip;
  vspaceloadcode(&tmp, path, &rip);
  myproc()->tf->rip = rip;

  if (vspaceinitstack(&tmp, SZ_2G) == -1) {
    return -1;
  }

  char* uargs_ptr[size + 2];
  uargs_ptr[size + 1] = NULL;
  uargs_ptr[0] = 0x00; //

  char* stack_p = SZ_2G;
  for (int i = 0; i < size; i++) {
    int len = strlen(argv[i]) + 1;
    stack_p -= len;

    vspacewritetova(&tmp, stack_p, (char*)argv[i], len);
    uargs_ptr[i + 1] = stack_p;
  }

  stack_p -= ((int)stack_p % 8);
  stack_p -= (sizeof(char*) * (size + 2));
  vspacewritetova(&tmp, stack_p, uargs_ptr, (size + 2) * sizeof(char*));

  myproc()->tf->rdi = size;
  myproc()->tf->rsi = stack_p + 8;
  myproc()->tf->rsp = stack_p;

  if (vspacecopy(&myproc()->vspace, &tmp) == -1) {
    return -1;
  }
  vspaceinstall(myproc());

  // vspacefree(&old);

  return 0;
}