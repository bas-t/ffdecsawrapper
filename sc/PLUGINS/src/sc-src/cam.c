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

#include <vdr/device.h>
#include <vdr/channels.h>
#include <vdr/thread.h>
#ifndef SASC
#include <vdr/ringbuffer.h>
#endif

#include "cam.h"
#include "global.h"
#include "device.h"
#include "scsetup.h"
#include "filter.h"
#include "system.h"
#include "data.h"
#include "override.h"
#include "misc.h"
#include "log-core.h"
#ifndef SASC
#include "FFdecsa/FFdecsa.h"
#endif

#define IDLE_SLEEP          0 // idleTime when sleeping
#define IDLE_GETCA        200 // idleTime when waiting for ca descriptors
#define IDLE_GETCA_SLOW 20000 // idleTime if no enc. system
#define IDLE_NO_SYNC      800 // idleTime when not in sync
#define IDLE_SYNC        2000 // idleTime when in sync

#define CW_REPEAT_TIME   2000 // rewrite CW after X ms
#define LOG_COUNT           3 // stop logging after X complete ECM cycles
#define CHAIN_HOLD     120000 // min. time to hold a logger chain
#define ECM_DATA_TIME    6000 // time to wait for ECM data updates
#define MAX_ECM_IDLE   300000 // delay before an idle handler can be removed
#define MAX_ECM_HOLD    15000 // delay before an idle handler stops processing

#define ECMCACHE_FILE "ecm.cache"

#define L_HEX       2
#define L_HEX_ECM   LCLASS(L_HEX,2)
#define L_HEX_EMM   LCLASS(L_HEX,4)
#define L_HEX_CAT   LCLASS(L_HEX,8)
#define L_HEX_PMT   LCLASS(L_HEX,16)
#define L_HEX_HOOK  LCLASS(L_HEX,32)
#define L_HEX_ALL   LALL(L_HEX_HOOK)

static const struct LogModule lm_hex = {
  (LMOD_ENABLE|L_HEX_ALL)&LOPT_MASK,
  (LMOD_ENABLE)&LOPT_MASK,
  "hexdata",
  { "ecm","emm","cat","pmt","hook" }
  };
ADD_MODULE(L_HEX,lm_hex)

static const char *typeNames[] = { "typ0","typ1","VIDEO","typ3","AUDIO","typ5","DOLBY","typ6+" };
#define TYPENAME(type) (typeNames[(type)<=7?(type):7])

// -- cLogStats ---------------------------------------------------------------

#define COUNTS  20
#define SAMPLE (30*1000)
#define AVR1   (60*1000)
#define AVR2   (4*60*1000)
#define AVR3   (10*60*1000)
#define REPORT (60*1000)

class cLogStats : public cThread {
private:
  cTimeMs sTime, repTime;
  int sCount, sIdx, sCounts[COUNTS][2];
protected:
  virtual void Action(void);
public:
  cLogStats(void);
  ~cLogStats();
  void Count(void);
  };

static cMutex logstatsMutex;
static cLogStats *logstats=0;

void LogStatsUp(void)
{
  logstatsMutex.Lock();
  if(LOG(L_CORE_AUSTATS) && !logstats) logstats=new cLogStats;
  logstatsMutex.Unlock();
}

void LogStatsDown(void)
{
  logstatsMutex.Lock();
  if(logstats) { delete logstats; logstats=0; }
  logstatsMutex.Unlock();
}

cLogStats::cLogStats(void)
{
  sCount=sIdx=0;
  for(int i=0; i<COUNTS; i++) { sCounts[i][0]=0; sCounts[i][1]=SAMPLE; }
  SetDescription("logger stats");
  Start();
}

cLogStats::~cLogStats()
{
  Cancel(2);
}

void cLogStats::Count(void)
{
  sCount++;
}

void cLogStats::Action(void)
{
  while(Running()) {
    cCondWait::SleepMs(50);
    if(sTime.Elapsed()>SAMPLE) {
      sCounts[sIdx][0]=sCount;          sCount=0;
      sCounts[sIdx][1]=sTime.Elapsed(); sTime.Set();
      if(++sIdx >= COUNTS) sIdx=0;
      }
    if(repTime.Elapsed()>REPORT) {
      repTime.Set();
      if(sCounts[(sIdx+COUNTS-1)%COUNTS][0]>0) {
        LBSTART(L_CORE_AUSTATS);
        LBPUT("EMM packet load average (%d/%d/%dmin)",AVR1/60000,AVR2/60000,AVR3/60000);
        int s=0, t=0;
        for(int i=1; i<=COUNTS; i++) {
          s+=sCounts[(sIdx+COUNTS-i)%COUNTS][0];
          t+=sCounts[(sIdx+COUNTS-i)%COUNTS][1];
          if(i==(AVR1/SAMPLE) || i==(AVR2/SAMPLE) || i==(AVR3/SAMPLE))
            LBPUT(" %4d",(int)((float)s/(float)t*1000.0));
          }
        LBPUT(" pks/s");
        LBEND();
        }
      }
    }
}

// -- cHookManager -------------------------------------------------------------

class cHookManager : public cAction {
  const char *devId;
  cSimpleList<cLogHook> hooks;
  //
  cPidFilter *AddFilter(int Pid, int Section, int Mask, int IdleTime);
  void ClearHooks(void);
  void DelHook(cLogHook *hook);
protected:
  virtual void Process(cPidFilter *filter, unsigned char *data, int len);
public:
  cHookManager(cDevice *Device, const char *DevId);
  virtual ~cHookManager();
  void AddHook(cLogHook *hook);
  bool TriggerHook(int id);
  void Down(void);
  };

cHookManager::cHookManager(cDevice *Device, const char *DevId)
:cAction("hookmanager",Device,DevId)
{
  devId=DevId;
  Priority(10);
}

cHookManager::~cHookManager()
{
  Down();
}

void cHookManager::Down(void)
{
  Lock();
  while(cLogHook *hook=hooks.First()) DelHook(hook);
  DelAllFilter();
  Unlock();
}

bool cHookManager::TriggerHook(int id)
{
  Lock();
  for(cLogHook *hook=hooks.First(); hook; hook=hooks.Next(hook))
    if(hook->id==id) {
      hook->delay.Set(CHAIN_HOLD);
      Unlock();
      return true;
      }
  Unlock();
  return false;
}

void cHookManager::AddHook(cLogHook *hook)
{
  Lock();
  PRINTF(L_CORE_HOOK,"%s: starting hook '%s' (%04x)",devId,hook->name,hook->id);
  hook->delay.Set(CHAIN_HOLD);
  hooks.Add(hook);
  for(cPid *pid=hook->pids.First(); pid; pid=hook->pids.Next(pid)) {
    cPidFilter *filter=AddFilter(pid->pid,pid->sct,pid->mask,CHAIN_HOLD/8);
    if(filter) {
      filter->userData=(void *)hook;
      pid->filter=filter;
      }
    }
  Unlock();
}

void cHookManager::DelHook(cLogHook *hook)
{
  PRINTF(L_CORE_HOOK,"%s: stopping hook '%s' (%04x)",devId,hook->name,hook->id);
  for(cPid *pid=hook->pids.First(); pid; pid=hook->pids.Next(pid)) {
    cPidFilter *filter=pid->filter;
    if(filter) {
      DelFilter(filter);
      pid->filter=0;
      }
    }
  hooks.Del(hook);
}

cPidFilter *cHookManager::AddFilter(int Pid, int Section, int Mask, int IdleTime)
{
  cPidFilter *filter=NewFilter(IdleTime);
  if(filter) {
    filter->SetBuffSize(32768);
    filter->Start(Pid,Section,Mask);
    PRINTF(L_CORE_HOOK,"%s: added filter pid=0x%.4x sct=0x%.2x/0x%.2x idle=%d",devId,Pid,Section,Mask,IdleTime);
    }
  else PRINTF(L_GEN_ERROR,"no free slot or filter failed to open for hookmanager %s",devId);
  return filter;
}

void cHookManager::Process(cPidFilter *filter, unsigned char *data, int len)
{
  if(data && len>0) {
    HEXDUMP(L_HEX_HOOK,data,len,"HOOK pid 0x%04x",filter->Pid());
    if(SCT_LEN(data)==len) {
      cLogHook *hook=(cLogHook *)(filter->userData);
      if(hook) {
        hook->Process(filter->Pid(),data);
        if(hook->bailOut || hook->delay.TimedOut()) DelHook(hook);
        }
      }
    else PRINTF(L_CORE_HOOK,"%s: incomplete section %d != %d",devId,len,SCT_LEN(data));
    }
  else {
    cLogHook *hook=(cLogHook *)(filter->userData);
    if(hook && (hook->bailOut || hook->delay.TimedOut())) DelHook(hook);
    }
}

// -- cLogChain ----------------------------------------------------------------

class cLogChain : public cSimpleItem {
public:
  cCam *cam;
  const char *devId;
  int caid, source, transponder;
  bool softCSA, active, delayed;
  cTimeMs delay;
  cPids pids;
  cSimpleList<cSystem> systems;
  //
  cLogChain(cCam *Cam, const char *DevId, bool soft, int src, int tr);
  void Process(int pid, const unsigned char *data);
  bool Parse(const unsigned char *cat);
  };

cLogChain::cLogChain(cCam *Cam, const char *DevId, bool soft, int src, int tr)
{
  cam=Cam; devId=DevId; softCSA=soft; source=src; transponder=tr;
  active=delayed=false;
}

void cLogChain::Process(int pid, const unsigned char *data)
{
  if(active) {
    for(cSystem *sys=systems.First(); sys; sys=systems.Next(sys))
      sys->ProcessEMM(pid,caid,data);
    }
}

bool cLogChain::Parse(const unsigned char *cat)
{
  if(cat[0]==0x09) {
    caid=WORD(cat,2,0xFFFF);
    LBSTARTF(L_CORE_AU);
    LBPUT("%s: chain caid %04x",devId,caid);
    cSystem *sys;
    if(systems.Count()>0) {
      LBPUT(" ++");
      for(sys=systems.First(); sys; sys=systems.Next(sys))
        sys->ParseCAT(&pids,cat,source,transponder);
      }
    else {
      LBPUT(" ->");
      if(!overrides.Ignore(source,transponder,caid)) {
        int Pri=0;
        while((sys=cSystems::FindBySysId(caid,!softCSA,Pri))) {
          Pri=sys->Pri();
          if(sys->HasLogger()) {
            sys->SetOwner(cam);
            sys->ParseCAT(&pids,cat,source,transponder);
            systems.Add(sys);
            LBPUT(" %s(%d)",sys->Name(),sys->Pri());
            }
          else
            delete sys;
          }
        }
      }
    if(systems.Count()==0) LBPUT(" none available");
    for(cPid *pid=pids.First(); pid; pid=pids.Next(pid))
       LBPUT(" [%04x-%02x/%02x]",pid->pid,pid->sct,pid->mask);
    LBEND();
    if(systems.Count()>0 && pids.Count()>0)
      return true;
    }
  return false;
}

// -- cLogger ------------------------------------------------------------------

class cLogger : public cAction {
private:
  cCam *cam;
  const char *devId;
  bool softCSA, up;
  cSimpleList<cLogChain> chains;
  cSimpleList<cEcmInfo> active;
  //
  cPidFilter *catfilt;
  int catVers;
  int source, transponder;
  //
  enum ePreMode { pmNone, pmStart, pmWait, pmActive, pmStop };
  ePreMode prescan;
  cTimeMs pretime;
  //
  cPidFilter *AddFilter(int Pid, int Section, int Mask, int IdleTime);
  void SetChains(void);
  void ClearChains(void);
  void StartChain(cLogChain *chain);
  void StopChain(cLogChain *chain, bool force);
  void ProcessCat(unsigned char *data, int len);
protected:
  virtual void Process(cPidFilter *filter, unsigned char *data, int len);
public:
  cLogger(cCam *Cam, cDevice *Device, const char *DevId, bool soft);
  virtual ~cLogger();
  void EcmStatus(const cEcmInfo *ecm, bool on);
  void Up(void);
  void Down(void);
  void PreScan(int src, int tr);
  };

cLogger::cLogger(cCam *Cam, cDevice *Device, const char *DevId, bool soft)
:cAction("logger",Device,DevId)
{
  cam=Cam; devId=DevId; softCSA=soft;
  catfilt=0; up=false; prescan=pmNone;
  Priority(10);
}

cLogger::~cLogger()
{
  Down();
}

void cLogger::Up(void)
{
  Lock();
  if(!up) {
    PRINTF(L_CORE_AUEXTRA,"%s: UP",devId);
    catVers=-1;
    catfilt=AddFilter(1,0x01,0xFF,0);
    up=true;
    }
  Unlock();
}

void cLogger::Down(void)
{
  Lock();
  if(up) {
    PRINTF(L_CORE_AUEXTRA,"%s: DOWN",devId);
    ClearChains();
    DelAllFilter();
    catfilt=0; up=false; prescan=pmNone;
    }
  Unlock();
}

void cLogger::PreScan(int src, int tr)
{
  Lock();
  source=src; transponder=tr;
  prescan=pmStart; Up();
  Unlock();
}

