/*
 * pthread double locking testing tool.
 * Locks a mutex 2 times.
 * This a valid operation for an ERRORCHECK mutex!
 * If this test locks up, your pthread implementation is faulty.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <pthread.h>
#include <sys/types.h>

class cMutex {
  friend class cCondVar;
private:
  pthread_mutex_t mutex;
  int locked;
public:
  cMutex(void);
  ~cMutex();
  void Lock(void);
  void Unlock(void);
  };

cMutex::cMutex(void)
{
  locked = 0;
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  printf("mutex attr init returned %d:%s\n",errno,strerror(errno));
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK_NP);
  printf("mutex attr settype returned %d:%s\n",errno,strerror(errno));
  pthread_mutex_init(&mutex, &attr);
  printf("mutex init returned %d:%s\n",errno,strerror(errno));
}

cMutex::~cMutex()
{
  pthread_mutex_destroy(&mutex);
  printf("mutex destroy returned %d:%s\n",errno,strerror(errno));
}

void cMutex::Lock(void)
{
  pthread_mutex_lock(&mutex);
  printf("mutex lock returned %d:%s\n",errno,strerror(errno));
  locked++;
}

void cMutex::Unlock(void)
{
 if (!--locked) {
    pthread_mutex_unlock(&mutex);
    printf("mutex unlock returned %d:%s\n",errno,strerror(errno));
    }
}

int main(int argc, char *argv[])
{
  cMutex mutex;
  printf("doing first lock\n");
  mutex.Lock();
  printf("first lock successful\n");
  printf("doing second lock\n");
  mutex.Lock();
  printf("second lock successful\n");

  mutex.Unlock();
  mutex.Unlock();
}
