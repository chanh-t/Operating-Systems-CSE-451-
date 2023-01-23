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

static int find_free_fd(int i, struct proc *proc);

// Finds an open spot in the process open file table and has it point the global open file table entry.
// Finds an open entry in the global open file table and allocates a new file_info struct
// for the process open file table spot to point to.
// Will always open a device, and only open a file if permissions passed are O_RDONLY.

// Use the type field to check for a device. See stat.h for possible values.
// Returns the index into the process open file table as the file descriptor, or -1 on failure.
int fileopen(char *path, int mode)
{
  struct inode *inode = namei(path); // returns pointer to inode
  struct proc *proc = myproc();
  if (inode == NULL)
    return -1;

  locki(inode);
  if (inode->type != T_DEV && mode != O_RDONLY)
  {
    unlocki(inode);
    return -1;
  }
  struct file_info tmp;
  tmp.inode_ptr = inode;
  tmp.mode = mode;
  tmp.offset = 0;
  tmp.ref = 1;

  int i = 0;
  while (i < NFILE)
  {
    if (file_table[i].inode_ptr == 0x0 &&
        file_table[i].mode == 0 &&
        file_table[i].offset == 0 &&
        file_table[i].ref == 0)
    {
      file_table[i] = tmp;
      break;
    }
    i++;
  }

  // no more file table space left
  if (i == NFILE)
  {
    unlocki(inode);
    return -1;
  }

  int fd = find_free_fd(i, proc);
  if (fd == -1)
  {
    unlocki(inode);
    return -1;
  }

  unlocki(inode);
  return fd;
}

static int find_free_fd(int i, struct proc *proc)
{
  int j;
  j = 0;
  while (j < NOFILE)
  {
    if (proc->fd_table[j] == NULL)
    {
      proc->fd_table[j] = &file_table[i];
      break;
    }
    j++;
  }
  if (j == NOFILE)
  {
    return -1;
  }
  return j;
}

int fileread(char *src, int fd, int n)
{
  struct proc *cur = myproc();
  struct file_info *fpointer = cur->fd_table[fd];
  if (fpointer == NULL || fpointer->mode == O_WRONLY)
  {
    return -1;
  }
  struct inode *ip = fpointer->inode_ptr;
  if (ip == NULL)
  {
    return -1;
  }
  int off = fpointer->offset;
  int ret = concurrent_readi(ip, src, off, n);
  fpointer->offset += ret;
  return ret;
}

int filewrite(char *src, int fd, int n)
{
  struct proc *cur = myproc();
  struct file_info *fpointer = cur->fd_table[fd];
  if (fpointer == NULL || fpointer->mode == O_RDONLY)
  {
    return -1;
  }
  struct inode *ip = fpointer->inode_ptr;
  if (ip == NULL)
  {
    return -1;
  }
  int off = fpointer->offset;
  int ret = concurrent_writei(ip, src, off, n);
  fpointer->offset += ret;
  return ret;
}

int fileclose(int fd)
{
  struct proc *cur = myproc();
  struct file_info *fpointer = cur->fd_table[fd];
  if (fpointer == NULL || fpointer -> ref == 0) {
    return -1;
  }
  fpointer -> ref -= 1;
  if (fpointer -> ref <= 0) {
    irelease(fpointer->inode_ptr);
    fpointer -> inode_ptr = NULL;
    fpointer -> mode = 0;
    fpointer -> offset = 0;
  }
  // cprintf("%d\n", fd);
  cur->fd_table[fd] = NULL;
  return 0;
}


int filedup(int fd)
{
  struct proc *cur = myproc();
  struct file_info *fpointer = cur->fd_table[fd];
  if (fpointer == NULL) {
    return -1;
  }
  int ret = -1;
  for (int i = 0; i < NOFILE; i++) {
    if (cur->fd_table[i] != NULL){
      continue;
    } else {
      cur->fd_table[i] = fpointer;
      fpointer -> ref += 1;
      ret = i;
      break;
    }
  }
  return ret;
}

int filestat(int fd, struct stat* fstat)
{
  struct proc *cur = myproc();
  struct file_info *fpointer = cur->fd_table[fd];
  struct inode* inode = fpointer->inode_ptr;
  if (fpointer == NULL || inode == NULL) {
    return -1;
  }
  fstat->type = inode->type;
  fstat->dev = inode->dev;
  fstat->ino = inode->inum;
  fstat->size = inode->size;
  return 0;
}