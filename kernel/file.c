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
extern struct {
  struct spinlock lock;
  struct inode inode[NINODE];
  struct inode inodefile;
} icache;

// Finds an open spot in the process open file table and has it point the global open file table entry.
// Finds an open entry in the global open file table and allocates a new file_info struct
// for the process open file table spot to point to.
// Will always open a device, and only open a file if permissions passed are O_RDONLY.

// Use the type field to check for a device. See stat.h for possible values.
// Returns the index into the process open file table as the file descriptor, or -1 on failure.
int fileopen(char *path, int mode)
{ 
  // turnate the string
  if (strlen(path) > DIRSIZ) {
    char newpath[DIRSIZ];
    strncpy(newpath, path, DIRSIZ);
    path = newpath;
  }
  if (mode == O_CREATE|O_RDONLY) {
      mode = O_RDONLY;
      createi(path);
  } else if (mode == O_CREATE|O_RDWR) {
      mode = O_RDWR;
      createi(path);
  } 
  struct inode *inode = namei(path); // returns pointer to inode
  struct proc *proc = myproc();
  if (inode == NULL) {

    return -1;
  }

  locki(inode);
  if (inode->type != T_DEV && mode != O_RDONLY && mode != O_RDWR)
  {
    unlocki(inode);
    return -1;
  }

  int i = 0;
  int fd = 0;
  while (i < NFILE)
  {
    acquiresleep(&file_table[i].lock);
    if (file_table[i].inode_ptr == 0x0 &&
        file_table[i].mode == 0 &&
        file_table[i].offset == 0 &&
        file_table[i].ref == 0)
    {
      file_table[i].inode_ptr = inode;
      file_table[i].mode = mode;
      file_table[i].offset = 0;
      file_table[i].ref = 1;
      fd = find_free_fd(i, proc);
      if (fd == -1)
      {
        unlocki(inode);
        releasesleep(&file_table[i].lock);
        return -1;
      }
      releasesleep(&file_table[i].lock);
      break;
    }
    releasesleep(&file_table[i].lock);
    i++;
  }

  // no more file table space left
  if (i == NFILE)
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

void createi(char *path) {
  if (namei(path) != NULL) {
    return;
  }
  struct dinode di;
  struct dirent de;
  // struct inode* dir = namei((char*)'/');
  struct inode* dir = &icache.inode[0];
  struct inode* inodefile = &icache.inodefile;
  // cprintf("\n%d\n", sizeof(de));

  int inum = 2;
  for(inum; INODEOFF(inum) <= inodefile->size; inum++) {
    if (INODEOFF(inum) == inodefile->size) {
      concurrent_writei(inodefile, (char*)&di, INODEOFF(inum), sizeof(di));
    }
    concurrent_readi(inodefile, (char *)&di, INODEOFF(inum), sizeof(di));
    if (di.size == 0 && di.type == 0) {
      di.size = 0;
      di.devid = T_DEV;
      di.type = T_FILE;
      // write the dinode to the file
      concurrent_writei(inodefile, (char*)&di, INODEOFF(inum), sizeof(di));
      for (int off = 0; off <= dir->size; off+=sizeof(de)) {
        if (off == dir->size) {
          concurrent_writei(dir, (char*)&de, off, sizeof(de));
        }
        concurrent_readi(dir, (char *)&de, off, sizeof(de));
        if (de.inum == 0) {
          de.inum = inum;
          strncpy(de.name, path, strlen(path));
          concurrent_writei(dir, (char*)&de, off, sizeof(de));
          break;
        }
      }
      break;
    }
  }
  // if (hole == 0) {
  //   di.size = 0;
  //   di.devid = T_DEV;
  //   di.type = T_FILE;
  //   concurrent_writei(inodefile, (char*)&di, INODEOFF(inum), sizeof(di));
  //   cprintf("%d", dir->size);
  //   dir->size += sizeof(de);
  //   for (int off = 0; off < dir->size; off+=sizeof(de)) {
  //     concurrent_readi(dir, (char *)&de, off, sizeof(de));
  //     if (de.inum == 0) {
  //       de.inum = inum;
  //       strncpy(de.name, path, strlen(path));
  //       concurrent_writei(dir, (char*)&de, off, sizeof(de));
  //       break;
  //     }
  //   }
  // }
}

int fileread(char *src, int fd, int n)
{
  struct proc *cur = myproc();
  struct file_info *fpointer = cur->fd_table[fd];
  int ret = 0;
  if (fpointer == NULL || fpointer->mode == O_WRONLY)
  {
    return -1;
  }
  if (fpointer -> is_pipe == 1) {
    int buffer_size = 3500;
    acquire(&fpointer->pipe->lock);
    int to_read;
    for (int i = 0; i < n; i++) {
    // check how much left to_read
      to_read = fpointer->pipe->offset_write - fpointer->pipe->offset_read;
      while (to_read == 0) {
        // if no writer left and no data in pipe return 0
        if (fpointer->pipe->writer == 0) {
          release(&fpointer->pipe->lock);
          return ret;
        }
        wakeup(&fpointer->pipe->writer);
        sleep(&fpointer->pipe->reader, &fpointer->pipe->lock);
        to_read = fpointer->pipe->offset_write - fpointer->pipe->offset_read;
      }
      src[i] = fpointer->pipe->buffer[fpointer->pipe->offset_read%buffer_size];
      ret++;
      fpointer->pipe->offset_read++;
    }
    release(&fpointer->pipe->lock);
  } else {
    acquiresleep(&fpointer->lock);
    struct inode *ip = fpointer->inode_ptr;
    if (ip == NULL)
    {
      releasesleep(&fpointer -> lock);
      return -1;
    }
    int off = fpointer->offset;
    ret = concurrent_readi(ip, src, off, n);
    fpointer->offset += ret;
    releasesleep(&fpointer -> lock);
  }
  return ret;
}

int filewrite(char *src, int fd, int n)
{ 
  struct proc *cur = myproc();
  struct file_info *fpointer = cur->fd_table[fd];
  int ret = 0;
  if (fpointer == NULL || fpointer->mode == O_RDONLY) { 
    return -1;
  }
  if (fpointer->is_pipe == 1) {
    // return -1 if there is no reader in pipe
    int buffer_size = 3500;
    // lock the pipe
    acquire(&fpointer->pipe->lock); 
    int to_write;
    for (int i = 0; i < n; i++) {
      // how much can write till reach read
      to_write = buffer_size - fpointer->pipe->offset_write%buffer_size + fpointer->pipe->offset_read%buffer_size;  
      // if the pipe is full wake up the reader
      while (to_write == 0) {
        if (fpointer->pipe->reader == 0) {
          release(&fpointer->pipe->lock);
          return -1;
        }
        wakeup(&fpointer->pipe->reader);
        sleep(&fpointer->pipe->writer, &fpointer->pipe->lock);
        to_write = buffer_size - fpointer->pipe->offset_write%buffer_size + fpointer->pipe->offset_read%buffer_size;  
      }
      if (fpointer->pipe->reader == 0) {
          release(&fpointer->pipe->lock);
          return -1;
      }      
      // break when we fulfill the writing task
      fpointer->pipe->buffer[fpointer->pipe->offset_write%buffer_size] = src[i];
      ret++;
      fpointer->pipe->offset_write++;
    } 
    release(&fpointer->pipe->lock);
  } else {
    acquiresleep(&fpointer->lock);
    struct inode *ip = fpointer->inode_ptr;
    if (ip == NULL)
    {
      releasesleep(&fpointer -> lock);
      return -1;
    }
    int off = fpointer->offset;
    ret = concurrent_writei(ip, src, off, n);
    fpointer->offset += ret;
    releasesleep(&fpointer -> lock);
  }
  return ret;
}

int fileclose(int fd)
{
  struct proc *cur = myproc();
  struct file_info *fpointer = cur->fd_table[fd];
  if (fpointer == NULL || fpointer -> ref == 0) {
    return -1;
  } else {
    acquiresleep(&fpointer->lock);
  }
  fpointer -> ref -= 1;
  // deal with pipe 
  if (fpointer -> is_pipe == 1) {
    acquire(&fpointer->pipe->lock);
    if (fpointer->mode == O_WRONLY) {
      fpointer->pipe->writer = fpointer->ref;
      // fpointer->pipe->writer = fpointer;
    } else {
      fpointer->pipe->reader = fpointer->ref;
    }
    if (fpointer->pipe->reader == 0 && fpointer->pipe->writer == 0) {
      release(&fpointer->pipe->lock);
      kfree((char *)fpointer->pipe->buffer);
      fpointer -> pipe = NULL;
    } else {
      if (fpointer->pipe->writer == 0) {
        wakeup(&fpointer->pipe->reader);
      }
      if (fpointer->pipe->reader == 0) {
        wakeup(&fpointer->pipe->writer);
      }
      release(&fpointer->pipe->lock);
    }
  }  
  if (fpointer -> ref <= 0) {
    if (fpointer -> is_pipe != 1) {
      irelease(fpointer->inode_ptr);
    } else {
      fpointer -> pipe = NULL;
      fpointer -> is_pipe = 0;
    }
    fpointer -> inode_ptr = NULL;
    fpointer -> mode = 0;
    fpointer -> offset = 0;
  }
  releasesleep(&fpointer -> lock);
  cur->fd_table[fd] = NULL;
  return 0;
}


int filedup(int fd)
{
  struct proc *cur = myproc();
  struct file_info *fpointer = cur->fd_table[fd];
  if (fpointer == NULL) {
    return -1;
  } else {
    acquiresleep(&fpointer->lock);
  }
  int ret = -1;
  for (int i = 0; i < NOFILE; i++) {
    if (cur->fd_table[i] != NULL){
      continue;
    } else {
      cur->fd_table[i] = fpointer;
      fpointer -> ref += 1;
      ret = i;
      if (fpointer -> is_pipe == 1) {
        acquire(&fpointer->pipe->lock);
        if (fpointer -> mode == O_WRONLY) {
          fpointer -> pipe -> writer = fpointer->ref;
        } else {
          fpointer -> pipe -> reader = fpointer->ref;
        }
        release(&fpointer->pipe->lock);
      }
      break;
    }
  }
  releasesleep(&fpointer->lock);
  return ret;
}

int filestat(int fd, struct stat* fstat)
{
  struct proc *cur = myproc();
  struct file_info *fpointer = cur->fd_table[fd];
  struct inode* inode = fpointer->inode_ptr;
  if (fpointer == NULL || inode == NULL) {
    return -1;
  } else {
    acquiresleep(&fpointer->lock);
  }
  concurrent_stati(inode, fstat);
  releasesleep(&fpointer->lock);
  return 0;
}

int filepipe(int* fds) {
  struct proc *cur = myproc();
  struct file_pipe* pipe;
  int reader_fd = -1;
  int writer_fd = -1;
  // get file descriptor
  int counter = 0;
  for (int i = 0; i < NOFILE; i++) {
    if (cur->fd_table[i] == NULL) {
      if (counter > 1) {
        break;
      } else {
        fds[counter] = i;
        counter++;
      }
    }
  }
  // no file descriptor left in the process
  if (counter <= 1) {
    return -1;
  }
  for (int i = 0; i < NFILE; i++) {
    if (file_table[i].ref == 0) {
      acquiresleep(&file_table[i].lock);
      if (reader_fd == -1) {
        reader_fd = i;
      } else {
        writer_fd = i;
        break;
      }
    }
  }
  // reutrn -1 if there are not enough fd avaliable
  if (reader_fd == -1 || writer_fd == -1) {
    // release the lock if there is reader but no writer opening
    if (reader_fd != -1) {
      releasesleep(&file_table[reader_fd].lock);
    }
    return -1;
  }
  // check if there is enough space in kernel to allocate the pipe
  if (!(pipe = (struct file_pipe*)kalloc())) {
    releasesleep(&file_table[reader_fd].lock);
    releasesleep(&file_table[writer_fd].lock);
    return -1;
  }  
  initlock(&pipe->lock, "pipe"); 
  acquire(&pipe->lock);
  // int buffer_size = 4096 -  6 * sizeof(int) - sizeof(struct spinlock);
  pipe -> reader = 1;
  pipe -> writer = 1;
  pipe -> offset_read = 0;
  pipe -> offset_write = 0;
  pipe -> full = 0;                           
  pipe -> empty = 1;
  // initilize reader
  cur->fd_table[fds[0]] = &file_table[reader_fd];
  file_table[reader_fd].is_pipe = 1;
  file_table[reader_fd].mode = O_RDONLY;
  file_table[reader_fd].ref = 1;
  file_table[reader_fd].pipe = pipe;
  // initilize writer
  cur->fd_table[fds[1]] = &file_table[writer_fd];
  file_table[writer_fd].is_pipe = 1;
  file_table[writer_fd].mode = O_WRONLY;
  file_table[writer_fd].ref = 1;
  file_table[writer_fd].pipe = pipe;
  releasesleep(&file_table[reader_fd].lock);
  releasesleep(&file_table[writer_fd].lock);
  release(&pipe->lock);
  return 0;
}