void cLogger::EcmStatus(const cEcmInfo *ecm, bool on)
{
  Lock();
  PRINTF(L_CORE_AUEXTRA,"%s: ecm prgid=%d caid=%04x prov=%.4x %s",devId,ecm->prgId,ecm->caId,ecm->provId,on ? "active":"inactive");
  source=ecm->source; transponder=ecm->transponder;
  cEcmInfo *e;
  if(on) {
    e=new cEcmInfo(ecm);
    active.Add(e);
    if(!up) Up();
    }
  else {
    for(e=active.First(); e; e=active.Next(e))
      if(e->Compare(ecm)) {
        active.Del(e);
        break;
        }
    }
  if(prescan>=pmWait) prescan=pmStop;
  SetChains();
  prescan=pmNone;
  Unlock();
}

void cLogger::SetChains(void)
{
  for(cLogChain *chain=chains.First(); chain; chain=chains.Next(chain)) {
    bool act=false;
    if(ScSetup.AutoUpdate>1 || prescan==pmActive) act=true;
    else if(ScSetup.AutoUpdate==1) {
      for(cEcmInfo *e=active.First(); e; e=active.Next(e))
        if((e->emmCaId && chain->caid==e->emmCaId) || chain->caid==e->caId) {
          act=true; break;
          }
       }
    if(act) StartChain(chain);
    else StopChain(chain,prescan==pmStop);
    }
}

void cLogger::ClearChains(void)
{
  for(cLogChain *chain=chains.First(); chain; chain=chains.Next(chain))
    StopChain(chain,true);
  chains.Clear();
}

void cLogger::StartChain(cLogChain *chain)
{
  if(chain->delayed)
    PRINTF(L_CORE_AUEXTRA,"%s: restarting delayed chain %04x",devId,chain->caid);
  chain->delayed=false;
  if(!chain->active) {
    PRINTF(L_CORE_AU,"%s: starting chain %04x",devId,chain->caid);
    chain->active=true;
    for(cPid *pid=chain->pids.First(); pid; pid=chain->pids.Next(pid)) {
      cPidFilter *filter=AddFilter(pid->pid,pid->sct,pid->mask,CHAIN_HOLD/8);
      if(filter) {
        filter->userData=(void *)chain;
        pid->filter=filter;
        }
      }
    }
}

void cLogger::StopChain(cLogChain *chain, bool force)
{
  if(chain->active) {
    if(force || (chain->delayed && chain->delay.TimedOut())) {
      PRINTF(L_CORE_AU,"%s: stopping chain %04x",devId,chain->caid);
      chain->active=false;
      for(cPid *pid=chain->pids.First(); pid; pid=chain->pids.Next(pid)) {
        cPidFilter *filter=pid->filter;
        if(filter) {
          DelFilter(filter);
          pid->filter=0;
          }
        }
      }
    else if(!chain->delayed) {
      PRINTF(L_CORE_AUEXTRA,"%s: delaying chain %04x",devId,chain->caid);
      chain->delayed=true;
      chain->delay.Set(CHAIN_HOLD);
      }
    }
}

cPidFilter *cLogger::AddFilter(int Pid, int Section, int Mask, int IdleTime)
{
  cPidFilter *filter=NewFilter(IdleTime);
  if(filter) {
    if(Pid>1) filter->SetBuffSize(KILOBYTE(64));
    filter->Start(Pid,Section,Mask);
    PRINTF(L_CORE_AUEXTRA,"%s: added filter pid=0x%.4x sct=0x%.2x/0x%.2x idle=%d",devId,Pid,Section,Mask,IdleTime);
    }
  else PRINTF(L_GEN_ERROR,"no free slot or filter failed to open for logger %s",devId);
  return filter;
}

void cLogger::ProcessCat(unsigned char *data, int len)
{
  for(int i=0; i<len; i+=data[i+1]+2) {
    if(data[i]==0x09) {
      int caid=WORD(data,i+2,0xFFFF);
      cLogChain *chain;
      for(chain=chains.First(); chain; chain=chains.Next(chain))
        if(chain->caid==caid) break;
      if(chain)
        chain->Parse(&data[i]);
      else {
        chain=new cLogChain(cam,devId,softCSA,source,transponder);
        if(chain->Parse(&data[i]))
          chains.Add(chain);
        else
          delete chain;
        }
      }
    }
}

void cLogger::Process(cPidFilter *filter, unsigned char *data, int len)
{
  if(data && len>0) {
    if(filter==catfilt) {
      int vers=(data[5]&0x3E)>>1;
      if(data[0]==0x01 && vers!=catVers) {
        PRINTF(L_CORE_AUEXTRA,"%s: got CAT version %02x",devId,vers);
        catVers=vers;
        HEXDUMP(L_HEX_CAT,data,len,"CAT vers %02x",catVers);
        ClearChains();
        ProcessCat(&data[8],len-4-8);
        unsigned char buff[2048];
        if((len=overrides.GetCat(source,transponder,buff,sizeof(buff)))>0) {
          HEXDUMP(L_HEX_CAT,buff,len,"override CAT");
          ProcessCat(buff,len);
          }
        SetChains();
        if(prescan==pmStart) { prescan=pmWait; pretime.Set(2000); }
        }
      if(prescan==pmWait && pretime.TimedOut()) { prescan=pmActive; SetChains(); }
      }
    else {
      HEXDUMP(L_HEX_EMM,data,len,"EMM pid 0x%04x",filter->Pid());
      if(logstats) logstats->Count();
      if(SCT_LEN(data)==len) {
        cLogChain *chain=(cLogChain *)(filter->userData);
        if(chain) {
          chain->Process(filter->Pid(),data);
          if(chain->delayed) StopChain(chain,false);
          }
        }
      else PRINTF(L_CORE_AU,"%s: incomplete section %d != %d",devId,len,SCT_LEN(data));
      }
    }
  else {
    cLogChain *chain=(cLogChain *)(filter->userData);
    if(chain && chain->delayed) StopChain(chain,false);
    }
}

// -----------------------------------------------------------------------------

#if APIVERSNUM >= 10713

cString OldSourceToString(int Code)
{
  enum eSourceType {
    stNone  = 0x0000,
    stCable = 0x4000,
    stSat   = 0x8000,
    stTerr  = 0xC000,
    st_Mask = 0xC000,
    st_Neg  = 0x0800,
    st_Pos  = 0x07FF,
    };
  char buffer[16];
  char *q = buffer;
  switch (Code & st_Mask) {
    case stCable: *q++ = 'C'; break;
    case stSat:   *q++ = 'S';
                  {
                    int pos = Code & ~st_Mask;
                    q += snprintf(q, sizeof(buffer) - 2, "%u.%u", (pos & ~st_Neg) / 10, (pos & ~st_Neg) % 10); // can't simply use "%g" here since the silly 'locale' messes up the decimal point
                    *q++ = (Code & st_Neg) ? 'E' : 'W';
                  }
                  break;
    case stTerr:  *q++ = 'T'; break;
    default:      *q++ = Code + '0'; // backward compatibility
    }
  *q = 0;
  return buffer;
}

#else // APIVERSNUM >= 10713

cString NewSourceToString(int Code)
{
  enum eSourceType {
    stNone  = 0x00000000,
    stAtsc  = ('A' << 24),
    stCable = ('C' << 24),
    stSat   = ('S' << 24),
    stTerr  = ('T' << 24),
    st_Mask = 0xFF000000,
    st_Pos  = 0x0000FFFF,
    };
  char buffer[16];
  char *q = buffer;
  *q++ = (Code & st_Mask) >> 24;
  int n = (Code & st_Pos);
  if (n > 0x00007FFF)
     n |= 0xFFFF0000;
  if (n) {
     q += snprintf(q, sizeof(buffer) - 2, "%u.%u", abs(n) / 10, abs(n) % 10); // can't simply use "%g" here since the silly 'locale' messes up the decimal point
     *q++ = (n < 0) ? 'E' : 'W';
     }
  *q = 0;
  return buffer;
}

#endif // APIVERSNUM >= 10713

// -- cEcmData -----------------------------------------------------------------

#define CACHE_VERS 2

class cEcmData : public cEcmInfo {
public:
  cEcmData(void):cEcmInfo() {}
  cEcmData(cEcmInfo *e):cEcmInfo(e) {}
  virtual cString ToString(bool hide);
  bool Parse(const char *buf);
  };

bool cEcmData::Parse(const char *buf)
{
  char Name[64], Source[64];
  int nu=0, num, vers=0;
  Name[0]=0;
  if(sscanf(buf,"V%d:%d:%63[^:]:%x:%63[^:]:%x/%x:%x:%x/%x:%d:%d/%d%n",
             &vers,&grPrgId,Source,&transponder,Name,&caId,&emmCaId,&provId,
             &ecm_pid,&ecm_table,&rewriterId,&nu,&dataIdx,&num)>=13) {
    if(vers==CACHE_VERS) {
      source=cSource::FromString(Source);
      }
    else if(vers==1) {
      source=strtoul(Source,0,16);
#if APIVERSNUM >= 10713
      // check for old style source code
      if(source<0x10000) source=cSource::FromString(OldSourceToString(source));
#else
      // check for new style source code
      if(source>=0x10000) source=cSource::FromString(NewSourceToString(source));
#endif
      }
    else return false;
   
    SetName(Name);
    SetRewriter();
    prgId=grPrgId%SIDGRP_SHIFT;
    const char *line=buf+num;
    if(nu>0 && *line++==':') {
      unsigned char *dat=AUTOMEM(nu);
      if(GetHex(line,dat,nu,true)==nu && dat[0]==0x09 && dat[1]==nu-2)
        AddCaDescr(dat,nu);
      }
    return true;
    }
  return false;
}

cString cEcmData::ToString(bool hide)
{
  char *str;
  if(caDescr) {
    str=AUTOARRAY(char,caDescrLen*2+16);
    int q=sprintf(str,"%d/%d:",caDescrLen,dataIdx);
    HexStr(str+q,caDescr,caDescrLen);
    }
  else {
    str=AUTOARRAY(char,10);
    sprintf(str,"0/%d:",dataIdx);
    }
  return cString::sprintf("V%d:%d:%s:%x:%s:%x/%x:%x:%x/%x:%d:%s",
                            CACHE_VERS,grPrgId,*cSource::ToString(source),transponder,name,
                            caId,emmCaId,provId,ecm_pid,ecm_table,rewriterId,
                            str);
}

// -- cEcmCache ----------------------------------------------------------------

cEcmCache ecmcache;

cEcmCache::cEcmCache(void)
:cStructListPlain<cEcmData>("ecm cache",ECMCACHE_FILE,SL_READWRITE|SL_MISSINGOK)
{}

void cEcmCache::New(cEcmInfo *e)
{
  if(ScSetup.EcmCache>0) return;
  ListLock(true);
  cEcmData *dat;
  if(!(dat=Exists(e))) {
    dat=new cEcmData(e);
    Add(dat);
    Modified();
    PRINTF(L_CORE_ECM,"cache add prgId=%d source=%x transponder=%x ecm=%x/%x",e->grPrgId,e->source,e->transponder,e->ecm_pid,e->ecm_table);
    }
  else {
    if(strcasecmp(e->name,dat->name)) {
      dat->SetName(e->name);
      Modified();
      }
    if(dat->AddCaDescr(e))
      Modified();
    }
  ListUnlock();
  e->SetCached();
}

cEcmData *cEcmCache::Exists(cEcmInfo *e)
{
  cEcmData *dat;
  for(dat=First(); dat; dat=Next(dat))
    if(dat->Compare(e)) break;
  return dat;
}

int cEcmCache::GetCached(cSimpleList<cEcmInfo> *list, int sid, int Source, int Transponder)
{
  int n=0;
  list->Clear();
  if(ScSetup.EcmCache>1) return 0;
  ListLock(false);
  for(cEcmData *dat=First(); dat; dat=Next(dat)) {
    if(dat->grPrgId==sid && dat->source==Source && dat->transponder==Transponder) {
      cEcmInfo *e=new cEcmInfo(dat);
      if(e) {
        PRINTF(L_CORE_ECM,"from cache: system %s (%04x) id %04x with ecm %x/%x",e->name,e->caId,e->provId,e->ecm_pid,e->ecm_table);
        e->SetCached();
        list->Add(e);
        n++;
        }
      }
    }
  ListUnlock();
  return n;
}

void cEcmCache::Delete(cEcmInfo *e)
{
  if(ScSetup.EcmCache>0) return;
  ListLock(false);
  cEcmData *dat=Exists(e);
  ListUnlock();
  if(dat) {
    DelItem(dat);
    PRINTF(L_CORE_ECM,"invalidated cached prgId=%d source=%x transponder=%x ecm=%x/%x",dat->grPrgId,dat->source,dat->transponder,dat->ecm_pid,dat->ecm_table);
    }
}

void cEcmCache::Flush(void)
{
  ListLock(true);
  Clear();
  Modified();
  PRINTF(L_CORE_ECM,"cache flushed");
  ListUnlock();
}

bool cEcmCache::ParseLinePlain(const char *line)
{
  cEcmData *dat=new cEcmData;
  if(dat && dat->Parse(line) && !Exists(dat)) Add(dat);
  else delete dat;
  return true;
}

// -- cCaDescr -----------------------------------------------------------------

cCaDescr::cCaDescr(void)
{
  descr=0; len=0;
}

cCaDescr::cCaDescr(const cCaDescr &cd)
{
  descr=0; len=0;
  Set(cd.descr,cd.len);
}

