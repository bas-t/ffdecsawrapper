#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

#define O_SYNC           010000
#define O_ASYNC          020000

static int (*realopen) (__const char *__file, int __oflag, ...);
static int (*realopen64) (__const char *__file, int __oflag, ...);
void _init(void) {
	realopen = dlsym(RTLD_NEXT, "open");
	realopen64 = dlsym(RTLD_NEXT, "open64");
}

int open(__const char *__file, int __oflag, mode_t __mode) {
  if(strncmp(__file, "/dev/dvb/adapter", 16) == 0)
    __oflag |= (O_SYNC | O_ASYNC);
  printf("flags: %04x",__oflag);
  return realopen(__file, __oflag, __mode);
}

int open64(__const char *__file, int __oflag, mode_t __mode) {
  if(strncmp(__file, "/dev/dvb/adapter", 16) == 0)
    __oflag |= (O_SYNC | O_ASYNC);
  return realopen64(__file, __oflag, __mode);
}
