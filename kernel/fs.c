// File system implementation.  Five layers:
//   + Blocks: allocator for raw disk blocks.
//   + Files: inode allocator, reading, writing, metadata.
//   + Directories: inode with special contents (list of other inodes!)
//   + Names: paths like /usr/rtm/xk/fs.c for convenient naming.
//
// This file contains the low-level file system manipulation
// routines.  The (higher-level) system call implementations
// are in sysfile.c.

#include <cdefs.h>
#include <defs.h>
#include <file.h>
#include <fs.h>
#include <mmu.h>
#include <param.h>
#include <proc.h>
#include <sleeplock.h>
#include <spinlock.h>
#include <stat.h>

#include <buf.h>

// there should be one superblock per disk device, but we run with
// only one device
struct superblock sb;

// in memory version of commit_block
struct commit_block in_mem_cb;

struct sleeplock fslock;

void log_apply() {
  struct buf *commit_buf = bread(ROOTDEV, sb.logstart);
  struct commit_block commit_block;
  memmove(&commit_block, commit_buf->data, BSIZE);
  brelse(commit_buf);

  if (commit_block.commit_flag == 1) {
    for (int i = 0; i < commit_block.size; i++) {
      // write to block
      struct buf* src = bread(ROOTDEV, sb.logstart + 1 + i);
      struct buf* dst = bread(ROOTDEV, commit_block.target[i]);
      memmove(dst->data, src->data, BSIZE);
      bwrite(dst);
      brelse(dst);
      brelse(src);
    }
  }

  commit_buf = bread(ROOTDEV, sb.logstart);
  memset(commit_buf->data, 0, BSIZE);
  bwrite(commit_buf);
  brelse(commit_buf);
  memset(&in_mem_cb, 0, sizeof(commit_block));

}


void log_commit_tx() {
  acquiresleep(&fslock);
  struct buf* commit_b = bread(ROOTDEV, sb.logstart);
  in_mem_cb.commit_flag = 1;
  memmove(commit_b->data, &in_mem_cb, sizeof(struct commit_block));

  bwrite(commit_b);
  brelse(commit_b);
  releasesleep(&fslock);
  log_apply();
}

// finished (needs to call log_recover())
void log_write(struct buf* buf) {
  acquiresleep(&fslock);
  uint blocknom_orig = buf->blockno;
  buf->blockno = sb.logstart + 1 + in_mem_cb.size;
  bwrite(buf);
  buf->blockno = blocknom_orig;
  in_mem_cb.size++;
  releasesleep(&fslock);
}

// Read the super block.
void readsb(int dev, struct superblock *sb) {
  struct buf *bp;

  bp = bread(dev, 1);
  memmove(sb, bp->data, sizeof(*sb));
  brelse(bp);
}

// mark [start, end] bit in bp->data to 1 if used is true, else 0
static void bmark(struct buf *bp, uint start, uint end, bool used)
{
  int m, bi;
  for (bi = start; bi <= end; bi++) {
    m = 1 << (bi % 8);
    if (used) {
      bp->data[bi/8] |= m;  // Mark block in use.
    } else {
      if((bp->data[bi/8] & m) == 0)
        panic("freeing free block");
      bp->data[bi/8] &= ~m; // Mark block as free.
    }
  }
  bp->flags |= B_DIRTY; // mark our update
  log_write(bp);
  log_commit_tx();
  bp->flags |= B_DIRTY; // clear our update
}

// Blocks.

// Allocate n disk blocks, no promise on content of allocated disk blocks
// Returns the beginning block number of a consecutive chunk of n blocks
static uint balloc(uint dev, uint n)
{
  int b, bi, m;
  struct buf *bp;

  bp = 0;
  for (b = 0; b < sb.size; b += BPB) {
    bp = bread(dev, BBLOCK(b, sb)); // look through each bitmap sector

    uint sz = 0;
    uint i = 0;
    for (bi = 0; bi < BPB && b + bi < sb.size; bi++) {
      m = 1 << (bi % 8);
      if ((bp->data[bi/8] & m) == 0) {  // Is block free?
        sz++;
        if (sz == 1) // reset starting blk
          i = bi;
        if (sz == n) { // found n blks
          bmark(bp, i, bi, true); // mark data block as used
          brelse(bp);
          return b+i;
        }
      } else { // reset search
        sz = 0;
        i =0;
      }
    }
    brelse(bp);
  }
  panic("balloc: can't allocate contiguous blocks");
}