cCaDescr::~cCaDescr()
{
  Clear();
}

const unsigned char *cCaDescr::Get(int &l) const
{
  l=len;
  return descr;
}

void cCaDescr::Set(const cCaDescr *d)
{
  Set(d->descr,d->len);
}

void cCaDescr::Set(const unsigned char *de, int l)
{
  Clear();
  if(l) {
    descr=MALLOC(unsigned char,l);
    if(descr) {
      memcpy(descr,de,l);
      len=l;
      }
    }
}

void cCaDescr::Clear(void)
{
  free(descr); descr=0; len=0;
}

void cCaDescr::Join(const cCaDescr *cd, bool rev)
{
  if(cd->descr) {
    int l=len+cd->len;
    unsigned char *m=MALLOC(unsigned char,l);
    if(m) {
      if(!rev) {
        if(descr) memcpy(m,descr,len);
        memcpy(m+len,cd->descr,cd->len);
        }
      else {
        memcpy(m,cd->descr,cd->len);
        if(descr) memcpy(m+cd->len,descr,len);
        }
      Clear();
      descr=m; len=l;
      }
    }
}

bool cCaDescr::operator== (const cCaDescr &cd) const
{
  return len==cd.len && (len==0 || memcmp(descr,cd.descr,len)==0);
}

cString cCaDescr::ToString(void)
{
  if(!descr) return "<empty>";
  char *str=AUTOARRAY(char,len*3+8);
  int q=sprintf(str,"%02X",descr[0]);
  for(int i=1; i<len; i++) q+=sprintf(str+q," %02X",descr[i]);
  return str;
}

// -- cPrg ---------------------------------------------------------------------

cPrg::cPrg(void)
{
  Setup();
}

cPrg::cPrg(int Sid, bool IsUpdate)
{
  Setup();
  sid=Sid; isUpdate=IsUpdate;
}

void cPrg::Setup(void)
{
  sid=-1; source=-1; transponder=-1;
  isUpdate=pidCaDescr=false;
}

void cPrg::DumpCaDescr(int c)
{
  PRINTF(c,"prgca: %s",*caDescr.ToString());
  for(cPrgPid *pid=pids.First(); pid; pid=pids.Next(pid))
    PRINTF(c,"pidca %04x: %s",pid->pid,*pid->caDescr.ToString());
}

bool cPrg::SimplifyCaDescr(void)
{
  if(HasPidCaDescr()) {
    bool equal=true;
    if(pids.Count()>1) {
      cPrgPid *pid0=pids.First();
      for(cPrgPid *pid1=pids.Next(pid0); pid1; pid1=pids.Next(pid1))
        if(!(pid0->caDescr==pid1->caDescr)) { equal=false; break; }
      }
    if(equal) {
      cPrgPid *pid=pids.First();
      caDescr.Join(&pid->caDescr);
      for(; pid; pid=pids.Next(pid)) pid->caDescr.Clear();
      SetPidCaDescr(false);
      }
    }
  if(HasPidCaDescr()) {
    for(cPrgPid *pid=pids.First(); pid; pid=pids.Next(pid))
      pid->caDescr.Join(&caDescr,true);
    caDescr.Clear();
    }

  PRINTF(L_CORE_PIDS,"Ca descriptors after simplify (pidCa=%d)",HasPidCaDescr());
  DumpCaDescr(L_CORE_PIDS);

  return HasPidCaDescr();
}

// -- cEcmPri ------------------------------------------------------------------

class cEcmPri : public cSimpleItem {
public:
  cEcmInfo *ecm;
  int pri, sysIdent;
  };

// -- cEcmHandler --------------------------------------------------------------

class cEcmHandler : public cSimpleItem, public cAction {
private:
  const char *devId;
  int cwIndex;
  cCam *cam;
  char *id;
  cTimeMs idleTime;
  //
  cMutex dataMutex;
  cPrg prg;
  //
  cSystem *sys;
  cPidFilter *filter;
  int filterCwIndex, filterSource, filterTransponder, filterSid;
  cCaDescr filterCaDescr;
  unsigned char lastCw[16];
  bool sync, noKey, trigger, ecmUpd;
  int triggerMode;
  int mode, count;
  cTimeMs lastsync, startecm, resendTime;
  unsigned int cryptPeriod;
  unsigned char parity;
  cMsgCache failed;
  //
  cSimpleList<cEcmInfo> ecmList;
  cSimpleList<cEcmPri> ecmPriList;
  cEcmInfo *ecm;
  cEcmPri *ecmPri;
  //
  int dolog;
  //
  void DeleteSys(void);
  void NoSync(bool clearParity);
  cEcmInfo *NewEcm(void);
  cEcmInfo *JumpEcm(void);
  void StopEcm(void);
  bool UpdateEcm(void);
  void EcmOk(void);
  void EcmFail(void);
  void ParseCAInfo(int sys);
  void AddEcmPri(cEcmInfo *n);
protected:
  virtual void Process(cPidFilter *filter, unsigned char *data, int len);
public:
  cEcmHandler(cCam *Cam, cDevice *Device, const char *DevId, int cwindex);
  virtual ~cEcmHandler();
  void Stop(void);
  void SetPrg(cPrg *Prg);
  void ShiftCwIndex(int cwindex);
  char *CurrentKeyStr(void) const;
  bool IsRemoveable(void);
  bool IsIdle(void);
  int Sid(void) const { return prg.sid; }
  int CwIndex(void) const { return cwIndex; }
  const char *Id(void) const { return id; }
  };

cEcmHandler::cEcmHandler(cCam *Cam, cDevice *Device, const char *DevId, int cwindex)
:cAction("ecmhandler",Device,DevId)
,failed(32,0)
{
  cam=Cam;
  devId=DevId;
  cwIndex=cwindex;
  sys=0; filter=0; ecm=0; ecmPri=0; mode=-1;
  trigger=ecmUpd=false; triggerMode=-1;
  filterSource=filterTransponder=0; filterCwIndex=-1; filterSid=-1;
  id=bprintf("%s.%d",devId,cwindex);
}

cEcmHandler::~cEcmHandler()
{
  Lock();
  StopEcm();
  DelAllFilter(); // delete filters before sys for multi-threading reasons
  DeleteSys();
  Unlock();
  free(id);
}

bool cEcmHandler::IsIdle(void)
{
  dataMutex.Lock();
  int n=prg.pids.Count();
  dataMutex.Unlock();
  return n==0;
}

bool cEcmHandler::IsRemoveable(void)
{
  return IsIdle() && idleTime.Elapsed()>MAX_ECM_IDLE;
}

void cEcmHandler::Stop(void)
{
  dataMutex.Lock();
  if(!IsIdle() || prg.sid!=-1) {
    PRINTF(L_CORE_ECM,"%s: stop",id);
    prg.sid=-1;
    idleTime.Set();
    prg.pids.Clear();
    prg.caDescr.Clear();
    trigger=true;
    }
  dataMutex.Unlock();
  if(filter) filter->Wakeup();
}

void cEcmHandler::ShiftCwIndex(int cwindex)
{
  if(cwIndex!=cwindex) {
    PRINTF(L_CORE_PIDS,"%s: shifting cwIndex from %d to %d",id,cwIndex,cwindex);
    free(id);
    id=bprintf("%s.%d",devId,cwindex);
    dataMutex.Lock();
    trigger=true;
    cwIndex=cwindex;
    for(cPrgPid *pid=prg.pids.First(); pid; pid=prg.pids.Next(pid))
      cam->SetCWIndex(pid->pid,cwIndex);
    dataMutex.Unlock();
    if(filter) filter->Wakeup();
    }
}

void cEcmHandler::SetPrg(cPrg *Prg)
{
  dataMutex.Lock();
  bool wasIdle=IsIdle();
  if(Prg->sid!=prg.sid) {
    PRINTF(L_CORE_ECM,"%s: setting new SID %d",id,Prg->sid);
    prg.sid=Prg->sid;
    prg.source=Prg->source;
    prg.transponder=Prg->transponder;
    idleTime.Set();
    prg.pids.Clear();
    trigger=true;
    }
  if(Prg->HasPidCaDescr())
    PRINTF(L_GEN_DEBUG,"internal: pid specific caDescr not supported at this point (sid=%d)",Prg->sid);
  LBSTART(L_CORE_PIDS);
  LBPUT("%s: pids on entry",id);
  for(cPrgPid *pid=prg.pids.First(); pid; pid=prg.pids.Next(pid))
    LBPUT(" %s=%04x",TYPENAME(pid->type),pid->pid);
  LBEND();

  for(cPrgPid *pid=prg.pids.First(); pid;) {
    cPrgPid *npid;
    for(npid=Prg->pids.First(); npid; npid=Prg->pids.Next(npid)) {
      if(pid->pid==npid->pid) {
        npid->Proc(true);
        break;
        }
      }
    if(!npid) {
      npid=prg.pids.Next(pid);
      prg.pids.Del(pid);
      pid=npid;
      }
    else pid=prg.pids.Next(pid);
    }
  LBSTART(L_CORE_PIDS);
  LBPUT("%s: pids after delete",id);
  for(cPrgPid *pid=prg.pids.First(); pid; pid=prg.pids.Next(pid))
    LBPUT(" %s=%04x",TYPENAME(pid->type),pid->pid);
  LBEND();
  for(cPrgPid *npid=Prg->pids.First(); npid; npid=Prg->pids.Next(npid)) {
    if(!npid->Proc()) {
      cPrgPid *pid=new cPrgPid(npid->type,npid->pid);
      prg.pids.Add(pid);
      cam->SetCWIndex(pid->pid,cwIndex);
      }
    }
  LBSTART(L_CORE_PIDS);
  LBPUT("%s: pids after add",id);
  for(cPrgPid *pid=prg.pids.First(); pid; pid=prg.pids.Next(pid))
    LBPUT(" %s=%04x",TYPENAME(pid->type),pid->pid);
  LBEND();
  if(!IsIdle()) {
    if(!(prg.caDescr==Prg->caDescr)) prg.caDescr.Set(&Prg->caDescr);
    trigger=true;
    triggerMode=0;
    if(wasIdle) PRINTF(L_CORE_ECM,"%s: is no longer idle",id);
    }
  else {
    if(!wasIdle) idleTime.Set();
    PRINTF(L_CORE_ECM,"%s: is idle%s",id,wasIdle?"":" now");
    }

  if(!filter) {
    filter=NewFilter(IDLE_SLEEP);
    if(!filter) PRINTF(L_GEN_ERROR,"failed to open ECM filter in handler %s",id);
    }
  dataMutex.Unlock();
  if(filter) filter->Wakeup();
}

