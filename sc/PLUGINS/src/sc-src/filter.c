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
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <unistd.h>

#include <linux/dvb/dmx.h>

#include <vdr/device.h>
#include <vdr/tools.h>

#include "filter.h"
#include "misc.h"
#include "log-core.h"

// -- cPidFilter ------------------------------------------------------------------

cPidFilter::cPidFilter(const char *Id, int Num, cDevice *Device, unsigned int IdleTime)
{
  device=Device;
  idleTime=IdleTime;
  id=0; fd=-1; forceRun=false; userData=0;
  id=bprintf("%s/%d",Id,Num);
  PRINTF(L_CORE_ACTION,"new filter '%s' (%d ms)",id,idleTime);
}

cPidFilter::~cPidFilter()
{
  cMutexLock lock(this);
  Stop();
  PRINTF(L_CORE_ACTION,"filter '%s' removed",id);
  free(id);
}

void cPidFilter::Flush(void)
{
  cMutexLock lock(this);
  if(fd>=0) {
    unsigned char buff[MAX_SECT_SIZE];
    while(read(fd,buff,sizeof(buff))>0);
    }
}

void cPidFilter::Start(int Pid, int Section, int Mask)
{
  cMutexLock lock(this);
  Stop();
  fd=device->OpenFilter(Pid,Section,Mask);
  if(fd>=0) {
    pid=Pid;

    LBSTART(L_CORE_ACTION);
    LBPUT("filter '%s' -> pid=0x%04x sct=0x%02x/0x%02x matching",id,Pid,Section,Mask);
    int Mode=0;
    int mam =Mask &  (~Mode);
    int manm=Mask & ~(~Mode);
    for(int i=0; i<256; i++) {
      int xxxor=Section ^ i;
      if((mam&xxxor) || (manm && !(manm&xxxor))) {}
      else
        LBPUT(" 0x%02x",i);
      }
    LBEND();
    }
}

void cPidFilter::Stop(void)
{
  cMutexLock lock(this);
  if(fd>=0) { device->CloseFilter(fd); fd=-1; }
}

void cPidFilter::SetBuffSize(int BuffSize)
{
  cMutexLock lock(this);
  if(fd>=0) {
    Stop();
    int s=max(BuffSize,8192);
    CHECK(ioctl(fd,DMX_SET_BUFFER_SIZE,s));
    }
}

int cPidFilter::SetIdleTime(unsigned int IdleTime)
{
  int i=idleTime;
  idleTime=IdleTime;
  return i;
}

void cPidFilter::Wakeup(void)
{
  cMutexLock lock(this);
  forceRun=true;
  PRINTF(L_CORE_ACTION,"filter '%s': wakeup",id);
}

int cPidFilter::Pid(void)
{
  return Active() ? pid : -1;
}

// -- cAction ------------------------------------------------------------------

cAction::cAction(const char *Id, cDevice *Device, const char *DevId)
{
  device=Device; devId=DevId;
  id=bprintf("%s %s",Id,DevId);
  unique=0; pri=-1;
  SetDescription("%s filter",id);
}

cAction::~cAction()
{
  Cancel(2);
  DelAllFilter();
  PRINTF(L_CORE_ACTION,"%s: stopped",id);
  free(id);
}

cPidFilter *cAction::CreateFilter(int Num, int IdleTime)
{
  return new cPidFilter(id,Num,device,IdleTime);
}

cPidFilter *cAction::NewFilter(int IdleTime)
{
  Lock();
  cPidFilter *filter=CreateFilter(unique++,IdleTime);
  if(filter) {
    if(!Active()) {
      Start();
      PRINTF(L_CORE_ACTION,"%s: started",id);
      }
    filters.Add(filter);
    }
  else
    PRINTF(L_CORE_ACTION,"%s: failed to create filter",id);
  Unlock();
  return filter;
}

cPidFilter *cAction::IdleFilter(void)
{
  Lock();
  cPidFilter *filter;
  for(filter=filters.First(); filter; filter=filters.Next(filter))
    if(!filter->Active()) break;
  Unlock();
  return filter;
}

void cAction::DelFilter(cPidFilter *filter)
{
  Lock();
  filters.Del(filter);
  Unlock();
}

void cAction::DelAllFilter(void)
{
  Lock();
  filters.Clear();
  unique=0;
  Unlock();
}

void cAction::Priority(int Pri)
{
  pri=Pri;
}

void cAction::Action(void)
{
  if(pri>0) SetPriority(pri);
  struct pollfd *pfd=0;
  while(Running()) {
    if(filters.Count()<=0) {
      cCondWait::SleepMs(100);
      }
    else {
      // first build pfd data
      Lock();
      delete[] pfd; pfd=new struct pollfd[filters.Count()];
      if(!pfd) {
        PRINTF(L_GEN_ERROR,"action %s: pollfd: out of memory",id);
        break;
        }
      int num=0;
      cPidFilter *filter;
      for(filter=filters.First(); filter; filter=filters.Next(filter))
        if(filter->Active()) {
          memset(&pfd[num],0,sizeof(struct pollfd));
          pfd[num].fd=filter->fd;
          pfd[num].events=POLLIN;
          num++;
          }
      Unlock();

      // now poll for data
      int r=poll(pfd,num,60);
      if(r<0 && errno!=EINTR) {
        PRINTF(L_GEN_ERROR,"action %s poll: %s",id,strerror(errno));
        break;
        }
      if(r>0) {
        for(r=0 ; r<num ; r++)
          if(pfd[r].revents&POLLIN) {
            Lock();
            for(filter=filters.First(); filter; filter=filters.Next(filter)) {
              if(filter->fd==pfd[r].fd) {
                unsigned char buff[MAX_SECT_SIZE];
                int n=read(filter->fd,buff,sizeof(buff));
                if(n<0 && errno!=EAGAIN) {
                  if(errno==EOVERFLOW) {
                    filter->Flush();
                    //PRINTF(L_GEN_ERROR,"action %s read: Buffer overflow",filter->id);
                    }
                  else PRINTF(L_GEN_ERROR,"action %s read: %s",filter->id,strerror(errno));
                  }
                if(n>0) {
                  filter->lastTime.Set(); filter->forceRun=false;
                  Process(filter,buff,n);
                  // don't make any assumption about data-structs here
                  // Process() may have changed them
                  }
                break;
                }
              }
            Unlock();
            }
        }

      // call filters which are idle too long
      Lock();
      do {
        r=0;
        for(filter=filters.First(); filter; filter=filters.Next(filter)) {
          if(filter->forceRun || (filter->idleTime && filter->lastTime.Elapsed()>filter->idleTime)) {
            filter->lastTime.Set(); filter->forceRun=false;
            Process(filter,0,0);
            // don't make any assumption about data-structs here
            // Process() may have changed them
            r=1; break;
            }
          }
        } while(r);
      Unlock();
      }
    }
  delete[] pfd;
}
