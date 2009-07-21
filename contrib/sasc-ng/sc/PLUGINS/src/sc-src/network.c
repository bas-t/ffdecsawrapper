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

const char *netscript=0;
int netTimeout=60*1000;   // timeout for shutting down dialup-network

// -- cNetWatcher ---------------------------------------------------------------

class cNetWatcher : protected cThread {
private:
  int count;
  cTimeMs down;
  bool netup, downDelay;
  cSimpleList<cNetSocket> socks;
  //
  int RunCommand(const char *cmd, const char *state);
protected:
  virtual void Action(void);
public:
  cNetWatcher(void);
  ~cNetWatcher();
  void Up(cNetSocket *so);
  void Down(cNetSocket *so);
  void Block(void) { Lock(); }
  void Unblock(void) { Unlock(); }
  };

static cNetWatcher nw;

cNetWatcher::cNetWatcher(void)
:cThread("Netwatcher")
{
  count=0; netup=downDelay=false;
}

cNetWatcher::~cNetWatcher()
{
  Cancel(2);
  Lock();
  if(netup) RunCommand(netscript,"down");
  cNetSocket *so;
  while((so=socks.First())) socks.Del(so,false);
  Unlock();
}

void cNetWatcher::Up(cNetSocket *so)
{
  Lock();
  socks.Add(so);
  if(!Active()) Start();
  if(netscript) {
    downDelay=false; netup=true;
    RunCommand(netscript,"up");
    count++;
    PRINTF(L_CORE_NET,"netwatch up (%d)",count);
    }
  else PRINTF(L_CORE_NET,"netwatch up");
  Unlock();
}

void cNetWatcher::Down(cNetSocket *so)
{
  Lock();
  socks.Del(so,false);
  if(netscript) {
    if(--count==0) {
      downDelay=true;
      down.Set(netTimeout);
      }
    PRINTF(L_CORE_NET,"netwatch down (%d)",count);
    }
  else PRINTF(L_CORE_NET,"netwatch down");
  Unlock();
}

void cNetWatcher::Action(void)
{
  while(Running()) {
    sleep(1);
    Lock();
    if(downDelay && down.TimedOut()) {
      PRINTF(L_CORE_NET,"netdown timeout expired");
      if(netup) {
        RunCommand(netscript,"down");
        netup=false;
        }
      downDelay=false;
      }
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

int cNetWatcher::RunCommand(const char *cmd, const char *state)
{
  char *tmp;
  asprintf(&tmp,"%s %s",cmd,state);
  PRINTF(L_CORE_NET,"netwatch cmd exec '%s'",tmp);
  int res=SystemExec(tmp);
  free(tmp);
  return res;
}

// -- cNetSocket ------------------------------------------------------------------

cNetSocket::cNetSocket(int ConnectTimeout, int ReadWriteTimeout, int IdleTimeout, bool Udp)
{
  hostname=0; sd=-1; connected=netup=false;
  udp=Udp; conTimeout=ConnectTimeout; rwTimeout=ReadWriteTimeout;
  idleTimeout=IdleTimeout*1000;
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
      else PRINTF(L_GEN_ERROR,"socket: fcntl SETFL failed: %s",strerror(errno)); 
      }
    else PRINTF(L_GEN_ERROR,"socket: fcntl GETFL failed: %s",strerror(errno));
    close(so);
    }
  else PRINTF(L_GEN_ERROR,"socket: socket failed: %s",strerror(errno));
  return -1;
}

bool cNetSocket::Connect(const char *Hostname, int Port, int timeout)
{
  nw.Block();
  Lock();
  Disconnect();
  if(Hostname) {
    free(hostname);
    hostname=strdup(Hostname); port=Port;
    }
  if(timeout<0) timeout=conTimeout;
  nw.Up(this); netup=true;
  nw.Unblock();
  struct sockaddr_in socketAddr;
  if(GetAddr(&socketAddr,hostname,port) && (sd=GetSocket(udp))>=0) {
    PRINTF(L_CORE_NET,"connecting to %s:%d/%s (%d.%d.%d.%d)",
               hostname,port,udp?"udp":"tcp",
               (socketAddr.sin_addr.s_addr>> 0)&0xff,(socketAddr.sin_addr.s_addr>> 8)&0xff,(socketAddr.sin_addr.s_addr>>16)&0xff,(socketAddr.sin_addr.s_addr>>24)&0xff);
    if(connect(sd,(struct sockaddr *)&socketAddr,sizeof(socketAddr))==0)
      connected=true;
    else if(errno==EINPROGRESS) {
      if(Select(false,timeout)>0) {
        int r=-1;
        unsigned int l=sizeof(r);
        if(getsockopt(sd,SOL_SOCKET,SO_ERROR,&r,&l)==0) {
          if(r==0)
            connected=true;
          else PRINTF(L_GEN_ERROR,"socket: connect failed (late): %s",strerror(r));
          }
        else PRINTF(L_GEN_ERROR,"socket: getsockopt failed: %s",strerror(errno));
        }
      else PRINTF(L_GEN_ERROR,"socket: connect timed out");
      }
    else PRINTF(L_GEN_ERROR,"socket: connect failed: %s",strerror(errno));

    if(connected) { Activity(); Unlock(); return true; }
    }
  Unlock();
  Disconnect();
  return false;
}