void cEcmHandler::Process(cPidFilter *filter, unsigned char *data, int len)
{
  dataMutex.Lock();
  if(trigger) {
    PRINTF(L_CORE_ECM,"%s: triggered SID %d/%d idx %d/%d mode %d/%d %s",
      id,filterSid,prg.sid,filterCwIndex,cwIndex,mode,triggerMode,(mode==3 && sync)?"sync":"-");
    trigger=false;
    if(filterSid!=prg.sid) {
      filterSid=prg.sid;
      filterSource=prg.source;
      filterTransponder=prg.transponder;
      filterCwIndex=cwIndex;
      noKey=true; mode=0;
      }
    else {
      if(filterCwIndex!=cwIndex) {
        filterCwIndex=cwIndex;
        if(mode==3 && sync)
          cam->WriteCW(filterCwIndex,lastCw,true);
        }
      if(mode<triggerMode) mode=triggerMode;
      }
    if(!(prg.caDescr==filterCaDescr)) {
      filterCaDescr.Set(&prg.caDescr);
      ecmUpd=true;
//XXX
PRINTF(L_CORE_ECM,"%s: new caDescr: %s",id,*filterCaDescr.ToString());
      }
    triggerMode=-1;
    }
  dataMutex.Unlock();

  switch(mode) {
    case -1:
      filter->SetIdleTime(IDLE_SLEEP);
      break;

    case 0:
      StopEcm();
      if(filterSid<0 || IsIdle()) { mode=-1; break; }

      dolog=LOG_COUNT;
      NewEcm();
      filter->SetIdleTime(IDLE_GETCA);
      startecm.Set();
      mode=1;
      break;

    case 1:
      if(!ecm && !JumpEcm()) {
        if(startecm.Elapsed()>IDLE_GETCA_SLOW) {
          if(IsIdle()) { mode=0; break; }
          PRINTF(L_CORE_ECM,"%s: no encryption system found",id);
          filter->SetIdleTime(IDLE_GETCA_SLOW/4);
          startecm.Set();
          }
        break;
        }
      mode=4;
      // fall through

    case 4:
    case 5:
      NoSync(mode==4);
      failed.Clear();
      filter->SetIdleTime(IDLE_NO_SYNC/2);
      lastsync.Set();
      cryptPeriod=20*1000;
      mode=2;
      // fall through
          
    case 2:
      if(sys->NeedsData()) {
        if(!UpdateEcm()) {
          if(lastsync.Elapsed()<ECM_DATA_TIME) break;
          PRINTF(L_CORE_ECM,"%s: no ecm extra data update (waited %d ms)",id,(int)lastsync.Elapsed());
          }
        if(lastsync.Elapsed()>IDLE_NO_SYNC/4 && dolog)
          PRINTF(L_CORE_ECM,"%s: ecm extra data update took %d ms",id,(int)lastsync.Elapsed());
        }
      filter->SetIdleTime(IDLE_NO_SYNC);
      mode=3;
      // fall through

    case 3:
      {
      bool resend=false, cwok=false;
      if(resendTime.TimedOut()) {
        resend=sync; resendTime.Set(8*24*60*60*1000);
        }

      if(startecm.Elapsed()<3*60*1000) cam->DumpAV7110();

      if(data && len>0) {
        HEXDUMP(L_HEX_ECM,data,len,"ECM sys 0x%04x id 0x%02x pid 0x%04x",ecm->caId,ecm->provId,filter->Pid());
        if(SCT_LEN(data)==len) {
          if(ecm->rewriter) {
            ecm->rewriter->Rewrite(data,len);
            HEXDUMP(L_HEX_ECM,data,len,"rewritten to");
            }
          LDUMP(L_CORE_ECMPROC,data,16,"%s: ECM",id);
          int n;
          if(!(n=sys->CheckECM(ecm,data,sync))) {
            if(resend || parity!=(data[0]&1)) {
              int ecmid;
              cTimeMs procTime;
              cwok=(ecmid=failed.Get(data,len,0))>=0 && sys->ProcessECM(ecm,data);
              n=(ecmid>0)?failed.Cache(ecmid,cwok,0):99;
              sys->CheckECMResult(ecm,data,cwok);
              if(cwok) {
                parity=data[0]&1;
                }
              else {
                if(procTime.Elapsed()>6000) {
                  PRINTF(L_CORE_ECM,"%s: filter flush (elapsed %d)",id,(int)procTime.Elapsed());
                  filter->Flush();
                  }
                if(n>=2) { count++; if(n==2) count++; }
                parity=0xFF;
                if(sync && lastsync.Elapsed()>cryptPeriod*2) {
                  PRINTF(L_CORE_ECM,"%s: lost sync (period %d, elapsed %d)",id,cryptPeriod,(int)lastsync.Elapsed());
                  NoSync(true);
                  }
                }
              PRINTF(L_CORE_ECMPROC,"%s: (%s) cwok=%d ecmid=%d n=%d sync=%d parity=%d count=%d ELA=%d",
                    id,sys->Name(),cwok,ecmid,n,sync,parity,count,(int)procTime.Elapsed());
              }
            }
          else {
            PRINTF(L_CORE_ECMPROC,"%s: check result %d",id,n);
            switch(n) {
              case 1: NoSync(true); break;
              case 2: count++; break;
              case 3: break;
              }
            }
          }
        else {
          PRINTF(L_CORE_ECM,"%s: incomplete section %d != %d",id,len,SCT_LEN(data));
          count++;
          }
        }
      else if(sys->Constant()) {
        if(sys->ProcessECM(ecm,NULL)) {
          cwok=true;
          if(sync) filter->SetIdleTime(IDLE_SYNC*10); 
          }
        else count++;
        }
      else count++;

      if(cwok) {
        dolog=LOG_COUNT; sys->DoLog(true);
        cam->WriteCW(filterCwIndex,sys->CW(),resend || !sync);
        memcpy(lastCw,sys->CW(),sizeof(lastCw));
        noKey=false; count=0;
        UpdateEcm(); EcmOk();
        if(!sync) {
          sync=true;
          filter->SetIdleTime(IDLE_SYNC);
          PRINTF(L_CORE_ECM,"%s: correct key found",id);
          if(!cam->IsSoftCSA(filterCwIndex==0))
            resendTime.Set(CW_REPEAT_TIME);
          }
        else if(!resend)
          cryptPeriod=max(5000,min(60000,(int)lastsync.Elapsed()));
        lastsync.Set();
        }

      if(!sync && !trigger) {
        if(count>=sys->MaxEcmTry()) {
          EcmFail(); JumpEcm();
          mode=4;
          if(!ecm) {
            JumpEcm();
            if(!ecm) { // this should not happen!
              PRINTF(L_GEN_DEBUG,"internal: handler %s, empty ecm list in sync loop",id);
              mode=0; break;
              }
            // if we looped through all systems, we wait until the next parity
            // change before we try again.
            if(dolog!=LOG_COUNT && data) { parity=data[0]&1; mode=5; }
            if(dolog && !--dolog) {
              sys->DoLog(false);
              PRINTF(L_CORE_ECM,"%s: stopping message log until valid key is found",id);
              }
            }
          break;
          }
        }
        
      if(IsIdle() && idleTime.Elapsed()>MAX_ECM_HOLD) {
        PRINTF(L_CORE_ECM,"%s: hold timeout expired",id);
        mode=0;
        }

      break;
      }
    }
}

void cEcmHandler::NoSync(bool clearParity)
{
  if(clearParity) parity=0xFF;
  count=0; sync=false;
}

void cEcmHandler::DeleteSys(void)
{
  delete sys; sys=0;
}

char *cEcmHandler::CurrentKeyStr(void) const
{
  if(noKey || !sys) return 0;
  return strdup(sys->CurrentKeyStr());
}

cEcmInfo *cEcmHandler::NewEcm(void)
{
  ecmcache.GetCached(&ecmList,filterSid,filterSource,filterTransponder);
  ecmPriList.Clear();
  for(cEcmInfo *n=ecmList.First(); n; n=ecmList.Next(n)) AddEcmPri(n);
  ecm=0; ecmPri=0;
  return JumpEcm();
}

void cEcmHandler::AddEcmPri(cEcmInfo *n)
{
  int ident, pri=0;
  while(1) {
    if(overrides.Ignore(n->source,n->transponder,n->caId)) break;
    if(!n->Cached()) ident=cSystems::FindIdentBySysId(n->caId,!cam->IsSoftCSA(filterCwIndex==0),pri);
    else ident=(pri==0) ? cSystems::FindIdentBySysName(n->caId,!cam->IsSoftCSA(filterCwIndex==0),n->name,pri) : 0;
    if(ident<=0) break;

    cEcmPri *ep=new cEcmPri;
    if(ep) {
      ep->ecm=n; 
      ep->pri=pri;
      ep->sysIdent=ident;
      if(n->Cached() && (!ScSetup.LocalPriority || pri!=-15)) ep->pri+=20;
      ep->pri=ep->pri*100 + overrides.GetEcmPrio(n->source,n->transponder,n->caId,n->provId);

      // no double entries in ecmPriList
      for(cEcmPri *epp=ecmPriList.First(); epp; epp=ecmPriList.Next(epp))
        if(epp->ecm==ep->ecm && epp->sysIdent==ep->sysIdent) {
          delete ep; ep=0;
          break;
          }
      }
    if(ep) {
      // keep ecmPriList sorted
      cEcmPri *eppp, *epp=ecmPriList.First();
      if(!epp || epp->pri<ep->pri)
        ecmPriList.Ins(ep);
      else {
        do {
          eppp=ecmPriList.Next(epp);
          if(!eppp || eppp->pri<ep->pri) {
            ecmPriList.Add(ep,epp);
            break;
            }
          } while((epp=eppp));
        }
      }
    }
}

void cEcmHandler::StopEcm(void)
{
  filter->Stop(); filter->Flush();
  if(ecm) cam->LogEcmStatus(ecm,false);
  DeleteSys();
}

bool cEcmHandler::UpdateEcm(void)
{
  if(ecmUpd) {
    bool log=dolog;
    dolog=(sys && sys->NeedsData() && ecm->Data()==0);
    if(dolog) PRINTF(L_CORE_ECM,"%s: try to update ecm extra data",id);
    ParseCAInfo(ecm->caId);
    dolog=log;
    }
  return ecm->Data()!=0;
}

cEcmInfo *cEcmHandler::JumpEcm(void)
{
  noKey=true;
  if(!ecmPri) {
    ParseCAInfo(0xFFFF); // all systems
    ecmPri=ecmPriList.First();
    }
  else ecmPri=ecmPriList.Next(ecmPri);
  if(ecmPri) {
    if(ecmPri->ecm!=ecm) {
      StopEcm();
      ecm=ecmPri->ecm;
      filter->Start(ecm->ecm_pid,ecm->ecm_table,0xfe);
      cam->LogEcmStatus(ecm,true);
      }
    else {
      DeleteSys();
      filter->Flush();
      }
    sys=cSystems::FindBySysIdent(ecmPri->sysIdent);
    if(!sys) {
      if(dolog) PRINTF(L_GEN_DEBUG,"internal: handler %s, no system found for ident %04x (caid %04x pri %d)",id,ecmPri->sysIdent,ecmPri->ecm->caId,ecmPri->pri);
      return JumpEcm();
      }
    sys->DoLog(dolog!=0);
    sys->SetOwner(cam);
    failed.SetMaxFail(sys->MaxEcmTry());

    if(dolog) PRINTF(L_CORE_ECM,"%s: try system %s (%04x) id %04x with ecm %x%s (pri=%d)",
                     id,sys->Name(),ecm->caId,ecm->provId,ecm->ecm_pid,ecm->Cached()?" (cached)":"",sys->Pri());
    }
  else {
    StopEcm();
    ecm=0;
    }
  return ecm;
}

void cEcmHandler::EcmOk(void)
{
  ecm->SetName(sys->Name());
  ecm->Fail(false);
  ecmcache.New(ecm);
  cEcmInfo *e=ecmList.First();
  while(e) {
    if(e->Cached() && e->Failed()) ecmcache.Delete(e);
    e=ecmList.Next(e);
    }
}

void cEcmHandler::EcmFail(void)
{
  ecm->Fail(true);
}

void cEcmHandler::ParseCAInfo(int SysId)
{
  ecmUpd=false;
  int len;
  const unsigned char *buff=filterCaDescr.Get(len);
  if(buff && len>0) {
    if(dolog) PRINTF(L_CORE_ECM,"%s: CA descriptors for SID %d (len=%d)",id,filterSid,len);
    HEXDUMP(L_HEX_PMT,buff,len,"PMT");
    for(int index=0; index<len; index+=buff[index+1]+2) {
      if(buff[index]==0x09) {
        int sysId=WORD(buff,index+2,0xFFFF);
        if(SysId!=0xFFFF && sysId!=SysId) continue;
        if(dolog) LDUMP(L_CORE_ECM,&buff[index+2],buff[index+1],"%s: descriptor",id);
        if(overrides.Ignore(filterSource,filterTransponder,sysId)) {
          if(dolog) PRINTF(L_CORE_ECM,"%s: system %04x ignored",id,sysId);
          continue;
          }
        int sysPri=0;
        cSystem *sys;
        while((sys=cSystems::FindBySysId(sysId,!cam->IsSoftCSA(filterCwIndex==0),sysPri))) {
          sysPri=sys->Pri();
          cSimpleList<cEcmInfo> ecms;
          sys->ParseCADescriptor(&ecms,sysId,filterSource,&buff[index+2],buff[index+1]);
          delete sys;
          if(ecms.Count()) {
            cEcmInfo *n;
            while((n=ecms.First())) {
              ecms.Del(n,false);
              n->SetSource(filterSid,filterSource,filterTransponder);
              n->SetDvb(cam->Adapter(),cam->Frontend());
              n->AddCaDescr(&buff[index],buff[index+1]+2);
              overrides.UpdateEcm(n,dolog);
              LBSTARTF(L_CORE_ECM);
              if(dolog) LBPUT("%s: found %04x(%04x) (%s) id %04x with ecm %x/%x ",id,n->caId,n->emmCaId,n->name,n->provId,n->ecm_pid,n->ecm_table);
              cEcmInfo *e=ecmList.First();
              while(e) {
                if(e->ecm_pid==n->ecm_pid) {
                  if(e->caId==n->caId && e->provId==n->provId) {
                    if(e->AddCaDescr(n) && dolog) LBPUT("(updated) ");
                    if(dolog) LBPUT("(already present)");
                    AddEcmPri(e);
                    delete n; n=0;
                    break;
                    }
                  else {
                    e->Fail(true);
                    if(dolog) LBPUT("(dup) ");
                    }
                  }
                e=ecmList.Next(e);
                }
              if(n) {
                if(dolog) LBPUT("(new)");
                ecmList.Add(n);
                AddEcmPri(n);
                }
              LBEND();
              }
            break;
            }
          }
        if(sysPri==0 && dolog) PRINTF(L_CORE_ECM,"%s: no module available for system %04x",id,sysId);
        }
      }
    }

  for(cEcmPri *ep=ecmPriList.First(); ep; ep=ecmPriList.Next(ep))
    PRINTF(L_CORE_ECMPROC,"%s: ecmPriList pri=%d ident=%04x caid=%04x pid=%04x",id,ep->pri,ep->sysIdent,ep->ecm->caId,ep->ecm->ecm_pid);
  PRINTF(L_CORE_ECMPROC,"%s: ecmPri list end",id);
}

// --- cChannelCaids -----------------------------------------------------------

#ifndef SASC

class cChannelCaids : public cSimpleItem {
private:
  int prg, source, transponder;
  int numcaids;
  caid_t caids[MAX_CI_SLOT_CAIDS+1];
public:
  cChannelCaids(cChannel *channel);
  bool IsChannel(cChannel *channel);
  void Sort(void);
  void Del(caid_t caid);
  bool HasCaid(caid_t caid);
  bool Same(cChannelCaids *ch, bool full);
  void HistAdd(unsigned short *hist);
  void Dump(const char *devId);
  const caid_t *Caids(void) { caids[numcaids]=0; return caids; }
  int NumCaids(void) { return numcaids; }
  int Source(void) const { return source; }
  int Transponder(void) const { return transponder; }
  };

