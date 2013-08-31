/*
 * tools.c: Various tools
 *
 * See the main source file 'vdr.c' for copyright information and
 * how to reach the author.
 *
 * $Id: tools.c 1.97 2005/08/27 14:43:55 kls Exp $
 */

#include "include/vdr/tools.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/vfs.h>
#include <time.h>
#include <unistd.h>
#include "include/vdr/i18n.h"
#include "include/vdr/thread.h"

int SysLogLevel = 3;

#define MAXSYSLOGBUF 256

void syslog_with_tid(int priority, const char *format, ...)
{
  va_list ap;
  char fmt[MAXSYSLOGBUF];
  snprintf(fmt, sizeof(fmt), "[%d] %s", cThread::ThreadId(), format);
  va_start(ap, format);
  vsyslog(priority, fmt, ap);
  va_end(ap);
}

int BCD2INT(int x)
{
  return ((1000000 * BCDCHARTOINT((x >> 24) & 0xFF)) +
            (10000 * BCDCHARTOINT((x >> 16) & 0xFF)) +
              (100 * BCDCHARTOINT((x >>  8) & 0xFF)) +
                     BCDCHARTOINT( x        & 0xFF));
}

ssize_t safe_read(int filedes, void *buffer, size_t size)
{
  for (;;) {
      ssize_t p = read(filedes, buffer, size);
      if (p < 0 && errno == EINTR) {
         dsyslog("EINTR while reading from file handle %d - retrying", filedes);
         continue;
         }
      return p;
      }
}

ssize_t safe_write(int filedes, const void *buffer, size_t size)
{
  ssize_t p = 0;
  ssize_t written = size;
  const unsigned char *ptr = (const unsigned char *)buffer;
  while (size > 0) {
        p = write(filedes, ptr, size);
        if (p < 0) {
           if (errno == EINTR) {
              dsyslog("EINTR while writing to file handle %d - retrying", filedes);
              continue;
              }
           break;
           }
        ptr  += p;
        size -= p;
        }
  return p < 0 ? p : written;
}

void writechar(int filedes, char c)
{
  safe_write(filedes, &c, sizeof(c));
}

int WriteAllOrNothing(int fd, const uchar *Data, int Length, int TimeoutMs, int RetryMs)
{
  int written = 0;
  while (Length > 0) {
        int w = write(fd, Data + written, Length);
        if (w > 0) {
           Length -= w;
           written += w;
           }
        else if (written > 0 && !FATALERRNO) {
           // we've started writing, so we must finish it!
           cTimeMs t;
           cPoller Poller(fd, true);
           Poller.Poll(RetryMs);
           if (TimeoutMs > 0 && (TimeoutMs -= t.Elapsed()) <= 0)
              break;
           }
        else
           // nothing written yet (or fatal error), so we can just return the error code:
           return w;
        }
  return written;
}

char *strcpyrealloc(char *dest, const char *src)
{
  if (src) {
     int l = max(dest ? strlen(dest) : 0, strlen(src)) + 1; // don't let the block get smaller!
     dest = (char *)realloc(dest, l);
     if (dest)
        strcpy(dest, src);
     else
        esyslog("ERROR: out of memory");
     }
  else {
     free(dest);
     dest = NULL;
     }
  return dest;
}

char *strn0cpy(char *dest, const char *src, size_t n)
{
  char *s = dest;
  for ( ; --n && (*dest = *src) != 0; dest++, src++) ;
  *dest = 0;
  return s;
}

char *strreplace(char *s, char c1, char c2)
{
  char *p = s;

  while (p && *p) {
        if (*p == c1)
           *p = c2;
        p++;
        }
  return s;
}

char *strreplace(char *s, const char *s1, const char *s2)
{
  char *p = strstr(s, s1);
  if (p) {
     int of = p - s;
     int l  = strlen(s);
     int l1 = strlen(s1);
     int l2 = strlen(s2);
     if (l2 > l1)
        s = (char *)realloc(s, strlen(s) + l2 - l1 + 1);
     if (l2 != l1)
        memmove(s + of + l2, s + of + l1, l - of - l1 + 1);
     strncpy(s + of, s2, l2);
     }
  return s;
}