bool cNetSocket::Bind(const char *Hostname, int Port)
{
  nw.Block();
  Lock();
  Disconnect();
  if(Hostname) {
    free(hostname);
    hostname=strdup(Hostname); port=Port;
    }
  nw.Up(this); netup=true;
  nw.Unblock();
  struct sockaddr_in socketAddr;
  if(GetAddr(&socketAddr,hostname,port) && (sd=GetSocket(udp))>=0) {
    PRINTF(L_CORE_NET,"socket: binding to %s:%d/%s (%d.%d.%d.%d)",
               hostname,port,udp?"udp":"tcp",
               (socketAddr.sin_addr.s_addr>> 0)&0xff,(socketAddr.sin_addr.s_addr>> 8)&0xff,(socketAddr.sin_addr.s_addr>>16)&0xff,(socketAddr.sin_addr.s_addr>>24)&0xff);
    if(bind(sd,(struct sockaddr *)&socketAddr,sizeof(socketAddr))==0) {
      connected=true;
      Activity(); Unlock();
      return true;
      }
    else PRINTF(L_GEN_ERROR,"socket: bind failed: %s",strerror(errno));
    }
  Unlock();
  Disconnect();
  return false;
}

void cNetSocket::Disconnect(void)
{
  nw.Block();
  cMutexLock lock(this);
  if(sd>=0) { close(sd); sd=-1; }
  connected=false;
  if(netup) { nw.Down(this); netup=false; }
  nw.Unblock();
}

void cNetSocket::Flush(void)
{
  cMutexLock lock(this);
  if(sd>=0) {
    unsigned char buff[512];
    while(read(sd,buff,sizeof(buff))>0) Activity();
    }
}

int cNetSocket::Read(unsigned char *data, int len, int timeout)
{
  cMutexLock lock(this);
  if(timeout<0) timeout=rwTimeout;
  int r=Select(true,timeout);
  if(r>0) {
    r=read(sd,data,len);
    if(r<0) PRINTF(L_GEN_ERROR,"socket: read failed: %s",strerror(errno));
    else if(r>0) HEXDUMP(L_CORE_NETDATA,data,r,"network read");
    }
  Activity();
  return r;
}

int cNetSocket::Write(const unsigned char *data, int len, int timeout)
{
  cMutexLock lock(this);
  if(timeout<0) timeout=rwTimeout;
  int r=Select(false,timeout);
  if(r>0) {
    r=write(sd,data,len);
    if(r<0) PRINTF(L_GEN_ERROR,"socket: write failed: %s",strerror(errno));
    else if(r>0) HEXDUMP(L_CORE_NETDATA,data,r,"network write");
    }
  Activity();
  return r;
}

int cNetSocket::SendTo(const char *Host, int Port, const unsigned char *data, int len, int timeout)
{
  cMutexLock lock(this);
  if(timeout<0) timeout=rwTimeout;
  int r=Select(false,timeout);
  if(r>0) {
    struct sockaddr_in saddr;
    if(GetAddr(&saddr,Host,Port)) {
      r=sendto(sd,data,len,0,(struct sockaddr *)&saddr,sizeof(saddr));
      if(r<0) PRINTF(L_GEN_ERROR,"socket: sendto %d.%d.%d.%d:%d failed: %s",(saddr.sin_addr.s_addr>> 0)&0xff,(saddr.sin_addr.s_addr>> 8)&0xff,(saddr.sin_addr.s_addr>>16)&0xff,(saddr.sin_addr.s_addr>>24)&0xff,Port,strerror(errno));
      else if(r>0) HEXDUMP(L_CORE_NETDATA,data,r,"network sendto %d.%d.%d.%d:%d",(saddr.sin_addr.s_addr>> 0)&0xff,(saddr.sin_addr.s_addr>> 8)&0xff,(saddr.sin_addr.s_addr>>16)&0xff,(saddr.sin_addr.s_addr>>24)&0xff,Port);
      }
    else r=-1;
    }
  Activity();
  return r;
}

int cNetSocket::Select(bool forRead, int timeout)
{
  if(sd>=0) {
    fd_set fds;
    FD_ZERO(&fds); FD_SET(sd,&fds);
    struct timeval tv;
    tv.tv_sec=timeout; tv.tv_usec=0;
    int r=select(sd+1,forRead ? &fds:0,forRead ? 0:&fds,0,&tv);
    if(r>0) return 1;
    else if(r<0) {
      PRINTF(L_GEN_ERROR,"socket: select failed: %s",strerror(errno));
      return -1;
      }
    else {
      if(timeout>0) PRINTF(L_CORE_NET,"socket: select timed out (%d secs)",timeout);
      return 0;
      }
    }
  return -1;
}
