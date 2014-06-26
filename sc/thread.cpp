/*
 * thread.c: A simple thread base class
 *
 * See the main source file 'vdr.c' for copyright information and
 * how to reach the author.
 *
 * $Id: thread.c 1.45 2005/08/14 11:15:42 kls Exp $
 */

#include "include/vdr/thread.h"
#include <errno.h>
#include <malloc.h>
#include <stdarg.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/syscall.h>
#include "include/vdr/tools.h"

static bool GetAbsTime(struct timespec *Abstime, int MillisecondsFromNow)
{
  struct timeval now;
  if (gettimeofday(&now, NULL) == 0) {           // get current time
     now.tv_usec += MillisecondsFromNow * 1000;  // add the timeout
     while (now.tv_usec >= 1000000) {            // take care of an overflow
           now.tv_sec++;
           now.tv_usec -= 1000000;
           }
     Abstime->tv_sec = now.tv_sec;          // seconds
     Abstime->tv_nsec = now.tv_usec * 1000; // nano seconds
     return true;
     }
  return false;
}

// --- cCondWait -------------------------------------------------------------

cCondWait::cCondWait(void)
{
  signaled = false;
  pthread_mutex_init(&mutex, NULL);
  pthread_cond_init(&cond, NULL);
}

cCondWait::~cCondWait()
{
  pthread_cond_broadcast(&cond); // wake up any sleepers
  pthread_cond_destroy(&cond);
  pthread_mutex_destroy(&mutex);
}

void cCondWait::SleepMs(int TimeoutMs)
{
  cCondWait w;
  w.Wait(max(TimeoutMs, 3)); // making sure the time is >2ms to avoid a possible busy wait
}

bool cCondWait::Wait(int TimeoutMs)
{
  pthread_mutex_lock(&mutex);
  if (!signaled) {
     if (TimeoutMs) {
        struct timespec abstime;
        if (GetAbsTime(&abstime, TimeoutMs)) {
           while (!signaled) {
                 if (pthread_cond_timedwait(&cond, &mutex, &abstime) == ETIMEDOUT)
                    break;
                 }
           }
        }
     else
        pthread_cond_wait(&cond, &mutex);
     }
  bool r = signaled;
  signaled = false;
  pthread_mutex_unlock(&mutex);
  return r;
}

void cCondWait::Signal(void)
{
  pthread_mutex_lock(&mutex);
  signaled = true;
  pthread_cond_broadcast(&cond);
  pthread_mutex_unlock(&mutex);
}

// --- cCondVar --------------------------------------------------------------

cCondVar::cCondVar(void)
{
  pthread_cond_init(&cond, 0);
}

cCondVar::~cCondVar()
{
  pthread_cond_broadcast(&cond); // wake up any sleepers
  pthread_cond_destroy(&cond);
}

void cCondVar::Wait(cMutex &Mutex)
{
  if (Mutex.locked) {
     int locked = Mutex.locked;
     Mutex.locked = 0; // have to clear the locked count here, as pthread_cond_wait
                       // does an implizit unlock of the mutex
     pthread_cond_wait(&cond, &Mutex.mutex);
     Mutex.locked = locked;
     }
}

bool cCondVar::TimedWait(cMutex &Mutex, int TimeoutMs)
{
  bool r = true; // true = condition signaled false = timeout

  if (Mutex.locked) {
     struct timespec abstime;
     if (GetAbsTime(&abstime, TimeoutMs)) {
        int locked = Mutex.locked;
        Mutex.locked = 0; // have to clear the locked count here, as pthread_cond_timedwait
                          // does an implizit unlock of the mutex.
        if (pthread_cond_timedwait(&cond, &Mutex.mutex, &abstime) == ETIMEDOUT)
           r = false;
        Mutex.locked = locked;
        }
     }
  return r;
}

void cCondVar::Broadcast(void)
{
  pthread_cond_broadcast(&cond);
}

// --- cRwLock ---------------------------------------------------------------

cRwLock::cRwLock(bool PreferWriter)
{
  pthread_rwlockattr_t attr;
  pthread_rwlockattr_init(&attr);
  pthread_rwlockattr_setkind_np(&attr, PreferWriter ? PTHREAD_RWLOCK_PREFER_WRITER_NP : PTHREAD_RWLOCK_PREFER_READER_NP);
  pthread_rwlock_init(&rwlock, &attr);
}

cRwLock::~cRwLock()
{
  pthread_rwlock_destroy(&rwlock);
}