// Free n disk blocks starting from b
static void bfree(int dev, uint b, uint n)
{
  struct buf *bp;

  assertm(n >= 1, "freeing less than 1 block");
  assertm(BBLOCK(b, sb) == BBLOCK(b+n-1, sb), "returned blocks live in different bitmap sectors");

  bp = bread(dev, BBLOCK(b, sb));
  bmark(bp, b % BPB, (b+n-1) % BPB, false);
  brelse(bp);
}

// Inodes.
//
// An inode describes a single unnamed file.
// The inode disk structure holds metadata: the file's type,
// its size, the number of links referring to it, and the
// range of blocks holding the file's content.
//
// The inodes themselves are contained in a file known as the
// inodefile. This allows the number of inodes to grow dynamically
// appending to the end of the inode file. The inodefile has an
// inum of 0 and starts at sb.startinode.
//
// The kernel keeps a cache of in-use inodes in memory
// to provide a place for synchronizing access
// to inodes used by multiple processes. The cached
// inodes include book-keeping information that is
// not stored on disk: ip->ref and ip->flags.
//
// Since there is no writing to the file system there is no need
// for the callers to worry about coherence between the disk
// and the in memory copy, although that will become important
// if writing to the disk is introduced.
//
// Clients use iget() to populate an inode with valid information
// from the disk. idup() can be used to add an in memory reference
// to and inode. irelease() will decrement the in memory reference count
// and will free the inode if there are no more references to it,
// freeing up space in the cache for the inode to be used again.



struct {
  struct spinlock lock;
  struct inode inode[NINODE];
  struct inode inodefile;
} icache;

// Find the inode file on the disk and load it into memory
// should only be called once, but is idempotent.
static void init_inodefile(int dev) {
  struct buf *b;
  struct dinode di;

  b = bread(dev, sb.inodestart);
  memmove(&di, b->data, sizeof(struct dinode));

  icache.inodefile.inum = INODEFILEINO;
  icache.inodefile.dev = dev;
  icache.inodefile.type = di.type;
  icache.inodefile.valid = 1;
  icache.inodefile.ref = 1;

  icache.inodefile.devid = di.devid;
  icache.inodefile.size = di.size;
  memmove(icache.inodefile.data, di.data, MAXEXTENT * sizeof(struct extent));
  // icache.inodefile.data = di.data;
  brelse(b);
}

void iinit(int dev) {
  int i;

  initlock(&icache.lock, "icache");
  for (i = 0; i < NINODE; i++) {
    initsleeplock(&icache.inode[i].lock, "inode");
  }
  initsleeplock(&icache.inodefile.lock, "inodefile");
  initsleeplock(&fslock, "log");

  readsb(dev, &sb);
  cprintf("sb: size %d nblocks %d bmap start %d inodestart %d\n", sb.size,
          sb.nblocks, sb.bmapstart, sb.inodestart);
  log_apply();
  init_inodefile(dev);
}


// Reads the dinode with the passed inum from the inode file.
// Threadsafe, will acquire sleeplock on inodefile inode if not held.
static void read_dinode(uint inum, struct dinode *dip) {
  int holding_inodefile_lock = holdingsleep(&icache.inodefile.lock);
  if (!holding_inodefile_lock)
    locki(&icache.inodefile);

  readi(&icache.inodefile, (char *)dip, INODEOFF(inum), sizeof(*dip));

  if (!holding_inodefile_lock)
    unlocki(&icache.inodefile);
}

static void write_dinode(uint inum, struct dinode *dip) {
  int holding_inodefile_lock = holdingsleep(&icache.inodefile.lock);
  if (!holding_inodefile_lock)
    locki(&icache.inodefile);

  writei(&icache.inodefile, (char *)dip, INODEOFF(inum), sizeof(*dip));

  if (!holding_inodefile_lock)
    unlocki(&icache.inodefile);
}

