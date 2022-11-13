#include <cdefs.h>
#include <fcntl.h>
#include <fs.h>
#include <memlayout.h>
#include <param.h>
#include <stat.h>
#include <syscall.h>
#include <trap.h>
#include <user.h>

char buf[8192];
int stdout = 1;

#define error(msg, ...)                                                        \
  do {                                                                         \
    printf(stdout, "ERROR (line %d): ", __LINE__);                             \
    printf(stdout, msg, ##__VA_ARGS__);                                        \
    printf(stdout, "\n");                                                      \
    exit();                                                                    \
    while (1) {                                                                \
    };                                                                         \
  } while (0)

#define assert(a)                                                              \
  do {                                                                         \
    if (!(a)) {                                                                \
      printf(stdout, "Assertion failed (line %d): %s\n", __LINE__, #a);        \
      while (1)                                                                \
        ;                                                                      \
    }                                                                          \
  } while (0)

void overwrite(void) {
  int fd;

  printf(stdout, "overwrite...\n");
  strcpy(buf, "lab5 is the last 451 lab\n");
  fd = open("small.txt", O_RDWR);
  if (fd < 0) {
    error("Could not open small.txt with RW permissions");
  }
  // overwrite the original data.
  int n = write(fd, buf, strlen(buf) + 1);
  if (n != strlen(buf) + 1) {
    error("Did not write entire buffer. Wanted: %d, Wrote: %d\n", strlen(buf) + 1, n);
  }
  close(fd);

  fd = open("small.txt", O_RDONLY);
  read(fd, buf, 50);

  if (strcmp(buf, "lab5 is the last 451 lab\n") != 0)
    error("file content was not 'lab5 is the last 451 lab', was: '%s'", buf);

  close(fd);

  printf(stdout, "overwrite ok\n");
}

void append(void) {
  int fd;
  int old_size;

  printf(stdout, "append...\n");

  old_size = strlen(buf);

  fd = open("small.txt", O_RDWR);
  if (fd < 0) {
    error("Could not open small.txt with RW permissions");
  }

  // Advance the fd offset to 1 less than the size of file.
  char garbo[old_size];
  int n = read(fd, &garbo, old_size - 1);
  if (n != old_size - 1) {
    error("read failed");
  }
  strcpy(buf, ", but this is just the beginning :(\n");

  // overwrite the last char, and append data.
  n = write(fd, buf, strlen(buf) + 1);
  if (n != strlen(buf) + 1) {
    error("Did not write entire buffer. Wanted: %d, Wrote: %d\n", strlen(buf) + 1, n);
  }
  close(fd);

  fd = open("small.txt", O_RDWR);
  read(fd, buf, 62);

  // If the content hasn't changed, then the dinode size is not being
  // updated for the file.
  if (strcmp(buf, "lab5 is the last 451 lab, but this is just the beginning :(\n") != 0)
    error("file content did not match expected, was: '%s'", buf);

  close(fd);

  printf(stdout, "append ok\n");
}


// Creates a new file.
// Writes 1 byte to it, reads 1 byte from it.
void filecreation(void) {
  int fd, n;
  printf(1, "filecreation...\n");

  if ((fd = open("create.txt", O_CREATE|O_RDWR)) < 0)
    error("create 'create.txt' failed");

  close(fd);

  // Reopen and write 1 byte.
  if ((fd = open("create.txt", O_RDWR)) < 0)
    error("open 'create.txt' after creation failed");
  memset(buf, 1, 1);
  n = write(fd, buf, 1);
  if (n != 1) {
    error("error writing to created file.\n");
  }
  close(fd);


  // Reopen and read 1 byte.
  if ((fd = open("create.txt", O_RDWR)) < 0)
    error("open 'create.txt' after creation failed");
  memset(buf, 0, 1);
  n = read(fd, buf, 1);
  if (n != 1) {
    error("error reading from created file.\n");
  }

  // Ensure read got the correct value.
  assert(buf[0] == 1);

  printf(1, "filecreation ok\n");

}


// Creates a file, writes and reads data.
// Data is written and read by 500 bytes to try
// and catch errors in writing.
void onefile(void) {
  int fd, i, j;
  printf(1, "onefile...\n");

  if ((fd = open("onefile.txt", O_CREATE|O_RDWR)) < 0)
    error("create 'onefile.txt' failed");

  close(fd);
  if ((fd = open("onefile.txt", O_RDWR)) < 0)
    error("open 'onefile.txt' after creation failed");


  memset(buf, 0, sizeof(buf));
  for (i = 0; i < 10; i++) {
    memset(buf, i, 500);
    write(fd, buf, 500);
  }
  close(fd);
  if ((fd = open("onefile.txt", O_RDONLY)) < 0)
    error("couldn't reopen 'onefile.txt'");

  memset(buf, 0, sizeof(buf));
  for (i = 0; i < 10; i++) {
    if (read(fd, buf, 500) != 500)
      error("couldn't read the bytes for iteration %d", i);

    for (j = 0; j < 500; j++) {
      assert(i == buf[j]);
    }
  }

  printf(1, "onefile ok\n");
}

// four processes write different files at the same
// time, to test concurrent block allocation.
void fourfiles(void) {
  int fd, pid, i, j, n, total, pi;
  int num = 4;
  char *names[] = {"f0", "f1", "f2", "f3"};
  char *fname;

  printf(1, "fourfiles...\n");

  for (pi = 0; pi < num; pi++) {
    fname = names[pi];
    pid = fork();
    if (pid < 0) {
      error("fork failed.");
    }

    if (pid == 0) {
      fd = open(fname, O_CREATE | O_RDWR);
      if (fd < 0) {
        error("create failed\n");
      }

      memset(buf, '0' + pi, 512);
      for (i = 0; i < 12; i++) {
        if ((n = write(fd, buf, 500)) != 500) {
          error("write failed %d\n", n);
        }
      }
      exit();
    }
  }

  for (pi = 0; pi < num; pi++) {
    wait();
  }

  for (i = 0; i < num; i++) {
    fname = names[i];
    fd = open(fname, O_RDONLY);
    if (fd < 0) {
      error("file open failed for fname=%s\n", fname);
    }
    total = 0;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
      for (j = 0; j < n; j++) {
        if (buf[j] != '0' + i) {
          error("wrong char, was %d should be %d\n", buf[j], '0' + i);
        }
      }
      total += n;
    }
    close(fd);
    if (total != 12 * 500) {
      error("wrong length i:%d, wanted:%d, %d\n", i, 12 * 500, total);
    }
  }

  printf(1, "fourfiles ok\n");
}

void simpledelete() {
  printf(1, "Starting delete test...\n");
  // Check a couple cases of things you should never be able to delete
  if (unlink("file_not_existing") != -1) {
    error("Able to delete file that doesn't exist");
  }
  if (unlink("..") != -1 || unlink(".") != -1)
    error("Able to unlink directories");

  // Can't delete open file (in this process or another)
  char* name = "some_other_file";
  int fd = open(name, O_RDWR | O_CREATE);
  if (fd == -1)
    error("Unable to create file");
  if (unlink(name) != -1)
    error("Able to delete file that's currently open");
  if (close(fd) == -1)
      error("Unable to close file");

  int pid = fork();
  if (pid == 0) {
    open(name, O_RDWR);
    sleep(999999999);
    exit();
  } else {
    sleep(20); // Should be enough time
    if (unlink(name) != -1)
      error("Able to delete file that's open in another process");
    kill(pid);
    wait();
  }
  if (unlink(name) == -1)
    error("Unable to delete file once all references closed");
  if (open(name, O_RDWR) != -1)
    error("Able to open already-deleted file");

  printf(1, "  delete open ok\n");

  // Make sure deleting one file doesn't interfere with other open ones
  char* n1 = "small.txt";
  char* n2 = "arbitrary";
  int fdn1 = open(n1, O_RDWR);
  int fdn2 = open(n2, O_RDWR | O_CREATE);
  if (fdn1 == -1 || fdn2 == -1)
    error("error opening files");
  if (close(fdn2) == -1)
    error("error closing arbitrary");
  if (unlink(n2) == -1)
    error("error unlinking arbitrary");
  if (read(fdn1, buf, 62) == -1)
    error("couldn't read from small after unlink");
  if (strcmp(buf, "lab5 is the last 451 lab, but this is just the beginning :(\n") != 0)
    error("file content did not match expected after unlink, was: '%s'", buf);

  // Check inum usage/reuse and ordering
  struct stat ss;
  int fd1 = open("df1", O_RDWR | O_CREATE);
  if (fd1 == -1)
    error("Unable to open df1");
  if (fstat(fd1, &ss) == -1)
    error("Unable to stat open df1");
  int in1 = ss.ino;
  int fd2 = open("df2", O_RDWR | O_CREATE);
  if (fd2 == -1)
    error("Unable to open df2");
  if (fstat(fd2, &ss) == -1)
    error("Unable to stat open df2");
  int in2 = ss.ino;
  if (close(fd1) == -1)
    error("unable to close df1");
  if (unlink("df1") == -1)
    error("unable to unlink df1");
  int fd3 = open("df3", O_RDWR | O_CREATE);
  if (fd3 == -1)
    error("Unable to open df3");
  if (fstat(fd3, &ss) == -1)
    error("Unable to stat open file");
  int in3 = ss.ino;
  if (in3 != in1 || in3 == in2)
    error("File allocation does not reuse inums");
  int fd4 = open("df4", O_RDWR | O_CREATE);
  if (fd4 == -1)
    error("Unable to open df4");
  if (fstat(fd4, &ss) == -1)
    error("Unable to stat open file");
  int in4 = ss.ino;
  if (in4 == in3 || in4 == in1 || in4 != in2 + 1)
    error("File allocation improperly finds inums");
  if (close(fd2) != 0 || close(fd3) != 0 || close(fd4) != 0 || unlink("df2") != 0 || unlink("df3") != 0 || unlink("df4") != 0)
    error("Unable to clean up after inum allocation test");

  // You should not be able to read from a deleted file
  for (int i = 0; i < 8; ++i)
    buf[i] = i + 1;
  fd = open("file", O_RDWR | O_CREATE);
  if (fd == -1 || write(fd, buf, 8) != 8)
    error("Unable to open/write to file");
  for (int i = 0; i < 8; ++i)
    buf[i] = 0;
  if (close(fd) != 0 || unlink("file") != 0)
    error("Unable to clean up file (1)");
  fd = open("file", O_RDWR | O_CREATE);
  if (fd == -1)
    error("Unable to reopen file");
  int r = read(fd, buf, 8);
  if (r > 0)
    error("Able to read from fresh file");
  if (close(fd) != 0 || unlink("file") != 0)
    error("Unable to clean up file");
  printf(1, "  simple deletion ok\n");
}

int main(int argc, char *argv[]) {
  printf(stdout, "lab4test_a starting\n");
  overwrite();
  append();
  filecreation();
  onefile();
  fourfiles();
  simpledelete();
  printf(stdout, "lab4test_a passed!\n");
  exit();
}