cChannelCaids::cChannelCaids(cChannel *channel)
{
  prg=channel->Sid(); source=channel->Source(); transponder=channel->Transponder();
  numcaids=0;
  for(const caid_t *ids=channel->Caids(); *ids; ids++)
    if(numcaids<MAX_CI_SLOT_CAIDS) caids[numcaids++]=*ids;
  Sort();
}

bool cChannelCaids::IsChannel(cChannel *channel)
{
  return prg==channel->Sid() && source==channel->Source() && transponder==channel->Transponder();
}

void cChannelCaids::Sort(void)
{
  caid_t tmp[MAX_CI_SLOT_CAIDS];
  int c=0xFFFF;
  for(int i=0; i<numcaids; i++) {
    int d=0;
    for(int j=0; j<numcaids; j++) if(caids[j]>d && caids[j]<c) d=caids[j];
    tmp[i]=d; c=d;
    }
  memcpy(caids,tmp,sizeof(caids));
}

void cChannelCaids::Del(caid_t caid)
{
  for(int i=0; i<numcaids; i++)
    if(caids[i]==caid) {
      numcaids--; caids[i]=caids[numcaids];
      if(numcaids>0) Sort();
      caids[numcaids]=0;
      break;
      }
}

bool cChannelCaids::HasCaid(caid_t caid)
{
  for(int i=0; i<numcaids; i++) if(caids[i]==caid) return true;
  return false;
}

bool cChannelCaids::Same(cChannelCaids *ch, bool full)
{
  if(full && (source!=ch->source || transponder!=ch->transponder)) return false;
  if(numcaids!=ch->numcaids) return false;
  return memcmp(caids,ch->caids,numcaids*sizeof(caid_t))==0;
}

void cChannelCaids::HistAdd(unsigned short *hist)
{
  for(int i=numcaids-1; i>=0; i--) hist[caids[i]]++;
}

void cChannelCaids::Dump(const char *devId)
{
  LBSTART(L_CORE_CAIDS);
  LBPUT("%s: channel %d/%x/%x",devId,prg,source,transponder);
  for(const caid_t *ids=Caids(); *ids; ids++) LBPUT(" %04x",*ids);
  LBEND();
}

// --- cChannelList ------------------------------------------------------------

class cChannelList : public cSimpleList<cChannelCaids> {
private:
  const char *devId;
public:
  cChannelList(const char *DevId);
  void Unique(bool full);
  void CheckIgnore(void);
  int Histo(void);
  void Purge(int caid, bool fullch);
  };

cChannelList::cChannelList(const char *DevId)
{
  devId=DevId;
}

void cChannelList::CheckIgnore(void)
{
  char *cache=MALLOC(char,0x10000);
  if(!cache) return;
  memset(cache,0,sizeof(char)*0x10000);
  int cTotal=0, cHits=0;
  for(cChannelCaids *ch=First(); ch; ch=Next(ch)) {
    const caid_t *ids=ch->Caids();
    while(*ids) {
      int pri=0;
      if(overrides.Ignore(ch->Source(),ch->Transponder(),*ids))
        ch->Del(*ids);
      else {
        char c=cache[*ids];
        if(c==0) cache[*ids]=c=(cSystems::FindIdentBySysId(*ids,false,pri) ? 1 : -1);
        else cHits++;
        cTotal++;
        if(c<0) {
          for(cChannelCaids *ch2=Next(ch); ch2; ch2=Next(ch2)) ch2->Del(*ids);
          ch->Del(*ids);
          }
        else ids++;
        }
      }
    }
  free(cache);
  PRINTF(L_CORE_CAIDS,"%s: after check",devId);
  for(cChannelCaids *ch=First(); ch; ch=Next(ch)) ch->Dump(devId);
  PRINTF(L_CORE_CAIDS,"%s: check cache usage: %d requests, %d hits, %d%% hits",devId,cTotal,cHits,(cTotal>0)?(cHits*100/cTotal):0);
}

void cChannelList::Unique(bool full)
{
  for(cChannelCaids *ch1=First(); ch1; ch1=Next(ch1)) {
    for(cChannelCaids *ch2=Next(ch1); ch2;) {
      if(ch1->Same(ch2,full) || ch2->NumCaids()<1) {
        cChannelCaids *t=Next(ch2);
        Del(ch2);
        ch2=t;
        }
      else ch2=Next(ch2);
      }
    }
  if(Count()==1 && First() && First()->NumCaids()<1) Del(First());
  PRINTF(L_CORE_CAIDS,"%s: after unique (%d)",devId,full);
  for(cChannelCaids *ch=First(); ch; ch=Next(ch)) ch->Dump(devId);
}

int cChannelList::Histo(void)
{
  int h=-1;
  unsigned short *hist=MALLOC(unsigned short,0x10000);
  if(hist) {
    memset(hist,0,sizeof(unsigned short)*0x10000);
    for(cChannelCaids *ch=First(); ch; ch=Next(ch)) ch->HistAdd(hist);
    int c=0;
    for(int i=0; i<0x10000; i++)
      if(hist[i]>c) { h=i; c=hist[i]; }
    free(hist);
    }
  else PRINTF(L_GEN_ERROR,"malloc failed in cChannelList::Histo");
  return h;
}

void cChannelList::Purge(int caid, bool fullch)
{
  for(cChannelCaids *ch=First(); ch;) {
    if(!fullch) ch->Del(caid);
    if(ch->NumCaids()<=0 || (fullch && ch->HasCaid(caid))) {
      cChannelCaids *t=Next(ch);
      Del(ch);
      ch=t;
      }
    else ch=Next(ch);
    }
  if(Count()>0) {
    PRINTF(L_CORE_CAIDS,"%s: still left",devId);
    for(cChannelCaids *ch=First(); ch; ch=Next(ch)) ch->Dump(devId);
    }
}

// -- cCiFrame -----------------------------------------------------------------

#define LEN_OFF 2

cCiFrame::cCiFrame(void)
{
  rb=0; mem=0; len=alen=glen=0;
}

cCiFrame::~cCiFrame()
{
  free(mem);
}

unsigned char *cCiFrame::GetBuff(int l)
{
  if(!mem || l>alen) {
    free(mem); mem=0; alen=0;
    mem=MALLOC(unsigned char,l+LEN_OFF);
    if(mem) alen=l;
    }
  len=l;
  if(!mem) {
    PRINTF(L_GEN_DEBUG,"internal: ci-frame alloc failed");
    return 0;
    }
  return mem+LEN_OFF;
}

void cCiFrame::Put(void)
{
  if(rb && mem) {
    *((short *)mem)=len;
    rb->Put(mem,len+LEN_OFF);
    }
}

unsigned char *cCiFrame::Get(int &l)
{
  if(rb) {
    int c;
    unsigned char *data=rb->Get(c);
    if(data) {
      if(c>LEN_OFF) {
        int s=*((short *)data);
        if(c>=s+LEN_OFF) {
          l=glen=s;
          return data+LEN_OFF;
          }
        }
      LDUMP(L_GEN_DEBUG,data,c,"internal: ci rb frame sync got=%d avail=%d -",c,rb->Available());
      rb->Clear();
      }
    }
  return 0;
}

int cCiFrame::Avail(void)
{
  return rb ? rb->Available() : 0;
}

void cCiFrame::Del(void)
{
  if(rb && glen) {
    rb->Del(glen+LEN_OFF);
    glen=0;
    }
}

// -- cScCamSlot ---------------------------------------------------------------

#define TDPU_SIZE_INDICATOR 0x80

#define CAID_TIME       300000 // time between caid scans
#define TRIGGER_TIME     10000 // min. time between caid scan trigger
#define SLOT_CAID_CHECK  10000
#define SLOT_RESET_TIME    600

class cScCamSlot : public cCamSlot {
private:
  cCam *cam;
  unsigned short caids[MAX_CI_SLOT_CAIDS+1];
  int slot, version;
  const char *devId;
  cTimeMs checkTimer;
  bool reset, doReply;
  cTimeMs resetTimer;
  eModuleStatus lastStatus;
  cRingBufferLinear rb;
  cCiFrame frame;
  //
  int GetLength(const unsigned char * &data);
  int LengthSize(int n);
  void SetSize(int n, unsigned char * &p);
  void CaInfo(int tcid, int cid);
  bool Check(void);
public:
  cScCamSlot(cCam *Cam, const char *DevId, int Slot);
  void Process(const unsigned char *data, int len);
  eModuleStatus Status(void);
  bool Reset(bool log=true);
  cCiFrame *Frame(void) { return &frame; }
  };

cScCamSlot::cScCamSlot(cCam *Cam, const char *DevId, int Slot)
:cCamSlot(Cam)
,checkTimer(-SLOT_CAID_CHECK-1000)
,rb(KILOBYTE(4),5+LEN_OFF,false,"SC-CI slot answer")
{
  cam=Cam; devId=DevId; slot=Slot;
  version=0; caids[0]=0; doReply=false; lastStatus=msReset;
  frame.SetRb(&rb);
  Reset(false);
}

eModuleStatus cScCamSlot::Status(void)
{
  eModuleStatus status;
  if(reset) {
    status=msReset;
    if(resetTimer.TimedOut()) reset=false;
    }
  else if(caids[0]) status=msReady;
  else {
    status=msPresent; //msNone;
    Check();
    }
  if(status!=lastStatus) {
    static const char *stext[] = { "none","reset","present","ready" };
    PRINTF(L_CORE_CI,"%s.%d: status '%s'",devId,slot,stext[status]);
    lastStatus=status;
    }
  return status;
}

bool cScCamSlot::Reset(bool log)
{
  reset=true; resetTimer.Set(SLOT_RESET_TIME);
  rb.Clear();
  if(log) PRINTF(L_CORE_CI,"%s.%d: reset",devId,slot);
  return true;
}

bool cScCamSlot::Check(void)
{
  bool res=false;
  bool dr=cam->IsSoftCSA(false) || ScSetup.ConcurrentFF>0;
  if(dr!=doReply && !IsDecrypting()) {
    PRINTF(L_CORE_CI,"%s.%d: doReply changed, reset triggered",devId,slot);
    Reset(false);
    doReply=dr;
    }
  if(checkTimer.TimedOut()) {
    if(version!=cam->GetCaids(slot,0,0)) {
      version=cam->GetCaids(slot,caids,MAX_CI_SLOT_CAIDS);
      PRINTF(L_CORE_CI,"%s.%d: now using CAIDs version %d",devId,slot,version);
      res=true;
      }
    checkTimer.Set(SLOT_CAID_CHECK);
    }
  return res;
}

int cScCamSlot::GetLength(const unsigned char * &data)
{
  int len=*data++;
  if(len&TDPU_SIZE_INDICATOR) {
    int i;
    for(i=len&~TDPU_SIZE_INDICATOR, len=0; i>0; i--) len=(len<<8) + *data++;
    }
  return len;
}

int cScCamSlot::LengthSize(int n)
{
  return n<TDPU_SIZE_INDICATOR?1:3;
}

void cScCamSlot::SetSize(int n, unsigned char * &p)
{
  if(n<TDPU_SIZE_INDICATOR) *p++=n;
  else { *p++=2|TDPU_SIZE_INDICATOR; *p++=n>>8; *p++=n&0xFF; }
}

void cScCamSlot::CaInfo(int tcid, int cid)
{
  int cn=0;
  for(int i=0; caids[i]; i++) cn+=2;
  int n=cn+8+LengthSize(cn);
  unsigned char *p;
  if(!(p=frame.GetBuff(n+1+LengthSize(n)))) return;
  *p++=0xa0;
  SetSize(n,p);
  *p++=tcid;
  *p++=0x90;
  *p++=0x02; *p++=cid>>8; *p++=cid&0xff;
  *p++=0x9f; *p++=0x80;   *p++=0x31; // AOT_CA_INFO
  SetSize(cn,p);
  for(int i=0; caids[i]; i++) { *p++=caids[i]>>8; *p++=caids[i]&0xff; }
  frame.Put();
  PRINTF(L_CORE_CI,"%s.%d sending CA info",devId,slot);
}