// Find the inode with number inum on device dev
// and return the in-memory copy. Does not read
// the inode from from disk.
static struct inode *iget(uint dev, uint inum) {
  struct inode *ip, *empty;

  acquire(&icache.lock);

  // Is the inode already cached?
  empty = 0;
  for (ip = &icache.inode[0]; ip < &icache.inode[NINODE]; ip++) {
    if (ip->ref > 0 && ip->dev == dev && ip->inum == inum) {
      ip->ref++;
      release(&icache.lock);
      return ip;
    }
    if (empty == 0 && ip->ref == 0) // Remember empty slot.
      empty = ip;
  }

  // Recycle an inode cache entry.
  if (empty == 0)
    panic("iget: no inodes");

  ip = empty;
  ip->ref = 1;
  ip->valid = 0;
  ip->dev = dev;
  ip->inum = inum;

  release(&icache.lock);

  return ip;
}

// Increment reference count for ip.
// Returns ip to enable ip = idup(ip1) idiom.
struct inode *idup(struct inode *ip) {
  acquire(&icache.lock);
  ip->ref++;
  release(&icache.lock);
  return ip;
}

// Drop a reference to an in-memory inode.
// If that was the last reference, the inode cache entry can
// be recycled.
void irelease(struct inode *ip) {
  acquire(&icache.lock);
  // inode has no other references release
  if (ip->ref == 1)
    ip->type = 0;
  ip->ref--;
  release(&icache.lock);
}

// Lock the given inode.
// Reads the inode from disk if necessary.
void locki(struct inode *ip) {
  struct dinode dip;

  if(ip == 0 || ip->ref < 1)
    panic("locki");

  acquiresleep(&ip->lock);

  if (ip->valid == 0) {

    if (ip != &icache.inodefile)
      locki(&icache.inodefile);
    read_dinode(ip->inum, &dip);
    if (ip != &icache.inodefile)
      unlocki(&icache.inodefile);

    ip->type = dip.type;
    ip->devid = dip.devid;

    ip->size = dip.size;
    memmove(ip->data, dip.data, sizeof(dip.data));
    ip->valid = 1;

    if (ip->type == 0)
      panic("iget: no type");
  }
}

// Unlock the given inode.
void unlocki(struct inode *ip) {
  if(ip == 0 || !holdingsleep(&ip->lock) || ip->ref < 1)
    panic("unlocki");

  releasesleep(&ip->lock);
}

// threadsafe stati.
void concurrent_stati(struct inode *ip, struct stat *st) {
  locki(ip);
  stati(ip, st);
  unlocki(ip);
}

// Copy stat information from inode.
// Caller must hold ip->lock.
void stati(struct inode *ip, struct stat *st) {
  if (!holdingsleep(&ip->lock))
    panic("not holding lock");

  st->dev = ip->dev;
  st->ino = ip->inum;
  st->type = ip->type;
  st->size = ip->size;
}


// threadsafe readi.
int concurrent_readi(struct inode *ip, char *dst, uint off, uint n) {
  int retval;

  locki(ip);
  retval = readi(ip, dst, off, n);
  unlocki(ip);

  return retval;
}

// Read data from inode.
// Returns number of bytes read.
// Caller must hold ip->lock.
int readi(struct inode *ip, char *dst, uint off, uint n) {
  uint tot, m;
  struct buf *bp;

  if (!holdingsleep(&ip->lock))
    panic("not holding lock");

  if (ip->type == T_DEV) {
    if (ip->devid < 0 || ip->devid >= NDEV || !devsw[ip->devid].read)
      return -1;
    return devsw[ip->devid].read(ip, dst, n);
  }

  if (off > ip->size || off + n < off)
    return -1;
  if (off + n > ip->size)
    n = ip->size - off;

  int extentnum = 0;
  int nblocks = off / BSIZE;
  // get current extent and block
  for (int i = 0; i < MAXEXTENT; i++) {
    int curblock = ip->data[i].nblocks;
    if ((nblocks - curblock) < 0) {
      extentnum = i; 
      break;
    } else {
      nblocks -= curblock;
    }
  }
  for (tot = 0; tot < n; tot += m, off += m, dst += m) {
    bp = bread(ip->dev, ip->data[extentnum].startblkno + nblocks);
    m = min(n - tot, BSIZE - off % BSIZE);
    memmove(dst, bp->data + off % BSIZE, m);
    brelse(bp);
    nblocks += 1;
    if (nblocks >= ip->data[extentnum].nblocks) {
      extentnum += 1;
      nblocks = 0;
    }
  }
  return n;
}

