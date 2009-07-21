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

#include <vdr/tools.h>

#include "filter.h"
#include "misc.h"
#include "log-core.h"

// -- cPidFilter ------------------------------------------------------------------

cPidFilter::cPidFilter(const char *Id, int Num, int DvbNum, unsigned int IdleTime)
{
  dvbNum=DvbNum;
  idleTime=IdleTime;
  id=0; fd=-1; active=false; forceRun=false; userData=0;
  asprintf(&id,"%s/%d",Id,Num);
  PRINTF(L_CORE_ACTION,"new filter '%s' on card %d (%d ms)",id,dvbNum,idleTime);
}

cPidFilter::~cPidFilter()
{
  cMutexLock lock(this);
  if(fd>=0) {
    Stop();
    close(fd);
    }
  PRINTF(L_CORE_ACTION,"filter '%s' on card %d removed",id,dvbNum);
  free(id);
}

bool cPidFilter::Open(void)
{
  cMutexLock lock(this);
  if(fd<0) {
    fd=DvbOpen(DEV_DVB_DEMUX,dvbNum,O_RDWR|O_NONBLOCK);
    if(fd>=0) return true;
    }
  return false;
}

void cPidFilter::Flush(void)
{
  cMutexLock lock(this);
  if(fd>=0) {
    unsigned char buff[MAX_SECT_SIZE];
    while(read(fd,buff,sizeof(buff))>0);
    }
}

void cPidFilter::Start(int Pid, int Section, int Mask, int Mode, bool Crc)
{
  cMutexLock lock(this);
  if(fd>=0) {
    Stop();
    struct dmx_sct_filter_params FilterParams;
    memset(&FilterParams,0,sizeof(FilterParams));
    FilterParams.filter.filter[0]=Section;
    FilterParams.filter.mask[0]=Mask;
    FilterParams.filter.mode[0]=Mode;
    FilterParams.flags=DMX_IMMEDIATE_START;
    if(Crc) FilterParams.flags|=DMX_CHECK_CRC;
    FilterParams.pid=Pid;
    if(ioctl(fd,DMX_SET_FILTER,&FilterParams)<0) {
      PRINTF(L_GEN_ERROR,"filter '%s': DMX_SET_FILTER failed: %s",id,strerror(errno));
      return;
      }
    pid=Pid;
    active=true;

    LBSTART(L_CORE_ACTION);
    LBPUT("filter '%s' -> pid=0x%04x sct=0x%02x/0x%02x/0x%02x crc=%d matching",id,Pid,Section,Mask,Mode,Crc);
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
  if(fd>=0) {
    CHECK(ioctl(fd,DMX_STOP));
    active=false;
    }
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
  return active ? pid : -1;
}

// -- cAction ------------------------------------------------------------------

cAction::cAction(const char *Id, int DvbNum)
{
  asprintf(&id,"%s %d",Id,DvbNum);
  dvbNum=DvbNum;
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

cPidFilter *cAction::CreateFilter(const char *Id, int Num, int DvbNum, int IdleTime)
{
  return new cPidFilter(Id,Num,DvbNum,IdleTime);
}

cPidFilter *cAction::NewFilter(int IdleTime)
{
  Lock();
  cPidFilter *filter=CreateFilter(id,unique++,dvbNum,IdleTime);
  if(filter && filter->Open()) {
    if(!Active()) {
      Start();
      PRINTF(L_CORE_ACTION,"%s: started",id);
      }
    filters.Add(filter);
    }
  else {
    PRINTF(L_CORE_ACTION,"%s: filter '%s' failed to open",id,filter?filter->id:"XxX");
    delete filter;
    filter=0;
    }
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
      delete pfd; pfd=new struct pollfd[filters.Count()];
      if(!pfd) {
        PRINTF(L_GEN_ERROR,"action %s: pollfd: out of memory",id);
        break;
        }
      int num=0;
      cPidFilter *filter;
      for(filter=filters.First(); filter; filter=filters.Next(filter)) {
        memset(&pfd[num],0,sizeof(struct pollfd));
        pfd[num].fd=filter->fd;
        pfd[num].events=POLLIN;
        num++;
        }
      Unlock();

      // now poll for data
      int r=poll(pfd,num,60);
      if(r<0) {
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
                  if(errno==EOVERFLOW) PRINTF(L_GEN_ERROR,"action %s read: Buffer overflow",filter->id);
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
  delete pfd;
}