void cScCamSlot::Process(const unsigned char *data, int len)
{
  const unsigned char *save=data;
  data+=3;
  int dlen=GetLength(data);
  if(dlen>len-(data-save)) {
    PRINTF(L_CORE_CI,"%s.%d TDPU length exceeds data length",devId,slot);
    dlen=len-(data-save);
    }
  int tcid=data[0];

  if(Check()) CaInfo(tcid,0x01);

  if(dlen<8 || data[1]!=0x90) return;
  int cid=(data[3]<<8)+data[4];
  int tag=(data[5]<<16)+(data[6]<<8)+data[7];
  data+=8;
  dlen=GetLength(data);
  if(dlen>len-(data-save)) {
    PRINTF(L_CORE_CI,"%s.%d tag length exceeds data length",devId,slot);
    dlen=len-(data-save);
    }
  switch(tag) {
    case 0x9f8030: // AOT_CA_INFO_ENQ
      CaInfo(tcid,cid);
      break;

    case 0x9f8032: // AOT_CA_PMT
      if(dlen>=6) {
        int ca_lm=data[0];
        int ci_cmd=-1;
        cPrg *prg=new cPrg((data[1]<<8)+data[2],ca_lm==5);
        int ilen=(data[4]<<8)+data[5];
        LBSTARTF(L_CORE_CI);
        LBPUT("%s.%d CA_PMT decoding len=%x lm=%x prg=%d len=%x",devId,slot,dlen,ca_lm,(data[1]<<8)+data[2],ilen);
        data+=6; dlen-=6;
        LBPUT("/%x",dlen);
        if(ilen>0 && dlen>=ilen) {
          ci_cmd=data[0];
          if(ilen>1)
            prg->caDescr.Set(&data[1],ilen-1);
          LBPUT(" ci_cmd(G)=%02x",ci_cmd);
          }
        data+=ilen; dlen-=ilen;
        while(dlen>=5) {
          cPrgPid *pid=new cPrgPid(data[0],(data[1]<<8)+data[2]);
          prg->pids.Add(pid);
          ilen=(data[3]<<8)+data[4];
          LBPUT(" pid=%d,%x len=%x",data[0],(data[1]<<8)+data[2],ilen);
          data+=5; dlen-=5;
          LBPUT("/%x",dlen);
          if(ilen>0 && dlen>=ilen) {
            ci_cmd=data[0];
            if(ilen>1) {
              pid->caDescr.Set(&data[1],ilen-1);
              prg->SetPidCaDescr(true);
              }
            LBPUT(" ci_cmd(S)=%x",ci_cmd);
            }
          data+=ilen; dlen-=ilen;
          }
        LBEND();
        PRINTF(L_CORE_CI,"%s.%d got CA pmt ciCmd=%d caLm=%d",devId,slot,ci_cmd,ca_lm);
        if(doReply && (ci_cmd==0x03 || (ci_cmd==0x01 && ca_lm==0x03))) {
          unsigned char *b;
          if((b=frame.GetBuff(4+11))) {
            b[0]=0xa0; b[2]=tcid;
            b[3]=0x90;
            b[4]=0x02; b[5]=cid<<8; b[6]=cid&0xff;
            b[7]=0x9f; b[8]=0x80; b[9]=0x33; // AOT_CA_PMT_REPLY
            b[11]=prg->sid<<8;
            b[12]=prg->sid&0xff;
            b[13]=0x00;
            b[14]=0x81; 	// CA_ENABLE
            b[10]=4; b[1]=4+9;
            frame.Put();
            PRINTF(L_CORE_CI,"%s.%d answer to query",devId,slot);
            }
          }
        else PRINTF(L_CORE_CI,"%s.%d answer to query surpressed",devId,slot);
        if(ci_cmd==0x04 || (ci_cmd==-1 && prg->sid==0 && ca_lm==0x03)) {
          PRINTF(L_CORE_CI,"%s.%d stop decrypt",devId,slot);
          cam->Stop();
          }
        else if(ci_cmd==0x01 || (ci_cmd==-1 && prg->sid!=0 && (ca_lm==0x03 || ca_lm==0x04 || ca_lm==0x05))) {
          PRINTF(L_CORE_CI,"%s.%d set CAM decrypt (prg %d)",devId,slot,prg->sid);
          cam->AddPrg(prg);
          }
        else PRINTF(L_CORE_CI,"%s.%d no action taken",devId,slot);
        delete prg;
        }
      break;
    }
}

#endif //!SASC

// -- cCams --------------------------------------------------------------------

class cCams : public cSimpleList<cCam> {
public:
  cMutex listMutex;
  //
  void Register(cCam *cam);
  void Unregister(cCam *cam);
  };

static cCams cams;

void cCams::Register(cCam *cam)
{
  cMutexLock lock(&listMutex);
  Add(cam);
}

void cCams::Unregister(cCam *cam)
{
  cMutexLock lock(&listMutex);
  Del(cam,false);
}

// -- cGlobal ------------------------------------------------------------------

char *cGlobal::CurrKeyStr(int camindex, int num, const char **id)
{
  cMutexLock lock(&cams.listMutex);
  char *str=0;
  cCam *c;
  for(c=cams.First(); c && camindex>0; c=cams.Next(c), camindex--);
  if(c) {
    str=c->CurrentKeyStr(num,id);
    if(!str && num==0) str=strdup(tr("(none)"));
    }
  return str;
}

void cGlobal::HouseKeeping(void)
{
  cMutexLock lock(&cams.listMutex);
  for(cCam *c=cams.First(); c; c=cams.Next(c)) c->HouseKeeping();
}

bool cGlobal::Active(bool log)
{
  cMutexLock lock(&cams.listMutex);
  for(cCam *c=cams.First(); c; c=cams.Next(c)) if(c->Active(log)) return true;
  return false;
}

void cGlobal::CaidsChanged(void)
{
  cMutexLock lock(&cams.listMutex);
  PRINTF(L_CORE_CAIDS,"caid list rebuild triggered");
  for(cCam *c=cams.First(); c; c=cams.Next(c)) c->CaidsChanged();
}

// -- cCam ---------------------------------------------------------------

#ifndef SASC
struct TPDU {
  unsigned char slot;
  unsigned char tcid;
  unsigned char tag;
  unsigned char len;
  unsigned char data[1];
  };
#endif

cCam::cCam(cDevice *Device, int Adapter, int Frontend, const char *DevId, cScDevicePlugin *DevPlugin, bool SoftCSA, bool FullTS)
{
  device=Device; devplugin=DevPlugin; adapter=Adapter; frontend=Frontend; devId=DevId;
  softcsa=SoftCSA; fullts=FullTS;
  tcid=0; rebuildcaids=false;
  memset(version,0,sizeof(version));
#ifndef SASC
  memset(slots,0,sizeof(slots));
  SetDescription("SC-CI adapter on device %s",devId);
  rb=new cRingBufferLinear(KILOBYTE(8),6+LEN_OFF,false,"SC-CI adapter read");
  if(rb) {
    rb->SetTimeouts(0,CAM_READ_TIMEOUT);
    frame.SetRb(rb);
    BuildCaids(true);
    slots[0]=new cScCamSlot(this,devId,0);
    Start();
    }
  else PRINTF(L_GEN_ERROR,"failed to create ringbuffer for SC-CI adapter %s.",devId);

  decsa=softcsa ? new cDeCSA(devId) : 0;
#endif //!SASC

  source=transponder=-1; liveVpid=liveApid=0; logger=0; hookman=0;
  memset(lastCW,0,sizeof(lastCW));
  memset(indexMap,0,sizeof(indexMap));
  memset(splitSid,0,sizeof(splitSid));
  cams.Register(this);
}

cCam::~cCam()
{
  cams.Unregister(this);
  handlerList.Clear();
  delete hookman;
  delete logger;
#ifndef SASC
  Cancel(3);
  ciMutex.Lock();
  delete rb; rb=0;
  ciMutex.Unlock();
  delete decsa; decsa=0;
#endif //!SASC
}

#ifndef SASC
bool cCam::OwnSlot(const cCamSlot *slot) const
{
  if(slots[0]==slot) return true;
  //for(int i=0; i<MAX_CI_SLOTS; i++) if(slots[i]==slot) return true;
  return false;
}

int cCam::GetCaids(int slot, unsigned short *Caids, int max)
{
  BuildCaids(false);
  cMutexLock lock(&ciMutex);
  if(Caids) {
    int i;
    for(i=0; i<MAX_CI_SLOT_CAIDS && i<max && caids[i]; i++) Caids[i]=caids[slot][i];
    Caids[i]=0;
    }
  return version[slot];
}
#endif //!SASC

void cCam::CaidsChanged(void)
{
  rebuildcaids=true;
}

#ifndef SASC
void cCam::BuildCaids(bool force)
{
  if(caidTimer.TimedOut() || force || (rebuildcaids && triggerTimer.TimedOut())) {
    PRINTF(L_CORE_CAIDS,"%s: building caid lists",devId);
    cChannelList list(devId);
    Channels.Lock(false);
    for(cChannel *channel=Channels.First(); channel; channel=Channels.Next(channel)) {
      if(!channel->GroupSep() && channel->Ca()>=CA_ENCRYPTED_MIN && device->ProvidesTransponder(channel)) {
        cChannelCaids *ch=new cChannelCaids(channel);
        if(ch) list.Add(ch);
        }
      }
    Channels.Unlock();
    list.Unique(true);
    list.CheckIgnore();
    list.Unique(false);

    int n=0, h;
    caid_t c[MAX_CI_SLOT_CAIDS+1];
    memset(c,0,sizeof(c));
    do {
      if((h=list.Histo())<0) break;
      c[n++]=h;
      LBSTART(L_CORE_CAIDS);
      LBPUT("%s: added %04x caids now",devId,h); for(int i=0; i<n; i++) LBPUT(" %04x",c[i]);
      LBEND();
      list.Purge(h,false);
      } while(n<MAX_CI_SLOT_CAIDS && list.Count()>0);
    c[n]=0;
    if(n==0) PRINTF(L_CORE_CI,"%s: no active CAIDs",devId);
    else if(list.Count()>0) PRINTF(L_GEN_ERROR,"too many CAIDs. You should ignore some CAIDs.");

    ciMutex.Lock();
    if((version[0]==0 && c[0]!=0) || memcmp(caids[0],c,sizeof(caids[0]))) {
      memcpy(caids[0],c,sizeof(caids[0]));
      version[0]++;
      if(version[0]>0) {
        LBSTART(L_CORE_CI);
        LBPUT("card %s, slot %d (v=%2d) caids:",devId,0,version[0]);
        for(int i=0; caids[0][i]; i++) LBPUT(" %04x",caids[0][i]);
        LBEND();
        }
      }
    ciMutex.Unlock();

    caidTimer.Set(CAID_TIME);
    triggerTimer.Set(TRIGGER_TIME);
    rebuildcaids=false;
    }
}

int cCam::Read(unsigned char *Buffer, int MaxLength)
{
  cMutexLock lock(&ciMutex);
  if(rb && Buffer && MaxLength>0) {
    int s;
    unsigned char *data=frame.Get(s);
    if(data) {
      if(s<=MaxLength) memcpy(Buffer,data,s);
      else PRINTF(L_GEN_DEBUG,"internal: sc-ci %s rb frame size exceeded %d",devId,s);
      frame.Del();
      if(Buffer[2]!=0x80 || LOG(L_CORE_CIFULL)) {
        LDUMP(L_CORE_CI,Buffer,s,"%s.%d <-",devId,Buffer[0]);
        readTimer.Set();
        }
      return s;
      }
    }
  else cCondWait::SleepMs(CAM_READ_TIMEOUT);
  if(LOG(L_CORE_CIFULL) && readTimer.Elapsed()>2000) {
    PRINTF(L_CORE_CIFULL,"%s: read heartbeat",devId);
    readTimer.Set();
    }
  return 0;
}

#define TPDU(data,slot)   do { unsigned char *_d=(data); _d[0]=(slot); _d[1]=tcid; } while(0)
#define TAG(data,tag,len) do { unsigned char *_d=(data); _d[0]=(tag); _d[1]=(len); } while(0)
#define SB_TAG(data,sb)   do { unsigned char *_d=(data); _d[0]=0x80; _d[1]=0x02; _d[2]=tcid; _d[3]=(sb); } while(0)

void cCam::Write(const unsigned char *buff, int len)
{
  cMutexLock lock(&ciMutex);
  if(buff && len>=5) {
    struct TPDU *tpdu=(struct TPDU *)buff;
    int slot=tpdu->slot;
    cCiFrame *slotframe=slots[slot]->Frame();
    if(buff[2]!=0xA0 || buff[3]>0x01 || LOG(L_CORE_CIFULL))
      LDUMP(L_CORE_CI,buff,len,"%s.%d ->",devId,slot);
    if(slots[slot]) {
      switch(tpdu->tag) {
        case 0x81: // T_RCV
          {
          int s;
          unsigned char *d=slotframe->Get(s);
          if(d) {
            unsigned char *b;
            if((b=frame.GetBuff(s+6))) {
              TPDU(b,slot);
              memcpy(b+2,d,s);
              slotframe->Del(); // delete from rb before Avail()
              SB_TAG(b+2+s,slotframe->Avail()>0 ? 0x80:0x00);
              frame.Put();
              }
            else slotframe->Del();
            }
          break;
          }
        case 0x82: // T_CREATE_TC
          {
          tcid=tpdu->data[0];
          unsigned char *b;
          static const unsigned char reqCAS[] = { 0xA0,0x07,0x01,0x91,0x04,0x00,0x03,0x00,0x41 };
          if((b=slotframe->GetBuff(sizeof(reqCAS)))) {
            memcpy(b,reqCAS,sizeof(reqCAS));
            b[2]=tcid;
            slotframe->Put();
            }
          if((b=frame.GetBuff(9))) {
            TPDU(b,slot);
            TAG(&b[2],0x83,0x01); b[4]=tcid;
            SB_TAG(&b[5],slotframe->Avail()>0 ? 0x80:0x00);
            frame.Put();
            }
          break;
          }
        case 0xA0: // T_DATA_LAST
          {
          slots[slot]->Process(buff,len);
          unsigned char *b;
          if((b=frame.GetBuff(6))) {
            TPDU(b,slot);
            SB_TAG(&b[2],slotframe->Avail()>0 ? 0x80:0x00);
            frame.Put();
            }
          break;
          }
        }
      }
    }
  else PRINTF(L_CORE_CIFULL,"%s: short write (buff=%d len=%d)",devId,buff!=0,len);
}