// threadsafe writei.
int concurrent_writei(struct inode *ip, char *src, uint off, uint n) {
  int retval;

  locki(ip);
  retval = writei(ip, src, off, n);
  unlocki(ip);

  return retval;
}

// Write data to inode.
// Returns number of bytes written.
// Caller must hold ip->lock.
int writei(struct inode *ip, char *src, uint off, uint n) {
  uint tot, m;
  struct buf *bp;

  if (!holdingsleep(&ip->lock))
    panic("not holding lock");

  if (ip->type == T_DEV) {
    if (ip->devid < 0 || ip->devid >= NDEV || !devsw[ip->devid].write)
      return -1;
    return devsw[ip->devid].write(ip, src, n);
  }
  if (off > ip->size || off + n < off)
    return -1;
  int actualblocks = 0;
  int blockstoa = (off + n) / BSIZE + ((off + n) % BSIZE == 0 ? 0 : 1);
  int extentnum = 0;
  int nblocks = off/BSIZE;
  for (int i = 0; i < MAXEXTENT; i++) {
    if (ip->data[i].nblocks == 0) {
      extentnum = i;
      break;
    } else {
      actualblocks += ip->data[i].nblocks;
    }
  }
  // add extents an allocate more blocks to account for bigger write
  if (off + n > ip->size) {
    if (blockstoa - actualblocks > 0) {
      ip->data[extentnum].startblkno = balloc(ip->dev, (blockstoa - actualblocks));
      ip->data[extentnum].nblocks = (blockstoa - actualblocks);
    }
    ip->size = off + n;
    struct dinode dip;
    dip.devid = ip->devid;
    dip.size = ip->size;
    dip.type = ip->type;
    memmove(dip.data, ip->data, MAXEXTENT*sizeof(struct extent));
    write_dinode(ip->inum, &dip);
  }
  // get current extent and block
  for (int i = 0; i < MAXEXTENT; i++) {
    int curblock = ip->data[i].nblocks;
    if (nblocks-curblock < 0) {
      extentnum = i;
      break;
    } else {
      nblocks -= curblock;
    }
  }

  for (tot = 0; tot < n; tot += m, off += m, src += m) {
    bp = bread(ip->dev, ip->data[extentnum].startblkno + nblocks);
    m = min(n - tot, BSIZE - off % BSIZE);
    memmove(bp->data + off % BSIZE, src, m);
    log_write(bp);
    //bwrite(bp);
    brelse(bp);
    nblocks += 1;
    if (nblocks >= ip->data[extentnum].nblocks) {
      extentnum += 1;
      nblocks = 0;
      if (extentnum > MAXEXTENT - 1) {
        return tot + m;
      }
    }
  }
  log_commit_tx();

  // read-only fs, writing to inode is an error
  return n;
}

// Directories

int namecmp(const char *s, const char *t) { return strncmp(s, t, DIRSIZ); }

struct inode *rootlookup(char *name) {
  return dirlookup(namei("/"), name, 0);
}

// Look for a directory entry in a directory.
// If found, set *poff to byte offset of entry.
struct inode *dirlookup(struct inode *dp, char *name, uint *poff) {
  uint off, inum;
  struct dirent de;

  if (dp->type != T_DIR)
    panic("dirlookup not DIR");

