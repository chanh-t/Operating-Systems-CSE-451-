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
  int ret;
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
  if (vspaceloadcode(&tmp, path, &rip) == 0) {
    vspacefree(&tmp);
    return -1;
  }
  myproc()->tf->rip = rip;

  if (vspaceinitstack(&tmp, SZ_2G) == -1) {
    vspacefree(&tmp);
    return -1;
  }

  uint64_t uargs_ptr[size + 2];
  uargs_ptr[size + 1] = (uint64_t)NULL;
  uargs_ptr[0] = 0x00;

  uint64_t stack_p = SZ_2G;
  for (int i = 0; i < size; i++) {
    int len = strlen(argv[i]) + 1;
    stack_p -= len;

    ret = vspacewritetova(&tmp, stack_p, (char*)argv[i], len);

    if (ret < 0) {
      vspacefree(&tmp);
      return -1;
    }
    uargs_ptr[i + 1] = stack_p;
  }

  stack_p -= ((int)stack_p % 8);
  stack_p -= (sizeof(char*) * (size + 2));
  ret = vspacewritetova(&tmp, stack_p, (char *)uargs_ptr, (size + 2) * sizeof(char*));
  if (ret < 0) {
    vspacefree(&tmp);
    return -1;
  }

  myproc()->tf->rdi = size;
  myproc()->tf->rsi = stack_p + 8;
  myproc()->tf->rsp = stack_p;

  myproc()->vspace =tmp;

  vspaceinstall(myproc());
  vspacefree(&old);

  return 0;
}