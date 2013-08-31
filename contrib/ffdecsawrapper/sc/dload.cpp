#include <sys/types.h>
#include <dlfcn.h>
#include <stdlib.h>

	/* dl*() stub routines for static compilation.  Prepared from
	   /usr/include/dlfcn.h by Hal Pomeranz <hal@deer-run.com> */

	void *dlopen(const char *str, int x) {return (void *)1;}
	void *dlsym(void *ptr, const char *str) {return NULL;}
	int dlclose(void *ptr) {return 0;}
	char *dlerror() {return NULL;}
	void *dlmopen(Lmid_t a, const char *str, int x) {return NULL;}
	int dladdr(void *ptr1, Dl_info *ptr2) {return 0;}
	int dldump(const char *str1, const char *str2, int x) {return 0;}
	int dlinfo(void *ptr1, int x, void *ptr2) {return 0;}

	void *_dlopen(const char *str, int x) {return NULL;}
	void *_dlsym(void *ptr, const char *str) {return NULL;}
	int _dlclose(void *ptr) {return 0;}
	char *_dlerror() {return NULL;}
	void *_dlmopen(Lmid_t a, const char *str, int x) {return NULL;}
	int _dladdr(void *ptr1, Dl_info *ptr2) {return 0;}
	int _dldump(const char *str1, const char *str2, int x) {return 0;}
	int _dlinfo(void *ptr1, int x, void *ptr2) {return 0;}
