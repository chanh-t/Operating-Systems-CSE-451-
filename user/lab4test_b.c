#include <cdefs.h>
#include <fcntl.h>
#include <stat.h>
#include <user.h>

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

void concurrent_dup_test() {
  int i, pid, fd1, fd2, fd3, fd4;

  fd1 = open("concurrent_dup.txt", O_CREATE | O_RDWR);
  assert(fd1 == 3);

  for (i = 0; i < 50; i++) {
    pid = fork();
    if (pid == 0) {
      fd2 = dup(fd1);
      assert(fd2 == 4);
      fd3 = dup(fd1);
      assert(fd3 == 5);
      fd4 = dup(fd1);
      assert(fd4 == 6);

      sleep(50);

      assert(close(fd1) >= 0);
      assert(close(fd2) >= 0);
      assert(close(fd3) >= 0);
      assert(close(fd4) >= 0);
      exit();
    } else if (pid < 0) {
      error("fork() failed");
    }
  }

  for (i = 0; i < 50; i++) {
    wait();
  }

  // no more children
  if (wait() >= 0) {
    error("ghost child!");
  }

  assert(close(fd1) >= 0);
  printf(stdout, "concurrent dup test OK\n");
}

void concurrent_create_test() {
  int i, j, pid, fd, ret;
  char buf[5];

  fd = open("cc_create.txt", O_CREATE | O_WRONLY);
  assert(fd == 3);

  memset(buf, 0, 5);
  for (i = 0; i < 1000; i++) {
    assert(write(fd, buf, 5) == 5);
  }

  assert(close(fd) >= 0);

  for (i = 0; i < 50; i++) {
    pid = fork();
    if (pid == 0) {
      fd = open("cc_create.txt", O_CREATE | O_RDWR);
      assert(fd == 3);

      j = 100 * i;
      while (j > 0) {
        assert((ret = read(fd, buf, 5)) >= 0);
        j -= ret;
      }

      memset(buf, 65 + i, 5);
      for (j = 0; j < 20; j++) {
        assert(write(fd, buf, 5) == 5);
      }

      assert(close(fd) >= 0);
      exit();
    } else if (pid < 0) {
      error("fork() failed");
    }
  }

  for (i = 0; i < 50; i++) {
    wait();
  }

  // no more children
  if (wait() >= 0) {
    error("ghost child!");
  }

  fd = open("cc_create.txt", O_RDONLY);
  assert(fd == 3);

  for (i = 0; i < 50; i++) {
    for (j = 0; j < 100; j++) {
      while (read(fd, buf, 1) <= 0);
      assert(buf[0] == (i + 65));
    }
  }

  // EOF
  assert(read(fd, buf, 1) == 0);
  assert(close(fd) >= 0);

  printf(stdout, "concurrent create test OK\n");
}

void concurrent_write_test() {
  int i, j, pid, fd;
  int map[50];
  char buf[5];

  fd = open("cc_write.txt", O_CREATE | O_WRONLY);
  assert(fd == 3);

  for (i = 0; i < 50; i++) {
    pid = fork();
    if (pid == 0) {
      for (j = 0; j < 5; j++) {
        buf[j] = i + 65;
      }

      for (j = 0; j < 20; j++) {
        assert(write(fd, buf, 5) == 5);
      }

      exit();
    } else if (pid < 0) {
      error("fork() failed");
    }
  }

  for (i = 0; i < 50; i++) {
    wait();
  }

  // no more children
  if (wait() >= 0) {
    error("ghost child!");
  }

  assert(close(fd) >= 0);
  fd = open("cc_write.txt", O_RDONLY);
  assert(fd == 3);

  for (i = 0; i < 50; i++) {
    map[i] = 0;
  }

  for (i = 0; i < 5000; i++) {
    while (read(fd, buf, 1) <= 0);
    map[buf[0] - 65]++;
  }

  // EOF
  assert(read(fd, buf, 1) == 0);
  assert(close(fd) >= 0);

  for (i = 0; i < 50; i++) {
    if (map[i] != 100)
      error("missing byte from child %d", i);
  }
  printf(stdout, "concurrent write test OK\n");
}