char *skipspace(const char *s)
{
  while (*s && isspace(*s))
        s++;
  return (char *)s;
}

char *stripspace(char *s)
{
  if (s && *s) {
     for (char *p = s + strlen(s) - 1; p >= s; p--) {
         if (!isspace(*p))
            break;
         *p = 0;
         }
     }
  return s;
}

char *compactspace(char *s)
{
  if (s && *s) {
     char *t = stripspace(skipspace(s));
     char *p = t;
     while (p && *p) {
           char *q = skipspace(p);
           if (q - p > 1)
              memmove(p + 1, q, strlen(q) + 1);
           p++;
           }
     if (t != s)
        memmove(s, t, strlen(t) + 1);
     }
  return s;
}

cString strescape(const char *s, const char *chars)
{
  char *buffer;
  const char *p = s;
  char *t = NULL;
  while (*p) {
        if (strchr(chars, *p)) {
           if (!t) {
              buffer = MALLOC(char, 2 * strlen(s) + 1);
              t = buffer + (p - s);
              s = strcpy(buffer, s);
              }
           *t++ = '\\';
           }
        if (t)
           *t++ = *p;
        p++;
        }
  if (t)
     *t = 0;
  return cString(s, t != NULL);
}

bool startswith(const char *s, const char *p)
{
  while (*p) {
        if (*p++ != *s++)
           return false;
        }
  return true;
}

bool endswith(const char *s, const char *p)
{
  const char *se = s + strlen(s) - 1;
  const char *pe = p + strlen(p) - 1;
  while (pe >= p) {
        if (*pe-- != *se-- || (se < s && pe >= p))
           return false;
        }
  return true;
}

bool isempty(const char *s)
{
  return !(s && *skipspace(s));
}

int numdigits(int n)
{
  char buf[16];
  snprintf(buf, sizeof(buf), "%d", n);
  return strlen(buf);
}

bool isnumber(const char *s)
{
  if (!*s)
     return false;
  while (*s) {
        if (!isdigit(*s))
           return false;
        s++;
        }
  return true;
}

cString AddDirectory(const char *DirName, const char *FileName)
{
  char *buf;
  asprintf(&buf, "%s/%s", DirName && *DirName ? DirName : ".", FileName);
  return cString(buf, true);
}

cString itoa(int n)
{
  char buf[16];
  snprintf(buf, sizeof(buf), "%d", n);
  return buf;
}

int FreeDiskSpaceMB(const char *Directory, int *UsedMB)
{
  if (UsedMB)
     *UsedMB = 0;
  int Free = 0;
  struct statfs statFs;
  if (statfs(Directory, &statFs) == 0) {
     double blocksPerMeg = 1024.0 * 1024.0 / statFs.f_bsize;
     if (UsedMB)
        *UsedMB = int((statFs.f_blocks - statFs.f_bfree) / blocksPerMeg);
     Free = int(statFs.f_bavail / blocksPerMeg);
     }
  else
     LOG_ERROR_STR(Directory);
  return Free;
}

bool DirectoryOk(const char *DirName, bool LogErrors)
{
  struct stat ds;
  if (stat(DirName, &ds) == 0) {
     if (S_ISDIR(ds.st_mode)) {
        if (access(DirName, R_OK | W_OK | X_OK) == 0)
           return true;
        else if (LogErrors)
           esyslog("ERROR: can't access %s", DirName);
        }
     else if (LogErrors)
        esyslog("ERROR: %s is not a directory", DirName);
     }
  else if (LogErrors)
     LOG_ERROR_STR(DirName);
  return false;
}

bool MakeDirs(const char *FileName, bool IsDirectory)
{
  bool result = true;
  char *s = strdup(FileName);
  char *p = s;
  if (*p == '/')
     p++;
  while ((p = strchr(p, '/')) != NULL || IsDirectory) {
        if (p)
           *p = 0;
        struct stat fs;
        if (stat(s, &fs) != 0 || !S_ISDIR(fs.st_mode)) {
           dsyslog("creating directory %s", s);
           if (mkdir(s, ACCESSPERMS) == -1) {
              LOG_ERROR_STR(s);
              result = false;
              break;
              }
           }
        if (p)
           *p++ = '/';
        else
           break;
        }
  free(s);
  return result;
}