bool cRwLock::Lock(bool Write, int TimeoutMs)
{
  int Result = 0;
  struct timespec abstime;
  if (TimeoutMs) {
     if (!GetAbsTime(&abstime, TimeoutMs))
        TimeoutMs = 0;
     }
  if (Write)
     Result = TimeoutMs ? pthread_rwlock_timedwrlock(&rwlock, &abstime) : pthread_rwlock_wrlock(&rwlock);
  else
     Result = TimeoutMs ? pthread_rwlock_timedrdlock(&rwlock, &abstime) : pthread_rwlock_rdlock(&rwlock);
  return Result == 0;
}

void cRwLock::Unlock(void)
{
  pthread_rwlock_unlock(&rwlock);
}

// --- cMutex ----------------------------------------------------------------

cMutex::cMutex(void)
{
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK_NP);
  locked = 0;
  pthread_mutex_init(&mutex, &attr);
}

cMutex::~cMutex()
{
  pthread_mutex_destroy(&mutex);
}

void cMutex::Lock(void)
{
  pthread_mutex_lock(&mutex);
  locked++;
}

void cMutex::Unlock(void)
{
 if (!--locked)
    pthread_mutex_unlock(&mutex);
}

// --- cThread ---------------------------------------------------------------

	cThread::cThread(const char *Description)
	{
	  active = running = false;
	  childTid = 0;
	  description = NULL;
	  SetDescription("%s",Description);
	}

	cThread::~cThread()
	{
	  Cancel(); // just in case the derived class didn't call it
	  free(description);
	}

	void cThread::SetPriority(int Priority)
	{
	  if (setpriority(PRIO_PROCESS, 0, Priority) < 0)
	     LOG_ERROR;
	}

	void cThread::SetDescription(const char *Description, ...)
	{
	  free(description);
	  description = NULL;
	  if (Description) {
	     va_list ap;
	     va_start(ap, Description);
	     vasprintf(&description, Description, ap);
	     va_end(ap);
	     }
	}

	void *cThread::StartThread(cThread *Thread)
	{
	  if (Thread->description)
	     dsyslog("%s thread started (pid=%d, tid=%ld)", Thread->description, getpid(), pthread_self());
	  Thread->Action();
	  if (Thread->description)
	     dsyslog("%s thread ended (pid=%d, tid=%ld)", Thread->description, getpid(), pthread_self());
	  Thread->running = false;
	  Thread->active = false;
	  return NULL;
	}

	#define THREAD_STOP_TIMEOUT  3000 // ms to wait for a thread to stop before newly starting it
	#define THREAD_STOP_SLEEP      30 // ms to sleep while waiting for a thread to stop

	bool cThread::Start(void)
	{
	  if (!running) {
	     if (active) {
		// Wait until the previous incarnation of this thread has completely ended
		// before starting it newly:
		cTimeMs RestartTimeout;
		while (!running && active && RestartTimeout.Elapsed() < THREAD_STOP_TIMEOUT)
		      cCondWait::SleepMs(THREAD_STOP_SLEEP);
		}
	     if (!active) {
		active = running = true;
		if (pthread_create(&childTid, NULL, (void *(*) (void *))&StartThread, (void *)this) == 0) {
		   pthread_detach(childTid); // auto-reap
		   }
		else {
		   LOG_ERROR;
		   active = running = false;
		   return false;
		   }
		}
	     }
	  return true;
	}

	bool cThread::Active(void)
	{
	  if (active) {
	     //
	     // Single UNIX Spec v2 says:
	     //
	     // The pthread_kill() function is used to request
	     // that a signal be delivered to the specified thread.
	     //
	     // As in kill(), if sig is zero, error checking is
	     // performed but no signal is actually sent.
	     //
	     int err;
	     if ((err = pthread_kill(childTid, 0)) != 0) {
		if (err != ESRCH)
		   LOG_ERROR;
		childTid = 0;
		active = running = false;
		}
	     else
		return true;
	     }
	  return false;
	}

	tThreadId cThread::ThreadId(void)
	{
	  return syscall(__NR_gettid);
	}

	void cThread::Cancel(int WaitSeconds)
	{
	  running = false;
	  if (active && WaitSeconds > -1) {
	     if (WaitSeconds > 0) {
		for (time_t t0 = time(NULL) + WaitSeconds; time(NULL) < t0; ) {
		    if (!Active())
		       return;
		    cCondWait::SleepMs(10);
		    }
		esyslog("ERROR: %s thread %d won't end (waited %d seconds) - canceling it...", description ? description : "", (int)childTid, WaitSeconds);
		}
	     pthread_cancel(childTid);
	     childTid = 0;
	     active = false;
	     }
	}


// --- cMutexLock ------------------------------------------------------------

cMutexLock::cMutexLock(cMutex *Mutex)
{
  mutex = NULL;
  locked = false;
  Lock(Mutex);
}

