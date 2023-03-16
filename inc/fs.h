#pragma once

#include "extent.h"
#include "param.h"
// On-disk file system format.
// Both the kernel and user programs use this header file.

#define INODEFILEINO 0 // inode file inum
#define ROOTINO 1      // root i-number
#define BSIZE 512      // block size
#define MAXEXTENT 30   // max extents

// Disk layout:
// [ boot block | super block | free bit map |
//                                          inode file | data blocks]
//
// mkfs computes the super block and builds an initial file system. The
// super block describes the disk layout:
struct superblock {
  uint size;       // Size of file system image (blocks)
  uint nblocks;    // Number of data blocks
  uint logstart;
  uint bmapstart;  // Block number of first free map block
  uint inodestart; // Block number of the start of inode file
};

// On-disk inode structure
struct dinode {
  short type;         // File type
  short devid;        // Device number (T_DEV only)
  uint size;          // Size of file (bytes)
  struct extent data[MAXEXTENT]; // Data blocks of file on disk
  char pad[8];       // So disk inodes fit contiguosly in a block
};

// offset of inode in inodefile
#define INODEOFF(inum) ((inum) * sizeof(struct dinode))

// Bitmap bits per block
#define BPB (BSIZE * 8)

// Block of free map containing bit for block b
#define BBLOCK(b, sb) ((b) / BPB + (sb).bmapstart)

// Directory is a file containing a sequence of dirent structures.
#define DIRSIZ 14

struct dirent {
  ushort inum;
  char name[DIRSIZ];
};

struct commit_block {
  uint size;
  uint commit_flag;       // indicates whether we are ready to commit or not
  uint target[LOGSIZE]; // 30 represents the log size
  char padding[BSIZE - sizeof(uint) * (LOGSIZE + 2)];
};