bool RemoveFileOrDir(const char *FileName, bool FollowSymlinks)
{
  struct stat st;
  if (stat(FileName, &st) == 0) {
     if (S_ISDIR(st.st_mode)) {
        cReadDir d(FileName);
        if (d.Ok()) {
           struct dirent *e;
           while ((e = d.Next()) != NULL) {
                 if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) {
                    char *buffer;
                    asprintf(&buffer, "%s/%s", FileName, e->d_name);
                    if (FollowSymlinks) {
                       int size = strlen(buffer) * 2; // should be large enough
                       char *l = MALLOC(char, size);
                       int n = readlink(buffer, l, size);
                       if (n < 0) {
                          if (errno != EINVAL)
                             LOG_ERROR_STR(buffer);
                          }
                       else if (n < size) {
                          l[n] = 0;
                          dsyslog("removing %s", l);
                          if (remove(l) < 0)
                             LOG_ERROR_STR(l);
                          }
                       else
                          esyslog("ERROR: symlink name length (%d) exceeded anticipated buffer size (%d)", n, size);
                       free(l);
                       }
                    dsyslog("removing %s", buffer);
                    if (remove(buffer) < 0)
                       LOG_ERROR_STR(buffer);
                    free(buffer);
                    }
                 }
           }
        else {
           LOG_ERROR_STR(FileName);
           return false;
           }
        }
     dsyslog("removing %s", FileName);
     if (remove(FileName) < 0) {
        LOG_ERROR_STR(FileName);
        return false;
        }
     }
  else if (errno != ENOENT) {
     LOG_ERROR_STR(FileName);
     return false;
     }
  return true;
}

bool RemoveEmptyDirectories(const char *DirName, bool RemoveThis)
{
  cReadDir d(DirName);
  if (d.Ok()) {
     bool empty = true;
     struct dirent *e;
     while ((e = d.Next()) != NULL) {
           if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..") && strcmp(e->d_name, "lost+found")) {
              char *buffer;
              asprintf(&buffer, "%s/%s", DirName, e->d_name);
              struct stat st;
              if (stat(buffer, &st) == 0) {
                 if (S_ISDIR(st.st_mode)) {
                    if (!RemoveEmptyDirectories(buffer, true))
                       empty = false;
                    }
                 else
                    empty = false;
                 }
              else {
                 LOG_ERROR_STR(buffer);
                 free(buffer);
                 return false;
                 }
              free(buffer);
              }
           }
     if (RemoveThis && empty) {
        dsyslog("removing %s", DirName);
        if (remove(DirName) < 0) {
           LOG_ERROR_STR(DirName);
           return false;
           }
        }
     return empty;
     }
  else
     LOG_ERROR_STR(DirName);
  return false;
}

char *ReadLink(const char *FileName)
{
  char RealName[PATH_MAX];
  const char *TargetName = NULL;
  int n = readlink(FileName, RealName, sizeof(RealName) - 1);
  if (n < 0) {
     if (errno == ENOENT || errno == EINVAL) // file doesn't exist or is not a symlink
        TargetName = FileName;
     else // some other error occurred
        LOG_ERROR_STR(FileName);
     }
  else if (n < int(sizeof(RealName))) { // got it!
     RealName[n] = 0;
     TargetName = RealName;
     }
  else
     esyslog("ERROR: symlink's target name too long: %s", FileName);
  return TargetName ? strdup(TargetName) : NULL;
}

bool SpinUpDisk(const char *FileName)
{
  char *buf = NULL;
  for (int n = 0; n < 10; n++) {
      free(buf);
      if (DirectoryOk(FileName))
         asprintf(&buf, "%s/vdr-%06d", *FileName ? FileName : ".", n);
      else
         asprintf(&buf, "%s.vdr-%06d", FileName, n);
      if (access(buf, F_OK) != 0) { // the file does not exist
         timeval tp1, tp2;
         gettimeofday(&tp1, NULL);
         int f = open(buf, O_WRONLY | O_CREAT, DEFFILEMODE);
         // O_SYNC doesn't work on all file systems
         if (f >= 0) {
            if (fdatasync(f) < 0)
               LOG_ERROR_STR(buf);
            close(f);
            remove(buf);
            gettimeofday(&tp2, NULL);
            double seconds = (((long long)tp2.tv_sec * 1000000 + tp2.tv_usec) - ((long long)tp1.tv_sec * 1000000 + tp1.tv_usec)) / 1000000.0;
            if (seconds > 0.5)
               dsyslog("SpinUpDisk took %.2f seconds\n", seconds);
            free(buf);
            return true;
            }
         else
            LOG_ERROR_STR(buf);
         }
      }
  free(buf);
  esyslog("ERROR: SpinUpDisk failed");
  return false;
}