cMutexLock::~cMutexLock()
{
  if (mutex && locked)
     mutex->Unlock();
}

bool cMutexLock::Lock(cMutex *Mutex)
{
  if (Mutex && !mutex) {
     mutex = Mutex;
     Mutex->Lock();
     locked = true;
     return true;
     }
  return false;
}

// --- cThreadLock -----------------------------------------------------------

cThreadLock::cThreadLock(cThread *Thread)
{
  thread = NULL;
  locked = false;
  Lock(Thread);
}

cThreadLock::~cThreadLock()
{
  if (thread && locked)
     thread->Unlock();
}

bool cThreadLock::Lock(cThread *Thread)
{
  if (Thread && !thread) {
     thread = Thread;
     Thread->Lock();
     locked = true;
     return true;
     }
  return false;
}

// --- cPipe -----------------------------------------------------------------

// cPipe::Open() and cPipe::Close() are based on code originally received from
// Andreas Vitting <Andreas@huji.de>

cPipe::cPipe(void)
{
  pid = -1;
  f = NULL;
}

cPipe::~cPipe()
{
  Close();
}

bool cPipe::Open(const char *Command, const char *Mode)
{
  int fd[2];

  if (pipe(fd) < 0) {
     LOG_ERROR;
     return false;
     }
  if ((pid = fork()) < 0) { // fork failed
     LOG_ERROR;
     close(fd[0]);
     close(fd[1]);
     return false;
     }

  const char *mode = "w";
  int iopipe = 0;

  if (pid > 0) { // parent process
     if (strcmp(Mode, "r") == 0) {
        mode = "r";
        iopipe = 1;
        }
     close(fd[iopipe]);
     f = fdopen(fd[1 - iopipe], mode);
     if ((f = fdopen(fd[1 - iopipe], mode)) == NULL) {
        LOG_ERROR;
        close(fd[1 - iopipe]);
        }
     return f != NULL;
     }
  else { // child process
     int iofd = STDOUT_FILENO;
     if (strcmp(Mode, "w") == 0) {
        mode = "r";
        iopipe = 1;
        iofd = STDIN_FILENO;
        }
     close(fd[iopipe]);
     if (dup2(fd[1 - iopipe], iofd) == -1) { // now redirect
        LOG_ERROR;
        close(fd[1 - iopipe]);
        _exit(-1);
        }
     else {
        int MaxPossibleFileDescriptors = getdtablesize();
        for (int i = STDERR_FILENO + 1; i < MaxPossibleFileDescriptors; i++)
            close(i); //close all dup'ed filedescriptors
        if (execl("/bin/sh", "sh", "-c", Command, NULL) == -1) {
           LOG_ERROR_STR(Command);
           close(fd[1 - iopipe]);
           _exit(-1);
           }
        }
     _exit(0);
     }
}

int cPipe::Close(void)
{
  int ret = -1;

  if (f) {
     fclose(f);
     f = NULL;
     }

  if (pid > 0) {
     int status = 0;
     int i = 5;
     while (i > 0) {
           ret = waitpid(pid, &status, WNOHANG);
           if (ret < 0) {
              if (errno != EINTR && errno != ECHILD) {
                 LOG_ERROR;
                 break;
                 }
              }
           else if (ret == pid)
              break;
           i--;
           cCondWait::SleepMs(100);
           }
     if (!i) {
        kill(pid, SIGKILL);
        ret = -1;
        }
     else if (ret == -1 || !WIFEXITED(status))
        ret = -1;
     pid = -1;
     }

  return ret;
}

// --- SystemExec ------------------------------------------------------------

int SystemExec(const char *Command, bool Detached)
{
  pid_t pid;

  if ((pid = fork()) < 0) { // fork failed
     LOG_ERROR;
     return -1;
     }

  if (pid > 0) { // parent process
     int status = 0;
     if (waitpid(pid, &status, 0) < 0) {
        LOG_ERROR;
        return -1;
        }
     return status;
     }
  else { // child process
     if (Detached) {
        // Fork again and let first child die - grandchild stays alive without parent
        if (fork() > 0)
           exit(0);
        // Start a new session
        pid_t sid = setsid();
        if (sid < 0)
           LOG_ERROR;
        // close STDIN and re-open as /dev/null
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull < 0 || dup2(devnull, 0) < 0)
           LOG_ERROR;
        }
     int MaxPossibleFileDescriptors = getdtablesize();
     for (int i = STDERR_FILENO + 1; i < MaxPossibleFileDescriptors; i++)
         close(i); //close all dup'ed filedescriptors
     if (execl("/bin/sh", "sh", "-c", Command, NULL) == -1) {
        LOG_ERROR_STR(Command);
        _exit(-1);
        }
     _exit(0);
     }
}

