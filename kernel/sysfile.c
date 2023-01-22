//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include <cdefs.h>
#include <defs.h>
#include <fcntl.h>
#include <file.h>
#include <fs.h>
#include <mmu.h>
#include <param.h>
#include <proc.h>
#include <sleeplock.h>
#include <spinlock.h>
#include <stat.h>

int sys_dup(void) {
  // LAB1
  return -1;
}

int sys_read(void) {
  // LAB1
  return -1;
}

int sys_write(void) {
  // you have to change the code in this function.
  // Currently it supports printing one character to the screen.

  int fd; // file descriptor
  char *p; // buffer of bytes to write to fd
  int n; // # of bytes to write of p

  if (argint(2, &n) < 0 || argptr(1, &p, n) < 0 /*|| argfd(0, fd)*/ ) {
    return -1;
  }

  uartputc((int)(*p)); // <- TODO: Replace function
  return 1;
}

int sys_close(void) {
  // LAB1
  return -1;
}

int sys_fstat(void) {
  // LAB1
  return -1;
}


/*
 * arg0: char * [path to the file]
 * arg1: int [mode for opening the file (see inc/fcntl.h)]
 *
 * Given a pathname for a file, sys_open() returns a file descriptor, a small,
 * nonnegative integer for use in subsequent system calls. The file descriptor
 * returned by a successful call will be the lowest-numbered file descriptor
 * not currently open for the process.
 *
 * Each open file maintains a current position, initially zero.
 *
 * returns -1 on error
 *
 * Errors:
 * arg0 points to an invalid or unmapped address 
 * there is an invalid address before the end of the string 
 * the file does not exist
 * there is no available file descriptor 
 * since the file system is read only, any write flags for non console files are invalid
 * O_CREATE is not permitted (for now)
 *
 * note that for lab1, the file system does not support file create
 */

int sys_open(void) {
  // LAB1
  char* path; // param 1: path to file
  int mode; // param 2: mode we want to open the file in

  if (argstr(0, &path) || argint(1, &mode)) {
    return -1;
  }

  if (mode != O_RDONLY) {
    return -1;
  }
  return fileopen(path, mode);
}

int sys_exec(void) {
  // LAB2
  return -1;
}

int sys_pipe(void) {
  // LAB2
  return -1;
}

int sys_unlink(void) {
  // LAB 4
  return -1;
}