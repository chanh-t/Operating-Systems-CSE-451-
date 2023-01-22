//
// File descriptors
//
#include <proc.h>
#include <fcntl.h>
#include <stat.h>
#include <cdefs.h>
#include <defs.h>
#include <file.h>
#include <fs.h>
#include <param.h>
#include <sleeplock.h>
#include <spinlock.h>

struct devsw devsw[NDEV];
struct file_info file_table[NFILE];

static int find_free_fd(int i, struct proc* proc);

// Finds an open spot in the process open file table and has it point the global open file table entry.
// Finds an open entry in the global open file table and allocates a new file_info struct
// for the process open file table spot to point to.
// Will always open a device, and only open a file if permissions passed are O_RDONLY.

// Use the type field to check for a device. See stat.h for possible values.
// Returns the index into the process open file table as the file descriptor, or -1 on failure.
int fileopen(char* path, int mode) {
  struct inode* inode = namei(path); // returns pointer to inode
  struct proc* proc = myproc();
  if (inode == NULL) return -1;

  locki(inode);
  if (inode->type != T_DEV && mode != O_RDONLY) {
    unlocki(inode);
    return -1;
  }

  struct file_info tmp;
  tmp.inode_ptr = inode;
  tmp.mode = mode;
  tmp.offset = 0;
  tmp.ref = 1;

  int i = 0;
  while (i < NFILE) {
    if (file_table[i].inode_ptr == NULL &&
        file_table[i].mode == 0 &&
        file_table[i].offset == 0 &&
        file_table->ref == 0) {
    file_table[i] = tmp;
    break;
    }
    i++;
  }

  // no more file table space left
  if (i == NFILE) {
    unlocki(inode);
    return -1;
  }

  int fd = find_free_fd(i, proc);
  if (fd == -1) {
    unlocki(inode);
    return -1;
  }

  unlocki(inode);
  return fd;
}

static int find_free_fd(int i, struct proc* proc) {
  int j;
  j = 0;
  while (j < NOFILE) {
    if (proc->fd_table[j] == NULL) {
      proc->fd_table[j] = &file_table[i];
      break;
    }
    j++;
  }
  if (j == NOFILE) {
    return -1;
  }
  return j;
}