time_t LastModifiedTime(const char *FileName)
{
  struct stat fs;
  if (stat(FileName, &fs) == 0)
     return fs.st_mtime;
  return 0;
}

// --- cTimeMs ---------------------------------------------------------------

cTimeMs::cTimeMs(int Ms)
{
  Set(Ms);
}

uint64_t cTimeMs::Now(void)
{
  struct timeval t;
  if (gettimeofday(&t, NULL) == 0)
     return (uint64_t(t.tv_sec)) * 1000 + t.tv_usec / 1000;
  return 0;
}

void cTimeMs::Set(int Ms)
{
  begin = Now() + Ms;
}

bool cTimeMs::TimedOut(void)
{
  return Now() >= begin;
}

uint64_t cTimeMs::Elapsed(void)
{
  return Now() - begin;
}

// --- cString ---------------------------------------------------------------

cString::cString(const char *S, bool TakePointer)
{
  s = TakePointer ? (char *)S : S ? strdup(S) : NULL;
}

cString::~cString()
{
  free(s);
}

cString &cString::operator=(const cString &String)
{
  s = String.s ? strdup(String.s) : NULL;
  return *this;
}

cString cString::sprintf(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  char *buffer;
  vasprintf(&buffer, fmt, ap);
  return cString(buffer, true);
}

cString WeekDayName(int WeekDay)
{
  char buffer[4];
  WeekDay = WeekDay == 0 ? 6 : WeekDay - 1; // we start with monday==0!
  if (0 <= WeekDay && WeekDay <= 6) {
     const char *day = "MonTueWedThuFriSatSun";
     day += WeekDay * 3;
     strn0cpy(buffer, day, sizeof(buffer));
     return buffer;
     }
  else
     return "???";
}

cString WeekDayName(time_t t)
{
  struct tm tm_r;
  return WeekDayName(localtime_r(&t, &tm_r)->tm_wday);
}

cString DayDateTime(time_t t)
{
  char buffer[32];
  if (t == 0)
     time(&t);
  struct tm tm_r;
  tm *tm = localtime_r(&t, &tm_r);
  snprintf(buffer, sizeof(buffer), "%s %2d.%02d %02d:%02d", *WeekDayName(tm->tm_wday), tm->tm_mday, tm->tm_mon + 1, tm->tm_hour, tm->tm_min);
  return buffer;
}

cString TimeToString(time_t t)
{
  char buffer[32];
  if (ctime_r(&t, buffer)) {
     buffer[strlen(buffer) - 1] = 0; // strip trailing newline
     return buffer;
     }
  return "???";
}

cString DateString(time_t t)
{
  char buf[32];
  struct tm tm_r;
  tm *tm = localtime_r(&t, &tm_r);
  char *p = stpcpy(buf, WeekDayName(tm->tm_wday));
  *p++ = ' ';
  strftime(p, sizeof(buf) - (p - buf), "%d.%m.%Y", tm);
  return buf;
}

cString TimeString(time_t t)
{
  char buf[25];
  struct tm tm_r;
  strftime(buf, sizeof(buf), "%R", localtime_r(&t, &tm_r));
  return buf;
}

// --- cReadLine -------------------------------------------------------------

char *cReadLine::Read(FILE *f)
{
  if (fgets(buffer, sizeof(buffer), f) > 0) {
     int l = strlen(buffer) - 1;
     if (l >= 0 && buffer[l] == '\n')
        buffer[l] = 0;
     return buffer;
     }
  return NULL;
}

// --- cPoller ---------------------------------------------------------------

cPoller::cPoller(int FileHandle, bool Out)
{
  numFileHandles = 0;
  Add(FileHandle, Out);
}

