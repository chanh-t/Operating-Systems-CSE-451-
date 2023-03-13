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

int sys_dup(void)
{
  // LAB1
  int fd;
  if (argint(0, &fd) < 0 || argfd(0, &fd) < 0) {  
    return -1;
  }
  return filedup(fd);
}

int sys_read(void)
{
  // LAB1
  int fd;
  int n;
  char *p;

  if (argint(0, &fd) < 0 || argfd(0, &fd) < 0 || argint(2, &n) < 0 || argptr(1, &p, n) < 0)
  {
    return -1;
  }

  return fileread(p, fd, n);
}

int sys_write(void)
{
  int fd;
  int n;
  char *p;

  if (argint(0, &fd) < 0 || argfd(0, &fd) < 0 || argint(2, &n) < 0 || argstr(1, &p) < 0 || argptr(1, &p, n) < 0)
  {
    return -1;
  }
  return filewrite(p, fd, n);
}

int sys_close(void)
{
  int fd;
  if (argint(0, &fd) < 0 || argfd(0, &fd) < 0) {
    return -1;
  }
  return fileclose(fd);
  // return -1;
}

int sys_fstat(void)
{
  // LAB1
  int fd;
  struct stat* fstat;
  if (argint(0, &fd) < 0 || argfd(0, &fd) < 0 || argptr(1, (char**)&fstat, sizeof(*fstat)) < 0)
  {
    return -1;
  }
  return filestat(fd, fstat);
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

int sys_open(void)
{
  // LAB1
  char *path; // param 1: path to file
  int mode;   // param 2: mode we want to open the file in

  if (argstr(0, &path) < 0 || argint(1, &mode) < 0)
  {
    return -1;
  }

  return fileopen(path, mode);
}

int sys_exec(void)
{
  // LAB2
  char* path;
  char* args[MAXARG];

  // grab arg0 (path)
  if (argstr(0, &path) == -1) {
    return -1;
  }
 // make sure ls exits and check wait()
 // check pipe (later?????)
 // write sys call for goofed locks (ls can't read)
 
  uint64_t addr; // address of the first pointer
  if (argint64(1, &addr) == -1) {
    return -1;
  }

  for (int i = 0; i < MAXARG; i++) {
    int64_t arg_addr;
    if (fetchint64_t(addr + 8 * i, &arg_addr) == -1) {
      return -1;
    }

    if (arg_addr == 0) {
      args[i] = 0;
      return exec(path, args);
    }

    if (fetchstr(arg_addr, &args[i]) == -1) {
      return -1;
    }
  }

  return -1;
}

int sys_pipe(void)
{  
  // LAB2
  int *fds;
  if (argptr(0, (char**)&fds, 2 * sizeof(int)) < 0) {
    return -1;
  }
  return filepipe(fds);
}

int sys_unlink(void)
{
  // LAB 4
  char* path;
  if (argstr(0, &path) < 0 || argptr(0, &path, strlen(path)) < 0) {
    return -1;
  }
  return fileunlink(path);
}