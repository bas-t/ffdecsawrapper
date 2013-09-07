#include <sys/types.h>
#include <dlfcn.h>

	/* dl*() stub routines for static compilation.  Prepared from
	   /usr/include/dlfcn.h by Hal Pomeranz <hal@deer-run.com> */

static	void *dlopen(const char *str, int x) {}
static	void *dlsym(void *ptr, const char *str) {}
static	int dlclose(void *ptr) {}
static	char *dlerror() {}
static	void *dlmopen(Lmid_t a, const char *str, int x) {}
static	int dladdr(void *ptr1, Dl_info *ptr2) {}
static	int dldump(const char *str1, const char *str2, int x) {}
static	int dlinfo(void *ptr1, int x, void *ptr2) {}

static	void *_dlopen(const char *str, int x) {}
static	void *_dlsym(void *ptr, const char *str) {}
static	int _dlclose(void *ptr) {}
static	char *_dlerror() {}
static	void *_dlmopen(Lmid_t a, const char *str, int x) {}
static	int _dladdr(void *ptr1, Dl_info *ptr2) {}
static	int _dldump(const char *str1, const char *str2, int x) {}
static	int _dlinfo(void *ptr1, int x, void *ptr2) {}