bool cPoller::Add(int FileHandle, bool Out)
{
  if (FileHandle >= 0) {
     for (int i = 0; i < numFileHandles; i++) {
         if (pfd[i].fd == FileHandle && pfd[i].events == (Out ? POLLOUT : POLLIN))
            return true;
         }
     if (numFileHandles < MaxPollFiles) {
        pfd[numFileHandles].fd = FileHandle;
        pfd[numFileHandles].events = Out ? POLLOUT : POLLIN;
        pfd[numFileHandles].revents = 0;
        numFileHandles++;
        return true;
        }
     esyslog("ERROR: too many file handles in cPoller");
     }
  return false;
}

bool cPoller::Poll(int TimeoutMs)
{
  if (numFileHandles) {
     if (poll(pfd, numFileHandles, TimeoutMs) != 0)
        return true; // returns true even in case of an error, to let the caller
                     // access the file and thus see the error code
     }
  return false;
}

// --- cReadDir --------------------------------------------------------------

cReadDir::cReadDir(const char *Directory)
{
  directory = opendir(Directory);
}

cReadDir::~cReadDir()
{
  if (directory)
     closedir(directory);
}

struct dirent *cReadDir::Next(void)
{
  return directory && readdir_r(directory, &u.d, &result) == 0 ? result : NULL;
}

// --- cFile -----------------------------------------------------------------

bool cFile::files[FD_SETSIZE] = { false };
int cFile::maxFiles = 0;

cFile::cFile(void)
{
  f = -1;
}

cFile::~cFile()
{
  Close();
}

bool cFile::Open(const char *FileName, int Flags, mode_t Mode)
{
  if (!IsOpen())
     return Open(open(FileName, Flags, Mode));
  esyslog("ERROR: attempt to re-open %s", FileName);
  return false;
}

bool cFile::Open(int FileDes)
{
  if (FileDes >= 0) {
     if (!IsOpen()) {
        f = FileDes;
        if (f >= 0) {
           if (f < FD_SETSIZE) {
              if (f >= maxFiles)
                 maxFiles = f + 1;
              if (!files[f])
                 files[f] = true;
              else
                 esyslog("ERROR: file descriptor %d already in files[]", f);
              return true;
              }
           else
              esyslog("ERROR: file descriptor %d is larger than FD_SETSIZE (%d)", f, FD_SETSIZE);
           }
        }
     else
        esyslog("ERROR: attempt to re-open file descriptor %d", FileDes);
     }
  return false;
}

void cFile::Close(void)
{
  if (f >= 0) {
     close(f);
     files[f] = false;
     f = -1;
     }
}

bool cFile::Ready(bool Wait)
{
  return f >= 0 && AnyFileReady(f, Wait ? 1000 : 0);
}

bool cFile::AnyFileReady(int FileDes, int TimeoutMs)
{
  fd_set set;
  FD_ZERO(&set);
  for (int i = 0; i < maxFiles; i++) {
      if (files[i])
         FD_SET(i, &set);
      }
  if (0 <= FileDes && FileDes < FD_SETSIZE && !files[FileDes])
     FD_SET(FileDes, &set); // in case we come in with an arbitrary descriptor
  if (TimeoutMs == 0)
     TimeoutMs = 10; // load gets too heavy with 0
  struct timeval timeout;
  timeout.tv_sec  = TimeoutMs / 1000;
  timeout.tv_usec = (TimeoutMs % 1000) * 1000;
  return select(FD_SETSIZE, &set, NULL, NULL, &timeout) > 0 && (FileDes < 0 || FD_ISSET(FileDes, &set));
}

bool cFile::FileReady(int FileDes, int TimeoutMs)
{
  fd_set set;
  struct timeval timeout;
  FD_ZERO(&set);
  FD_SET(FileDes, &set);
  if (TimeoutMs >= 0) {
     if (TimeoutMs < 100)
        TimeoutMs = 100;
     timeout.tv_sec  = TimeoutMs / 1000;
     timeout.tv_usec = (TimeoutMs % 1000) * 1000;
     }
  return select(FD_SETSIZE, &set, NULL, NULL, (TimeoutMs >= 0) ? &timeout : NULL) > 0 && FD_ISSET(FileDes, &set);
}

