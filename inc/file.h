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
  struct inode* inode_ptr; // current inode
  int offset;  // Offset in file
  int mode;     // Modes (eg. O_RDONLY, O_WRONLY, ...)
  int ref;       // Reference count
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


int fileopen(char * path, int mode);
int filewrite(char *src, int fd, int n);
