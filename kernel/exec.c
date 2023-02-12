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
    if (argv[j] == '\0') {
      break;
    }
    j++;
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

  uint64_t uargs_ptr[j + 2]; // ptrs to args
  uargs_ptr[j + 1] = NULL;
  uargs_ptr[0] = 0x00; // doesn't matter
  uint64_t cur_ptr = SZ_2G;
  for (int i = 0; i < j; i++) {
    int len = strlen(argv[i]) + 1;
    cur_ptr -= len;

    vspacewritetova(&tmp, cur_ptr, (char*)argv[i], len);
    uargs_ptr[i + 1] = cur_ptr;
  }

  uint64_t limit = cur_ptr - ((SZ_2G - cur_ptr) % 8);  // base of argueuments, 8 bit aligned
  uint64_t base = limit - (j + 2) * 8;
  vspacewritetova(&tmp, base, (char*)uargs_ptr, (j + 2) * sizeof(char*));

  myproc()->tf->rdi = j;
  myproc()->tf->rsi = base + 8;
  myproc()->tf->rsp = base;

  if (vspacecopy(&myproc()->vspace, &tmp) == -1) {
    return -1;
  }
  vspaceinstall(myproc());

  vspacefree(&tmp);

  return 0;

  /*
  // pointers to arg
  char* args[j + 2];
  // null terminator at end
  args[j + 1] = '\0';
  int64_t total_size = 0;

  for (int i = 0; i < j; i++) {
    int64_t len = strlen(argv[i]) + 1; // for null terminator
    total_size += len;

    if (vspacewritetova(&tmp, SZ_2G - total_size, argv[i], len) == -1) {
      return -1;
    }
    args[i + 1] = (char*)(SZ_2G - total_size);
  }

  int64_t limit = SZ_2G - total_size - ((SZ_2G - total_size) % 8);

  // write down pointers to addresses
  int start_of_ptr = limit - sizeof(char*) * j;
  if (vspacewritetova(&tmp, start_of_ptr, args, sizeof(char*) * j) == -1) {
    return -1;
  }
  vspacedumpstack(&tmp);
  struct proc *p = myproc();
  //registers
  p->tf->rip = rip;
  p->tf->rsi = SZ_2G - total_size;
  p->tf->rdi = j;
  p->tf->rsp = args;  // doesn't matter

  if (vspacecopy(&myproc()->vspace, &tmp) == -1) {
    return -1;
  }
  vspaceinstall(myproc());

  vspacefree(&tmp);

  return -1;
  */
}