bool cFile::FileReadyForWriting(int FileDes, int TimeoutMs)
{
  fd_set set;
  struct timeval timeout;
  FD_ZERO(&set);
  FD_SET(FileDes, &set);
  if (TimeoutMs < 100)
     TimeoutMs = 100;
  timeout.tv_sec  = 0;
  timeout.tv_usec = TimeoutMs * 1000;
  return select(FD_SETSIZE, NULL, &set, NULL, &timeout) > 0 && FD_ISSET(FileDes, &set);
}

// --- cSafeFile -------------------------------------------------------------

cSafeFile::cSafeFile(const char *FileName)
{
  f = NULL;
  fileName = ReadLink(FileName);
  tempName = fileName ? MALLOC(char, strlen(fileName) + 5) : NULL;
  if (tempName)
     strcat(strcpy(tempName, fileName), ".$$$");
}

cSafeFile::~cSafeFile()
{
  if (f)
     fclose(f);
  unlink(tempName);
  free(fileName);
  free(tempName);
}

bool cSafeFile::Open(void)
{
  if (!f && fileName && tempName) {
     f = fopen(tempName, "w");
     if (!f)
        LOG_ERROR_STR(tempName);
     }
  return f != NULL;
}

bool cSafeFile::Close(void)
{
  bool result = true;
  if (f) {
     if (ferror(f) != 0) {
        LOG_ERROR_STR(tempName);
        result = false;
        }
     if (fclose(f) < 0) {
        LOG_ERROR_STR(tempName);
        result = false;
        }
     f = NULL;
     if (result && rename(tempName, fileName) < 0) {
        LOG_ERROR_STR(fileName);
        result = false;
        }
     }
  else
     result = false;
  return result;
}

// --- cLockFile -------------------------------------------------------------

#define LOCKFILENAME      ".lock-vdr"
#define LOCKFILESTALETIME 600 // seconds before considering a lock file "stale"

cLockFile::cLockFile(const char *Directory)
{
  fileName = NULL;
  f = -1;
  if (DirectoryOk(Directory))
     asprintf(&fileName, "%s/%s", Directory, LOCKFILENAME);
}

cLockFile::~cLockFile()
{
  Unlock();
  free(fileName);
}

bool cLockFile::Lock(int WaitSeconds)
{
  if (f < 0 && fileName) {
     time_t Timeout = time(NULL) + WaitSeconds;
     do {
        f = open(fileName, O_WRONLY | O_CREAT | O_EXCL, DEFFILEMODE);
        if (f < 0) {
           if (errno == EEXIST) {
              struct stat fs;
              if (stat(fileName, &fs) == 0) {
                 if (abs(time(NULL) - fs.st_mtime) > LOCKFILESTALETIME) {
                    esyslog("ERROR: removing stale lock file '%s'", fileName);
                    if (remove(fileName) < 0) {
                       LOG_ERROR_STR(fileName);
                       break;
                       }
                    continue;
                    }
                 }
              else if (errno != ENOENT) {
                 LOG_ERROR_STR(fileName);
                 break;
                 }
              }
           else {
              LOG_ERROR_STR(fileName);
              break;
              }
           if (WaitSeconds)
              sleep(1);
           }
        } while (f < 0 && time(NULL) < Timeout);
     }
  return f >= 0;
}

void cLockFile::Unlock(void)
{
  if (f >= 0) {
     close(f);
     remove(fileName);
     f = -1;
     }
}

// --- cListObject -----------------------------------------------------------

cListObject::cListObject(void)
{
  prev = next = NULL;
}

cListObject::~cListObject()
{
}

void cListObject::Append(cListObject *Object)
{
  next = Object;
  Object->prev = this;
}

void cListObject::Insert(cListObject *Object)
{
  prev = Object;
  Object->next = this;
}

void cListObject::Unlink(void)
{
  if (next)
     next->prev = prev;
  if (prev)
     prev->next = next;
  next = prev = NULL;
}

int cListObject::Index(void) const
{
  cListObject *p = prev;
  int i = 0;

  while (p) {
        i++;
        p = p->prev;
        }
  return i;
}

// --- cListBase -------------------------------------------------------------