bool cCam::Reset(int Slot)
{
  cMutexLock lock(&ciMutex);
  PRINTF(L_CORE_CI,"%s: reset of slot %d requested",devId,Slot);
  return slots[Slot] ? slots[Slot]->Reset():false;
}

eModuleStatus cCam::ModuleStatus(int Slot)
{
  cMutexLock lock(&ciMutex);
  bool enable=ScSetup.CapCheck(device->CardIndex());
  if(!enable) Stop();
  return (enable && slots[Slot]) ? slots[Slot]->Status():msNone;
}

bool cCam::Assign(cDevice *Device, bool Query)
{
  return Device ? (Device==device) : true;
}
#endif //!SASC

bool cCam::IsSoftCSA(bool live)
{
  return softcsa && (!fullts || !live);
}

void cCam::Tune(const cChannel *channel)
{
  cMutexLock lock(&camMutex);
  if(source!=channel->Source() || transponder!=channel->Transponder()) {
    source=channel->Source(); transponder=channel->Transponder();
    PRINTF(L_CORE_PIDS,"%s: now tuned to source %x(%s) transponder %x",devId,source,*cSource::ToString(source),transponder);
    Stop();
    }
  else PRINTF(L_CORE_PIDS,"%s: tune to same source/transponder",devId);
}

void cCam::PostTune(void)
{
  cMutexLock lock(&camMutex);
  if(ScSetup.PrestartAU) {
    LogStartup();
    if(logger) logger->PreScan(source,transponder);
    }
}

void cCam::SetPid(int type, int pid, bool on)
{
  cMutexLock lock(&camMutex);
  int oldA=liveApid, oldV=liveVpid;
  if(type==1) liveVpid=on ? pid:0;
  else if(type==0) liveApid=on ? pid:0;
  else if(liveVpid==pid && on) liveVpid=0;
  else if(liveApid==pid && on) liveApid=0;
  if(oldA!=liveApid || oldV!=liveVpid)
    PRINTF(L_CORE_PIDS,"%s: livepids video=%04x audio=%04x",devId,liveVpid,liveApid);
}

void cCam::Stop(void)
{
  cMutexLock lock(&camMutex);
  for(cEcmHandler *handler=handlerList.First(); handler; handler=handlerList.Next(handler))
    handler->Stop();
  if(logger) logger->Down();
  if(hookman) hookman->Down();
  memset(splitSid,0,sizeof(splitSid));
}

void cCam::AddPrg(cPrg *prg)
{
  cMutexLock lock(&camMutex);
  bool islive=false;
  for(cPrgPid *pid=prg->pids.First(); pid; pid=prg->pids.Next(pid))
    if(pid->pid==liveVpid || pid->pid==liveApid) {
      islive=true;
      break;
      }
  bool needZero=!IsSoftCSA(islive) && (islive || !ScSetup.ConcurrentFF);
  bool noshift=IsSoftCSA(true) || (prg->IsUpdate() && prg->pids.Count()==0);
  PRINTF(L_CORE_PIDS,"%s: %s SID %d (zero=%d noshift=%d)",devId,prg->IsUpdate()?"update":"add",prg->sid,needZero,noshift);
  if(prg->pids.Count()>0) {
    LBSTART(L_CORE_PIDS);
    LBPUT("%s: pids",devId);
    for(cPrgPid *pid=prg->pids.First(); pid; pid=prg->pids.Next(pid))
      LBPUT(" %s=%04x",TYPENAME(pid->type),pid->pid);
    LBEND();
    }
  bool isSplit=false;
  if(prg->pids.Count()>0 && prg->SimplifyCaDescr()) isSplit=true;
  else {
    for(int i=0; splitSid[i]; i++)
      if(splitSid[i]==prg->sid) { isSplit=true; break; }
    }
  if(!isSplit) {
    cEcmHandler *handler=GetHandler(prg->sid,needZero,noshift);
    if(handler) {
      PRINTF(L_CORE_PIDS,"%s: found handler for SID %d (%s idle=%d idx=%d)",devId,prg->sid,handler->Id(),handler->IsIdle(),handler->CwIndex());
      prg->source=source;
      prg->transponder=transponder;
      handler->SetPrg(prg);
      }
    }
  else {
    PRINTF(L_CORE_PIDS,"%s: SID %d is handled as splitted",devId,prg->sid);
    // first update the splitSid list
    if(prg->pids.Count()==0) { // delete
      for(int i=0; splitSid[i]; i++)
        if(splitSid[i]==prg->sid) {
          memmove(&splitSid[i],&splitSid[i+1],sizeof(splitSid[0])*(MAX_SPLIT_SID-i));
          break;
          }
      PRINTF(L_CORE_PIDS,"%s: deleted from list",devId);
      }
    else { // add
      bool has=false;
      int i;
      for(i=0; splitSid[i]; i++) if(splitSid[i]==prg->sid) has=true;
      if(!has) {
        if(i<MAX_SPLIT_SID) {
          splitSid[i]=prg->sid;
          splitSid[i+1]=0;
          PRINTF(L_CORE_PIDS,"%s: added to list",devId);
          }
        else PRINTF(L_CORE_PIDS,"%s: split SID list overflow",devId);
        }
      }
    LBSTART(L_CORE_PIDS);
    LBPUT("%s: split SID list now:",devId);
    for(int i=0; i<=MAX_SPLIT_SID; i++) LBPUT(" %d",splitSid[i]);
    LBEND();
    // prepare an empty prg head
    cPrg work;
    work.source=source;
    work.transponder=transponder;
    // loop through pids
    int group=1;
    cPrgPid *first;
    while((first=prg->pids.First())) {
      LBSTARTF(L_CORE_PIDS);
      LBPUT("%s: group %d pids",devId,group);
      prg->pids.Del(first,false);
      work.caDescr.Set(&first->caDescr);
      first->caDescr.Clear();
      work.pids.Add(first);
      LBPUT(" %04x",first->pid);
      for(cPrgPid *pid=prg->pids.First(); pid;) {
        cPrgPid *next=prg->pids.Next(pid);
        if(work.caDescr==pid->caDescr) { // same group
          prg->pids.Del(pid,false);
          pid->caDescr.Clear();
          work.pids.Add(pid);
          LBPUT(" %04x",pid->pid);
          }
        pid=next;
        }
      LBEND();
      // get a handler for the group
      int grsid=group*SIDGRP_SHIFT+prg->sid;
      cEcmHandler *handler=0;
      if(group==1) {
        // in the first group check if we have a non-split handler
        // for the sid
        for(handler=handlerList.First(); handler; handler=handlerList.Next(handler))
          if(handler->Sid()==prg->sid) {
            // let GetHandler() take care of needZero/noshift stuff
            handler=GetHandler(prg->sid,needZero,noshift);
            break;
            }
        }
      // otherwise get the group-sid handler
      if(!handler) handler=GetHandler(grsid,needZero,noshift);
      if(handler) {
        PRINTF(L_CORE_PIDS,"%s: found handler for group-SID %d (%s idle=%d idx=%d)",devId,grsid,handler->Id(),handler->IsIdle(),handler->CwIndex());
        work.sid=grsid;
        handler->SetPrg(&work);
        }
      // prepare for next group
      work.pids.Clear();
      needZero=false; // only one group can have this
      group++;
      }
    // now we scan the handler list for leftover group handlers
    for(cEcmHandler *handler=handlerList.First(); handler; handler=handlerList.Next(handler)) {
      int sid=handler->Sid();
      if(!handler->IsIdle() && sid>SIDGRP_SHIFT) {
        int gr=sid/SIDGRP_SHIFT;
        sid%=SIDGRP_SHIFT;
        if(sid==prg->sid && gr>=group) {
          PRINTF(L_CORE_PIDS,"%s: idle group handler %s idx=%d",devId,handler->Id(),handler->CwIndex());
          work.sid=handler->Sid();
          handler->SetPrg(&work);
          }
        }
      }
    }
}

bool cCam::HasPrg(int prg)
{
  cMutexLock lock(&camMutex);
  for(cEcmHandler *handler=handlerList.First(); handler; handler=handlerList.Next(handler))
    if(!handler->IsIdle() && handler->Sid()==prg)
      return true;
  return false;
}

char *cCam::CurrentKeyStr(int num, const char **id)
{
  cMutexLock lock(&camMutex);
  if(id) *id=devId;
  cEcmHandler *handler;
  for(handler=handlerList.First(); handler; handler=handlerList.Next(handler))
    if(--num<0) return handler->CurrentKeyStr();
  return 0;
}

bool cCam::Active(bool log)
{
  cMutexLock lock(&camMutex);
  for(cEcmHandler *handler=handlerList.First(); handler; handler=handlerList.Next(handler))
    if(!handler->IsIdle()) {
      if(log) PRINTF(L_GEN_INFO,"handler %s on card %s is not idle",handler->Id(),devId);
      return true;
      }
  return false;
}

void cCam::HouseKeeping(void)
{
  cMutexLock lock(&camMutex);
  for(cEcmHandler *handler=handlerList.First(); handler;) {
    cEcmHandler *next=handlerList.Next(handler);
    if(handler->IsRemoveable()) RemHandler(handler);
    handler=next;
    }
  if(handlerList.Count()<1 && !ScSetup.PrestartAU) {
    delete hookman; hookman=0;
    delete logger; logger=0;
    }
}

void cCam::LogStartup(void)
{
  if(!logger && ScSetup.AutoUpdate) {
    logger=new cLogger(this,device,devId,IsSoftCSA(false));
    LogStatsUp();
    }
}

void cCam::LogEcmStatus(const cEcmInfo *ecm, bool on)
{
  cMutexLock lock(&camMutex);
  if(on) LogStartup();
  if(logger) logger->EcmStatus(ecm,on);
}

void cCam::AddHook(cLogHook *hook)
{
  cMutexLock lock(&camMutex);
  if(!hookman) hookman=new cHookManager(device,devId);
  if(hookman) hookman->AddHook(hook);
}

bool cCam::TriggerHook(int id)
{
  return hookman && hookman->TriggerHook(id);
}

void cCam::SetCWIndex(int pid, int index)
{
  if(index<MAX_CW_IDX) {
    ca_pid_t ca_pid;
    ca_pid.pid=pid;
    ca_pid.index=index;
    PRINTF(L_CORE_PIDS,"%s: descrambling pid %04x on index %x",devId,pid,index);
    if(!SetCaPid(&ca_pid))
      if(index>0) {
        PRINTF(L_GEN_ERROR,"CA_SET_PID failed (%s). Expect a black screen/bad recording. Do you use the patched DVB driver?",strerror(errno));
        PRINTF(L_GEN_WARN,"Adjusting 'Concurrent FF streams' to NO");
        ScSetup.ConcurrentFF=0;
        ScSetup.Store(true);
        }
    }
}

void cCam::WriteCW(int index, unsigned char *cw, bool force)
{
  if(index<MAX_CW_IDX) {
    for(int i=0; i<16; i+=4) cw[i+3]=cw[i]+cw[i+1]+cw[i+2];
    ca_descr_t ca_descr;
    ca_descr.index=index;
    unsigned char *last=lastCW[index];
    if(force || memcmp(&cw[0],&last[0],8)) {
      memcpy(&last[0],&cw[0],8);
      ca_descr.parity=0;
      memcpy(ca_descr.cw,&cw[0],8);
      if(!SetCaDescr(&ca_descr,force))
        PRINTF(L_GEN_ERROR,"CA_SET_DESCR failed (%s). Expect a black screen.",strerror(errno));
      }

    if(force || memcmp(&cw[8],&last[8],8)) {
      memcpy(&last[8],&cw[8],8);
      ca_descr.parity=1;
      memcpy(ca_descr.cw,&cw[8],8);
      if(!SetCaDescr(&ca_descr,force))
        PRINTF(L_GEN_ERROR,"CA_SET_DESCR failed (%s). Expect a black screen.",strerror(errno));
      }
    }
}

bool cCam::SetCaDescr(ca_descr_t *ca_descr, bool initial)
{
  if(!softcsa || (fullts && ca_descr->index==0))
    return devplugin->SetCaDescr(device,ca_descr,initial);
#ifndef SASC
  else if(decsa)
    return decsa->SetDescr(ca_descr,initial);
#endif //!SASC
  return false;
}

bool cCam::SetCaPid(ca_pid_t *ca_pid)
{
  if(!softcsa || (fullts && ca_pid->index==0))
    return devplugin->SetCaPid(device,ca_pid);
#ifndef SASC
  else if(decsa)
    return decsa->SetCaPid(ca_pid);
#endif //!SASC
  return false;
}

void cCam::DumpAV7110(void)
{
  devplugin->DumpAV(device);
}

int cCam::GetFreeIndex(void)
{
  for(int idx=0; idx<MAX_CW_IDX; idx++)
    if(!indexMap[idx]) return idx;
  return -1;
}