void concurrent_read_test() {
  int i, j, pid, fd;
  uchar buf[5];

  fd = open("cc_write.txt", O_CREATE | O_WRONLY);
  assert(fd == 3);

  for (i = 0; i < 250; i++) {
    for (j = 0; j < 5; j++) {
      buf[j] = i;
    }
    for (j = 0; j < 4; j++) {
      assert(write(fd, buf, 5) == 5);
    }
  }

  assert(close(fd) >= 0);
  fd = open("cc_write.txt", O_RDONLY);
  assert(fd == 3);

  buf[0] = 0;
  for (i = 0; i < 50; i++) {
    pid = fork();
    if (pid == 0) {
      for (j = 0; j < 100; j++) {
        while (read(fd, buf + 1, 1) <= 0);
        if (buf[1] >= buf[0]) {
          buf[0] = buf[1];
        } else {
          error("read out of order: read %d before %d in child %d", buf[0], buf[1], i);
        }
      }
      exit();
    } else if (pid < 0) {
      error("fork() failed");
    }
  }

  for (i = 0; i < 50; i++) {
    wait();
  }

  // no more children
  if (wait() >= 0) {
    error("ghost child!");
  }

  assert(read(fd, buf, 1) == 0);
  assert(close(fd) >= 0);

  printf(stdout, "concurrent read test OK\n");
}

void concurrent_delete_test() {
  int pipes[2];
  pipe(pipes);
  char buf[2];
  int fd = open("cc_del", O_RDWR | O_CREATE);
  if (fd < 0)
    error("open failed");

  for (int i = 0; i < 50; ++i) {
    int pid = fork();
    if (pid != 0)
      continue;
    if (unlink("cc_del") != -1)
      write(pipes[1], &buf, 1);
    exit();
  }
  for (int i = 0; i < 50; ++i)
    wait();
  close(pipes[1]);
  if (2 == read(pipes[0], &buf, 2))
    error("File deleted multiple times");
 printf(stdout, "concurrent delete test OK\n");

}

void printBar(int count, bool backspace) {
  if (backspace)
    for (int i = 0; i < 100 + 2; ++i)
      printf(1, "\b");
  printf(1, "[");
  for (int i = 0; i < 100; ++i)
    if (i < count)
      printf(1, "#");
    else printf(1, " ");
  printf(1, "]");

}

void delete_stress_test() {
  int fd0 = open("ddf1", O_RDWR | O_CREATE);
  char buf[128];
  char* text = "This is the data that goes into the file created here.";
  // overwrite the last char, and append data.
  int n = write(fd0, text, strlen(text));
  if (n < 0)
    error("write failed");

  if (close(fd0) == -1)
    error("unable to close");
  fd0 = open("ddf1", O_RDONLY);
  if (fd0 == -1)
    error("unable to reopen");
  // You better be reclaiming everything......
  printf(1, "starting delete stress test (this should take around 5-10 min)...\n");
  printBar(0, false);

  // This test ensures that you're reclaiming space on delete by
  // churning through the entire disk's worth of memory several times over
  // It's slow.
  //
  // If you want to speed this up for your own intermediate testing
  // 1) reduce the size of the disk (FSSIZE) in inc/params.h
  // 2) change the number of loop iterations to (FSSIZE / min_file_blocks) * 2
  //    (so if you always allocate 20 blocks per file, and drop FSSIZE to 10k,
  //     you could try dropping this down to 1k iterations)
  //
  // Just make sure you test on the original parameters before submission!
  for (int i = 1; i <= 100000; ++i) {
    int fd = open("file", O_RDWR | O_CREATE);
    if (fd == -1)
      error("Unable to open file on %d", i);
    if (close(fd) == -1)
      error("Unable to close file on %d", i);
    if (unlink("file") == -1)
      error("Unable to unlink file on %d", i);
   if (i % 1000 == 0)
      printBar(i / 1000, true);
  }

  read(fd0, &buf, strlen(text));
  if (strcmp(buf, text) != 0)
    error("Bad. %s  //  %s\n", buf, text);

  printf(1, "\ndelete stress test OK\n");

}

int main(int argc, char *argv[]) {
  printf(stdout, "lab4test_b starting\n");

  concurrent_dup_test();
  concurrent_create_test();
  concurrent_write_test();
  concurrent_read_test();
  concurrent_delete_test();
  delete_stress_test();
  printf(stdout, "lab4test_b passed!\n");
  exit();
}