cListBase::cListBase(void)
{ 
  objects = lastObject = NULL;
  count = 0;
}

cListBase::~cListBase()
{
  Clear();
}

void cListBase::Add(cListObject *Object, cListObject *After)
{ 
  if (After && After != lastObject) {
     After->Next()->Insert(Object);
     After->Append(Object);
     }
  else {
     if (lastObject)
        lastObject->Append(Object);
     else
        objects = Object;
     lastObject = Object;
     }
  count++;
}

void cListBase::Ins(cListObject *Object, cListObject *Before)
{ 
  if (Before && Before != objects) {
     Before->Prev()->Append(Object);
     Before->Insert(Object);
     }
  else {
     if (objects)
        objects->Insert(Object);
     else
        lastObject = Object;
     objects = Object;
     }
  count++;
}

void cListBase::Del(cListObject *Object, bool DeleteObject)
{
  if (Object == objects)
     objects = Object->Next();
  if (Object == lastObject)
     lastObject = Object->Prev();
  Object->Unlink();
  if (DeleteObject)
     delete Object;
  count--;
}

void cListBase::Move(int From, int To)
{
  Move(Get(From), Get(To));
}

void cListBase::Move(cListObject *From, cListObject *To)
{
  if (From && To) {
     if (From->Index() < To->Index())
        To = To->Next();
     if (From == objects)
        objects = From->Next();
     if (From == lastObject)
        lastObject = From->Prev();
     From->Unlink();
     if (To) {
        if (To->Prev())
           To->Prev()->Append(From);
        From->Append(To);
        }
     else {
        lastObject->Append(From);
        lastObject = From;
        }
     if (!From->Prev())
        objects = From;
     }
}

void cListBase::Clear(void)
{
  while (objects) {
        cListObject *object = objects->Next();
        delete objects;
        objects = object;
        }
  objects = lastObject = NULL;
  count = 0;
}

cListObject *cListBase::Get(int Index) const
{
  if (Index < 0)
     return NULL;
  cListObject *object = objects;
  while (object && Index-- > 0)
        object = object->Next();
  return object;
}

static int CompareListObjects(const void *a, const void *b)
{
  const cListObject *la = *(const cListObject **)a;
  const cListObject *lb = *(const cListObject **)b;
  return la->Compare(*lb);
}

void cListBase::Sort(void)
{
  int n = Count();
  cListObject *a[n];
  cListObject *object = objects;
  int i = 0;
  while (object && i < n) {
        a[i++] = object;
        object = object->Next();
        }
  qsort(a, n, sizeof(cListObject *), CompareListObjects);
  objects = lastObject = NULL;
  for (i = 0; i < n; i++) {
      a[i]->Unlink();
      count--;
      Add(a[i]);
      }
}

// --- cHashBase -------------------------------------------------------------

cHashBase::cHashBase(int Size)
{
  size = Size;
  hashTable = (cList<cHashObject>**)calloc(size, sizeof(cList<cHashObject>*));
}

cHashBase::~cHashBase(void)
{
  for (int i = 0; i < size; i++)
      delete hashTable[i];
  free(hashTable);
}

void cHashBase::Add(cListObject *Object, unsigned int Id)
{
  unsigned int hash = hashfn(Id);
  if (!hashTable[hash])
     hashTable[hash] = new cList<cHashObject>;
  hashTable[hash]->Add(new cHashObject(Object, Id));
}

void cHashBase::Del(cListObject *Object, unsigned int Id)
{
  cList<cHashObject> *list = hashTable[hashfn(Id)];
  if (list) {
     for (cHashObject *hob = list->First(); hob; hob = list->Next(hob)) {
         if (hob->object == Object) {
            list->Del(hob);
            break;
            }
         }
     }
}

cListObject *cHashBase::Get(unsigned int Id) const
{
  cList<cHashObject> *list = hashTable[hashfn(Id)];
  if (list) {
     for (cHashObject *hob = list->First(); hob; hob = list->Next(hob)) {
         if (hob->id == Id)
            return hob->object;
         }
     }
  return NULL;
}

cList<cHashObject> *cHashBase::GetList(unsigned int Id) const
{
  return hashTable[hashfn(Id)];
}
