/*
 * Softcam plugin to VDR (C++)
 *
 * This code is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This code is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include <vdr/thread.h>
#include <vdr/tools.h>

#include "network.h"
#include "misc.h"
#include "log-core.h"

#define DEFAULT_CONNECT_TIMEOUT   20*1000   // ms
#define DEFAULT_READWRITE_TIMEOUT 3*1000    // ms
#define DEFAULT_IDLE_TIMEOUT      120*1000  // ms

// -- cNetWatcher ---------------------------------------------------------------

class cNetWatcher : protected cThread {
private:
  cSimpleList<cNetSocket> socks;
protected:
  virtual void Action(void);
public:
  cNetWatcher(void);
  ~cNetWatcher();
  void Up(cNetSocket *so);
  void Down(cNetSocket *so);
  };

static cNetWatcher nw;

cNetWatcher::cNetWatcher(void)
:cThread("Netwatcher")
{}

cNetWatcher::~cNetWatcher()
{
  Cancel(2);
  Lock();
  cNetSocket *so;
  while((so=socks.First())) socks.Del(so,false);
  Unlock();
}

void cNetWatcher::Up(cNetSocket *so)
{
  Lock();
  socks.Add(so);
  if(!Active()) Start();
  Unlock();
}

void cNetWatcher::Down(cNetSocket *so)
{
  Lock();
  socks.Del(so,false);
  Unlock();
}

void cNetWatcher::Action(void)
{
  while(Running()) {
    sleep(1);
    Lock();
    for(cNetSocket *so=socks.First(); so;) {
      cNetSocket *next=socks.Next(so);
      so->Lock();
      if(so->connected && so->activity.TimedOut()) {
        PRINTF(L_CORE_NET,"idle timeout, disconnected %s:%d",so->hostname,so->port);
        so->Disconnect();
        }
      so->Unlock();
      so=next;
      }
    Unlock();
    }
}

// -- cNetSocket ------------------------------------------------------------------

cNetSocket::cNetSocket(void)
{
  hostname=0; sd=-1; udp=connected=quietlog=false;
  conTimeout=DEFAULT_CONNECT_TIMEOUT; rwTimeout=DEFAULT_READWRITE_TIMEOUT;
  idleTimeout=DEFAULT_IDLE_TIMEOUT;
}

cNetSocket::~cNetSocket()
{
  Disconnect();
  free(hostname);
}

void cNetSocket::Activity(void)
{
  activity.Set(idleTimeout);
}

bool cNetSocket::GetAddr(struct sockaddr_in *saddr, const char *Hostname, int Port)
{
  const struct hostent * const hostaddr=gethostbyname(Hostname);
  if(hostaddr) {
    saddr->sin_family=AF_INET;
    saddr->sin_port=htons(Port);
    saddr->sin_addr.s_addr=((struct in_addr *)hostaddr->h_addr)->s_addr;
    return true;
    }
  PRINTF(L_GEN_ERROR,"socket: name lookup error for %s",Hostname);
  return false;
}

cString cNetSocket::StrError(int err) const
{
  char buf[64];
  return strerror_r(err,buf,sizeof(buf));
}

int cNetSocket::GetSocket(bool Udp)
{
  int proto=0;
  const struct protoent * const ptrp=getprotobyname(Udp?"udp":"tcp");
  if(ptrp) proto=ptrp->p_proto;
  int so;
  if((so=socket(PF_INET,Udp?SOCK_DGRAM:SOCK_STREAM,proto))>=0) {
    int flgs=fcntl(so,F_GETFL);
    if(flgs>=0) {
      if(fcntl(so,F_SETFL,flgs|O_NONBLOCK)==0)
        return so;
      else PRINTF(L_GEN_ERROR,"socket: fcntl SETFL failed: %s",*StrError(errno));
      }
    else PRINTF(L_GEN_ERROR,"socket: fcntl GETFL failed: %s",*StrError(errno));
    close(so);
    }
  else PRINTF(L_GEN_ERROR,"socket: socket failed: %s",*StrError(errno));
  return -1;
}

bool cNetSocket::Connect(const char *Hostname, int Port, int timeout)
{
  Lock();
  Disconnect();
  if(Hostname) {
    free(hostname);
    hostname=strdup(Hostname); port=Port;
    }
  if(timeout<0) timeout=conTimeout;
  struct sockaddr_in socketAddr;
  if(GetAddr(&socketAddr,hostname,port) && (sd=GetSocket(udp))>=0) {
    if(!quietlog) PRINTF(L_CORE_NET,"connecting to %s:%d/%s (%d.%d.%d.%d)",
               hostname,port,udp?"udp":"tcp",
               (socketAddr.sin_addr.s_addr>> 0)&0xff,(socketAddr.sin_addr.s_addr>> 8)&0xff,(socketAddr.sin_addr.s_addr>>16)&0xff,(socketAddr.sin_addr.s_addr>>24)&0xff);
    int r;
    do { r=connect(sd,(struct sockaddr *)&socketAddr,sizeof(socketAddr)); } while(r<0 && errno==EINTR);
    if(r==0) connected=true;
    else if(errno==EINPROGRESS) {
      if(Select(false,timeout)>0) {
        int r=-1;
        unsigned int l=sizeof(r);
        if(getsockopt(sd,SOL_SOCKET,SO_ERROR,&r,&l)==0) {
          if(r==0)
            connected=true;
          else PRINTF(L_GEN_ERROR,"socket: connect failed (late): %s",*StrError(r));
          }
        else PRINTF(L_GEN_ERROR,"socket: getsockopt failed: %s",*StrError(errno));
        }
      else PRINTF(L_GEN_ERROR,"socket: connect timed out");
      }
    else PRINTF(L_GEN_ERROR,"socket: connect failed: %s",*StrError(errno));

    if(connected) { Activity(); Unlock(); nw.Up(this); return true; }
    }
  Unlock();
  Disconnect();
  return false;
}

bool cNetSocket::Bind(const char *Hostname, int Port)
{
  Lock();
  Disconnect();
  if(Hostname) {
    free(hostname);
    hostname=strdup(Hostname); port=Port;
    }
  struct sockaddr_in socketAddr;
  if(GetAddr(&socketAddr,hostname,port) && (sd=GetSocket(udp))>=0) {
    if(!quietlog) PRINTF(L_CORE_NET,"socket: binding to %s:%d/%s (%d.%d.%d.%d)",
               hostname,port,udp?"udp":"tcp",
               (socketAddr.sin_addr.s_addr>> 0)&0xff,(socketAddr.sin_addr.s_addr>> 8)&0xff,(socketAddr.sin_addr.s_addr>>16)&0xff,(socketAddr.sin_addr.s_addr>>24)&0xff);
    int r;
    do { r=bind(sd,(struct sockaddr *)&socketAddr,sizeof(socketAddr)); } while(r<0 && errno==EINTR);
    if(r==0) {
      connected=true;
      Activity(); Unlock(); nw.Up(this);
      return true;
      }
    else PRINTF(L_GEN_ERROR,"socket: bind failed: %s",*StrError(errno));
    }
  Unlock();
  Disconnect();
  return false;
}

void cNetSocket::Disconnect(void)
{
  nw.Down(this);
  cMutexLock lock(this);
  if(sd>=0) { close(sd); sd=-1; }
  quietlog=connected=false;
}

void cNetSocket::Flush(void)
{
  cMutexLock lock(this);
  if(sd>=0) {
    unsigned char buff[512];
    while(read(sd,buff,sizeof(buff))>0) Activity();
    }
}

// len>0 : block read len bytes or fail
// len<0 : read available bytes up to len
int cNetSocket::Read(unsigned char *data, int len, int timeout)
{
  cMutexLock lock(this);
  if(timeout<0) timeout=rwTimeout;
  bool blockmode=true;
  if(len<0) { len=-len; blockmode=false; }
  else if(len==0) {
    PRINTF(L_GEN_DEBUG,"internal: zero length on socket read");
    errno=EINVAL;
    return -1;
    }
  int cnt=0, r;
  cTimeMs tim;
  do {
    r=timeout;
    if(r>0 && blockmode) { r-=tim.Elapsed(); if(r<10) r=10; }
    r=Select(true,r);
    if(r>0) {
      do { r=read(sd,data+cnt,len-cnt); } while(r<0 && errno==EINTR);
      if(r<0) PRINTF(L_GEN_ERROR,"socket: read failed: %s",*StrError(errno));
      else if(r>0) cnt+=r;
      else {
        PRINTF(L_GEN_ERROR,"socket: EOF on read");
        errno=ECONNRESET;
        }
      }
    } while(r>0 && cnt<len && blockmode);
  Activity();
  if((!blockmode && cnt>0) || cnt>=len) {
    HEXDUMP(L_CORE_NETDATA,data,cnt,"network read");
    return cnt;
    }
  return -1;
}

int cNetSocket::Write(const unsigned char *data, int len, int timeout)
{
  cMutexLock lock(this);
  if(timeout<0) timeout=rwTimeout;
  int cnt=0, r;
  cTimeMs tim;
  do {
    r=timeout;
    if(r>0) { r-=tim.Elapsed(); if(r<10) r=10; }
    r=Select(false,r);
    if(r>0) {
      do { r=write(sd,data+cnt,len-cnt); } while(r<0 && errno==EINTR);
      if(r<0) PRINTF(L_GEN_ERROR,"socket: write failed: %s",*StrError(errno));
      else if(r>0) cnt+=r;
      }
    } while(r>0 && cnt<len);
  Activity();
  if(cnt>=len) {
    HEXDUMP(L_CORE_NETDATA,data,cnt,"network write");
    return cnt;
    }
  return -1;
}

int cNetSocket::SendTo(const char *Host, int Port, const unsigned char *data, int len, int timeout)
{
  cMutexLock lock(this);
  if(timeout<0) timeout=rwTimeout;
  int cnt=0, r=-1;
  struct sockaddr_in saddr;
  if(GetAddr(&saddr,Host,Port)) {
    cTimeMs tim;
    do {
      r=timeout;
      if(r>0) { r-=tim.Elapsed(); if(r<10) r=10; }
      r=Select(false,r);
      if(r>0) {
        do { r=sendto(sd,data+cnt,len-cnt,0,(struct sockaddr *)&saddr,sizeof(saddr)); } while(r<0 && errno==EINTR);
        if(r<0) PRINTF(L_GEN_ERROR,"socket: sendto %d.%d.%d.%d:%d failed: %s",(saddr.sin_addr.s_addr>> 0)&0xff,(saddr.sin_addr.s_addr>> 8)&0xff,(saddr.sin_addr.s_addr>>16)&0xff,(saddr.sin_addr.s_addr>>24)&0xff,Port,*StrError(errno));
        else if(r>0) cnt+=r;
        }
      } while(r>0 && cnt<len);
    }
  Activity();
  if(cnt>=len) {
    HEXDUMP(L_CORE_NETDATA,data,cnt,"network sendto %d.%d.%d.%d:%d",(saddr.sin_addr.s_addr>> 0)&0xff,(saddr.sin_addr.s_addr>> 8)&0xff,(saddr.sin_addr.s_addr>>16)&0xff,(saddr.sin_addr.s_addr>>24)&0xff,Port);
    return cnt;
    }
  return -1;
}

int cNetSocket::Select(bool forRead, int timeout)
{
  if(sd>=0) {
    fd_set fds;
    FD_ZERO(&fds); FD_SET(sd,&fds);
    struct timeval tv;

    if(timeout>0 && timeout<60)
      PRINTF(L_GEN_DEBUG,"socket: internal: small timeout value %d",timeout);

    tv.tv_sec=timeout/1000; tv.tv_usec=(timeout%1000)*1000;
    int r;
    do { r=select(sd+1,forRead ? &fds:0,forRead ? 0:&fds,0,&tv); } while(r<0 && errno==EINTR);
    if(r>0) return 1;
    else if(r<0) {
      PRINTF(L_GEN_ERROR,"socket: select failed: %s",*StrError(errno));
      return -1;
      }
    else {
      if(timeout>0 && !quietlog) PRINTF(L_CORE_NET,"socket: select timed out (%d ms)",timeout);
      errno=ETIMEDOUT;
      return 0;
      }
    }
  return -1;
}
