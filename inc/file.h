#pragma once

#include <extent.h>
#include <sleeplock.h>

// in-memory copy of an inode
struct inode {
  uint dev;  // Device number
  uint inum; // Inode number
  int ref;   // Reference count
  int valid; // Flag for if node is valid
  struct sleeplock lock;

  short type; // copy of disk inode
  short devid;
  uint size;
  struct extent data;
};

// file 
struct file_info {
  struct sleeplock lock; // sleep lock for file struct to ensure synchronization
  struct inode* inode_ptr; // current inode
  int offset;  // Offset in file
  int mode;     // Modes (eg. O_RDONLY, O_WRONLY, ...)
  int ref;       // Reference count
  int is_pipe; // if 1 then is pipe otherwise 0 
  struct file_pipe* pipe;
};

// table mapping device ID (devid) to device functions
struct devsw {
  int (*read)(struct inode *, char *, int);
  int (*write)(struct inode *, char *, int);
};

extern struct devsw devsw[];

// Device ids
enum {
  CONSOLE = 1,
};

struct file_pipe {
  char* buffer;
  struct spinlock lock;
  int offset_read; //offset for reader
  int offset_write; //offset for writer
  int reader; // reference count for reader
  int writer; // reference count for writer
  int full; // if full, set to 1 else 0
  int empty; // if empty, set to 1 else 0
};

int fileopen(char * path, int mode);
int filewrite(char *src, int fd, int n);
int fileread(char *src, int fd, int n);
int filedup(int fd);
int filestat(int fd, struct stat* fstat);
int fileclose(int fd);