  for (off = 0; off < dp->size; off += sizeof(de)) {
    if (readi(dp, (char *)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlink read");
    if (de.inum == 0)
      continue;
    if (namecmp(name, de.name) == 0) {
      // entry matches path element
      if (poff)
        *poff = off;
      inum = de.inum;
      return iget(dp->dev, inum);
    }
  }

  return 0;
}

// Paths

// Copy the next path element from path into name.
// Return a pointer to the element following the copied one.
// The returned path has no leading slashes,
// so the caller can check *path=='\0' to see if the name is the last one.
// If no name to remove, return 0.
//
// Examples:
//   skipelem("a/bb/c", name) = "bb/c", setting name = "a"
//   skipelem("///a//bb", name) = "bb", setting name = "a"
//   skipelem("a", name) = "", setting name = "a"
//   skipelem("", name) = skipelem("////", name) = 0
//
static char *skipelem(char *path, char *name) {
  char *s;
  int len;

  while (*path == '/')
    path++;
  if (*path == 0)
    return 0;
  s = path;
  while (*path != '/' && *path != 0)
    path++;
  len = path - s;
  if (len >= DIRSIZ)
    memmove(name, s, DIRSIZ);
  else {
    memmove(name, s, len);
    name[len] = 0;
  }
  while (*path == '/')
    path++;
  return path;
}

// Look up and return the inode for a path name.
// If parent != 0, return the inode for the parent and copy the final
// path element into name, which must have room for DIRSIZ bytes.
// Must be called inside a transaction since it calls iput().
static struct inode *namex(char *path, int nameiparent, char *name) {
  struct inode *ip, *next;

  if (*path == '/')
    ip = iget(ROOTDEV, ROOTINO);
  else
    ip = idup(namei("/"));

  while ((path = skipelem(path, name)) != 0) {
    locki(ip);
    if (ip->type != T_DIR) {
      unlocki(ip);
      goto notfound;
    }

    // Stop one level early.
    if (nameiparent && *path == '\0') {
      unlocki(ip);
      return ip;
    }

    if ((next = dirlookup(ip, name, 0)) == 0) {
      unlocki(ip);
      goto notfound;
    }

    unlocki(ip);
    irelease(ip);
    ip = next;
  }
  if (nameiparent)
    goto notfound;

  return ip;

notfound:
  irelease(ip);
  return 0;
}

struct inode *namei(char *path) {
  char name[DIRSIZ];
  return namex(path, 0, name);
}

struct inode *nameiparent(char *path, char *name) {
  return namex(path, 1, name);
}

int fileunlink(char* path) {
  struct inode * inode = namei(path);
  if (inode == NULL) {
    return -1;
  } else if (inode->type == T_DEV || inode->type == T_DIR) {
    inode->ref -= 1;
    return -1;
  }
  inode->ref -= 1;
  if (inode->ref > 0) {
    return -1;
  }
  inode->ref += 1;
  locki(inode);
  struct inode* dir = &icache.inode[0];
  struct inode* inodefile = &icache.inodefile;
  struct dinode di;
  struct dirent de;
  // de.inum = 0;
  // concurrent_readi(inodefile, &di, INODEOFF(inode->inum), sizeof(di));
  for (int i = 0; i < MAXEXTENT; i++) {
    if (inode->data[i].nblocks == 0) {
      break;
    }
    bfree(inode->dev, inode->data[i].startblkno, inode->data[i].nblocks);
  }
  di.size = 0;
  di.devid = 0;
  di.type = 0;
  for (int i = 0; i < MAXEXTENT; i++) {
    di.data[i].nblocks = 0;
    di.data[i].startblkno = 0;
  } 
  concurrent_writei(inodefile, &di, INODEOFF(inode->inum), sizeof(di));
  for (int off = 0; off < dir->size; off+=sizeof(de)) {
    concurrent_readi(dir, &de, off, sizeof(de));
    if (de.inum == inode->inum) {
      de.inum = 0;
      memmove(de.name, 0, DIRSIZ);
      concurrent_writei(dir, &de, off, sizeof(de));
    }
  }
  // inode->dev=0;
  // inode->inum = 0;
  // inode->size = 0;
  // inode->valid =0;
  // inode->type=0;

  unlocki(inode);
  inode->ref -= 1;
  return 0;
}