cEcmHandler *cCam::GetHandler(int sid, bool needZero, bool noshift)
{
  cEcmHandler *zerohandler=0, *sidhandler=0, *idlehandler=0;
  for(cEcmHandler *handler=handlerList.First(); handler; handler=handlerList.Next(handler)) {
    if(handler->Sid()==sid)
      sidhandler=handler;
    if(handler->CwIndex()==0)
      zerohandler=handler;
    if(handler->IsIdle() && (!idlehandler || (!(needZero ^ (idlehandler->CwIndex()!=0)) && (needZero ^ (handler->CwIndex()!=0))) ))
      idlehandler=handler;
    }
  LBSTART(L_CORE_PIDS);
  LBPUT("%s: SID=%d zero=%d |",devId,sid,needZero);
  if(sidhandler) LBPUT(" sid=%d/%d/%d",sidhandler->CwIndex(),sidhandler->Sid(),sidhandler->IsIdle());
  else LBPUT(" sid=-/-/-");
  if(zerohandler) LBPUT(" zero=%d/%d/%d",zerohandler->CwIndex(),zerohandler->Sid(),zerohandler->IsIdle());
  else LBPUT(" zero=-/-/-");
  if(idlehandler) LBPUT(" idle=%d/%d/%d",idlehandler->CwIndex(),idlehandler->Sid(),idlehandler->IsIdle());
  else LBPUT(" idle=-/-/-");
  LBEND();

  if(sidhandler) {
    if(needZero && sidhandler->CwIndex()!=0 && !noshift) {
      if(!sidhandler->IsIdle())
        PRINTF(L_CORE_ECM,"%s: shifting cwindex on non-idle handler.",devId);
      if(zerohandler) {
        if(!zerohandler->IsIdle())
          PRINTF(L_CORE_ECM,"%s: shifting non-idle zero handler. This shouldn't happen!",devId);
        zerohandler->ShiftCwIndex(sidhandler->CwIndex());
        sidhandler->ShiftCwIndex(0);
        }
      else if(indexMap[0]==0) {
        indexMap[0]=1;
        indexMap[sidhandler->CwIndex()]=0;
        sidhandler->ShiftCwIndex(0);
        }
      else PRINTF(L_CORE_ECM,"%s: zero index not free.",devId);
      }

    if(!needZero && sidhandler->CwIndex()==0 && !noshift) {
      if(!sidhandler->IsIdle())
        PRINTF(L_CORE_ECM,"%s: shifting cwindex on non-idle handler.",devId);
      int idx=GetFreeIndex();
      if(idx>=0) {
        indexMap[idx]=1;
        sidhandler->ShiftCwIndex(idx);
        indexMap[0]=0;
        }
      else PRINTF(L_CORE_ECM,"%s: no free cwindex. Can't free zero index.",devId);
      }

    return sidhandler;
    }

  if(needZero && zerohandler) {
    if(!zerohandler->IsIdle())
      PRINTF(L_CORE_ECM,"%s: changing SID on non-idle zero handler. This shouldn't happen!",devId);
    return zerohandler;
    }
  
  if(idlehandler) {
    if(needZero && idlehandler->CwIndex()!=0 && !noshift) {
      if(indexMap[0]==0) {
        indexMap[0]=1;
        indexMap[idlehandler->CwIndex()]=0;
        idlehandler->ShiftCwIndex(0);
        }
      else PRINTF(L_CORE_ECM,"%s: zero index not free. (2)",devId);
      }
    if(!needZero && idlehandler->CwIndex()==0 && !noshift) {
      int idx=GetFreeIndex();
      if(idx>=0) {
        indexMap[idx]=1;
        idlehandler->ShiftCwIndex(idx);
        indexMap[0]=0;
        }
      else PRINTF(L_CORE_ECM,"%s: no free cwindex. Can't free zero index. (2)",devId);
      }
    if((needZero ^ (idlehandler->CwIndex()==0)))
      PRINTF(L_CORE_ECM,"%s: idlehandler index doesn't match needZero",devId);
    return idlehandler;
    }

  int idx=GetFreeIndex();
  if(!needZero && idx==0) {
    indexMap[0]=1;
    idx=GetFreeIndex();
    indexMap[0]=0;
    if(idx<0) {
      idx=0;
      PRINTF(L_CORE_ECM,"%s: can't respect !needZero for new handler",devId);
      }
    }
  if(idx<0) {
    PRINTF(L_CORE_ECM,"%s: no free cwindex",devId);
    return 0;
    }
  indexMap[idx]=1;
  idlehandler=new cEcmHandler(this,device,devId,idx);
  handlerList.Add(idlehandler);
  return idlehandler;
}

void cCam::RemHandler(cEcmHandler *handler)
{
  int idx=handler->CwIndex();
  PRINTF(L_CORE_PIDS,"%s: removing %s on cw index %d",devId,handler->Id(),idx);
  handlerList.Del(handler);
  indexMap[idx]=0;
}

// -- cDeCSA -------------------------------------------------------------------

#ifndef SASC

#define MAX_STALL_MS 70

#define MAX_REL_WAIT 100 // time to wait if key in used on set
#define MAX_KEY_WAIT 500 // time to wait if key not ready on change

#define FL_EVEN_GOOD 1
#define FL_ODD_GOOD  2
#define FL_ACTIVITY  4


cDeCSA::cDeCSA(const char *DevId)
:stall(MAX_STALL_MS)
{
  devId=DevId;
  cs=get_suggested_cluster_size();
  PRINTF(L_CORE_CSA,"%s: clustersize=%d rangesize=%d",devId,cs,cs*2+5);
  range=MALLOC(unsigned char *,(cs*2+5));
  memset(keys,0,sizeof(keys));
  memset(pidmap,0,sizeof(pidmap));
  ResetState();
}

cDeCSA::~cDeCSA()
{
  for(int i=0; i<MAX_CSA_IDX; i++)
    if(keys[i]) free_key_struct(keys[i]);
  free(range);
}

void cDeCSA::ResetState(void)
{
  PRINTF(L_CORE_CSA,"%s: reset state",devId);
  memset(even_odd,0,sizeof(even_odd));
  memset(flags,0,sizeof(flags));
  lastData=0;
}

void cDeCSA::SetActive(bool on)
{
  if(!on && active) ResetState();
  active=on;
  PRINTF(L_CORE_CSA,"%s: set active %s",devId,active?"on":"off");
}

bool cDeCSA::GetKeyStruct(int idx)
{
  if(!keys[idx]) keys[idx]=get_key_struct();
  return keys[idx]!=0;
}

bool cDeCSA::SetDescr(ca_descr_t *ca_descr, bool initial)
{
  cMutexLock lock(&mutex);
  int idx=ca_descr->index;
  if(idx<MAX_CSA_IDX && GetKeyStruct(idx)) {
    if(!initial && active && ca_descr->parity==(even_odd[idx]&0x40)>>6) {
      if(flags[idx] & (ca_descr->parity?FL_ODD_GOOD:FL_EVEN_GOOD)) {
        PRINTF(L_CORE_CSA,"%s.%d: %s key in use (%d ms)",devId,idx,ca_descr->parity?"odd":"even",MAX_REL_WAIT);
        if(wait.TimedWait(mutex,MAX_REL_WAIT)) PRINTF(L_CORE_CSA,"%s.%d: successfully waited for release",devId,idx);
        else PRINTF(L_CORE_CSA,"%s.%d: timed out. setting anyways",devId,idx);
        }
      else PRINTF(L_CORE_CSA,"%s.%d: late key set...",devId,idx);
      }
    LDUMP(L_CORE_CSA,ca_descr->cw,8,"%s.%d: %4s key set",devId,idx,ca_descr->parity?"odd":"even");
    if(ca_descr->parity==0) {
      set_even_control_word(keys[idx],ca_descr->cw);
      if(!CheckNull(ca_descr->cw,8)) flags[idx]|=FL_EVEN_GOOD|FL_ACTIVITY;
      else PRINTF(L_CORE_CSA,"%s.%d: zero even CW",devId,idx);
      wait.Broadcast();
      }
    else {
      set_odd_control_word(keys[idx],ca_descr->cw);
      if(!CheckNull(ca_descr->cw,8)) flags[idx]|=FL_ODD_GOOD|FL_ACTIVITY;
      else PRINTF(L_CORE_CSA,"%s.%d: zero odd CW",devId,idx);
      wait.Broadcast();
      }
    }
  return true;
}

bool cDeCSA::SetCaPid(ca_pid_t *ca_pid)
{
  cMutexLock lock(&mutex);
  if(ca_pid->index<MAX_CSA_IDX && ca_pid->pid<MAX_CSA_PIDS) {
    pidmap[ca_pid->pid]=ca_pid->index;
    PRINTF(L_CORE_CSA,"%s.%d: set pid %04x",devId,ca_pid->index,ca_pid->pid);
    }
  return true;
}

bool cDeCSA::Decrypt(unsigned char *data, int len, bool force)
{
  cMutexLock lock(&mutex);
  int r=-2, ccs=0, currIdx=-1;
  bool newRange=true;
  range[0]=0;
  len-=(TS_SIZE-1);
  int l;
  for(l=0; l<len; l+=TS_SIZE) {
    if(data[l]!=TS_SYNC_BYTE) {       // let higher level cope with that
      PRINTF(L_CORE_CSA,"%s: garbage in TS buffer",devId);
      if(ccs) force=true;             // prevent buffer stall
      break;
      }
    unsigned int ev_od=data[l+3]&0xC0;
    if(ev_od==0x80 || ev_od==0xC0) { // encrypted
      int idx=pidmap[((data[l+1]<<8)+data[l+2])&(MAX_CSA_PIDS-1)];
      if(currIdx<0 || idx==currIdx) { // same or no index
        currIdx=idx;
        if(ccs==0 && ev_od!=even_odd[idx]) {
          even_odd[idx]=ev_od;
          wait.Broadcast();
          PRINTF(L_CORE_CSA,"%s.%d: change to %s key",devId,idx,(ev_od&0x40)?"odd":"even");
          bool doWait=false;
          if(ev_od&0x40) {
            flags[idx]&=~FL_EVEN_GOOD;
            if(!(flags[idx]&FL_ODD_GOOD)) doWait=true;
            }
          else {
            flags[idx]&=~FL_ODD_GOOD;
            if(!(flags[idx]&FL_EVEN_GOOD)) doWait=true;
            }
          if(doWait) {
            PRINTF(L_CORE_CSA,"%s.%d: %s key not ready (%d ms)",devId,idx,(ev_od&0x40)?"odd":"even",MAX_KEY_WAIT);
            if(flags[idx]&FL_ACTIVITY) {
              flags[idx]&=~FL_ACTIVITY;
              if(wait.TimedWait(mutex,MAX_KEY_WAIT)) PRINTF(L_CORE_CSA,"%s.%d: successfully waited for key",devId,idx);
              else PRINTF(L_CORE_CSA,"%s.%d: timed out. proceeding anyways",devId,idx);
              }
            else PRINTF(L_CORE_CSA,"%s.%d: not active. wait skipped",devId,idx);
            }
          }
        if(newRange) {
          r+=2; newRange=false;
          range[r]=&data[l];
          range[r+2]=0;
          }
        range[r+1]=&data[l+TS_SIZE];
        if(++ccs>=cs) break;
        }
      else newRange=true;             // other index, create hole
      }
    else {                            // unencrypted
      // nothing, we don't create holes for unencrypted packets
      }
    }
  int scanTS=l/TS_SIZE;
  int stallP=ccs*100/scanTS;

  LBSTART(L_CORE_CSAVERB);
  LBPUT("%s: %s-%d-%d : %d-%d-%d stall=%d :: ",
        devId,data==lastData?"SAME":"MOVE",(len+(TS_SIZE-1))/TS_SIZE,force,
        currIdx,ccs,scanTS,stallP);
  for(int l=0; l<len; l+=TS_SIZE) {
    if(data[l]!=TS_SYNC_BYTE) break;
    unsigned int ev_od=data[l+3]&0xC0;
    if(ev_od&0x80) {
      int pid=(data[l+1]<<8)+data[l+2];
      int idx=pidmap[pid&(MAX_CSA_PIDS-1)];
      LBPUT("%s/%x/%d ",(ev_od&0x40)?"o":"e",pid,idx);
      }
    else {
      LBPUT("*/%x ",(data[l+1]<<8)+data[l+2]);
      }
    }
  LBEND();

  if(r>=0 && ccs<cs && !force) {
    if(lastData==data && stall.TimedOut()) {
      PRINTF(L_CORE_CSAVERB,"%s: stall timeout -> forced",devId);
      force=true;
      }
    else if(stallP<=10 && scanTS>=cs) {
      PRINTF(L_CORE_CSAVERB,"%s: flow factor stall -> forced",devId);
      force=true;
      }
    }
  lastData=data;

  if(r>=0) {                          // we have some range
    if(ccs>=cs || force) {
      if(GetKeyStruct(currIdx)) {
        int n=decrypt_packets(keys[currIdx],range);
        PRINTF(L_CORE_CSAVERB,"%s.%d: decrypting ccs=%3d cs=%3d %s -> %3d decrypted",devId,currIdx,ccs,cs,ccs>=cs?"OK ":"INC",n);
        if(n>0) {
          stall.Set(MAX_STALL_MS);
          return true;
          }
        }
      }
    else PRINTF(L_CORE_CSAVERB,"%s.%d: incomplete ccs=%3d cs=%3d",devId,currIdx,ccs,cs);
    }
  return false;
}

#endif //!SASC
