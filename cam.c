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
#include <sys/ioctl.h>
#include <sys/time.h>
#include <dlfcn.h>

#include <linux/dvb/ca.h>
#include <vdr/channels.h>
#include <vdr/ci.h>
#include <vdr/dvbdevice.h>
#ifndef SASC
#if APIVERSNUM >= 10500
#include <vdr/dvbci.h>
#endif
#include <vdr/thread.h>

#include "FFdecsa/FFdecsa.h"
#endif //SASC

#include "cam.h"
#include "scsetup.h"
#include "filter.h"
#include "system.h"
#include "data.h"
#include "override.h"
#include "misc.h"
#include "log-core.h"

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
  int cardNum;
  cSimpleList<cLogHook> hooks;
  //
  cPidFilter *AddFilter(int Pid, int Section, int Mask, int Mode, int IdleTime, bool Crc);
  void ClearHooks(void);
  void DelHook(cLogHook *hook);
protected:
  virtual void Process(cPidFilter *filter, unsigned char *data, int len);
public:
  cHookManager(int CardNum);
  virtual ~cHookManager();
  void AddHook(cLogHook *hook);
  bool TriggerHook(int id);
  void Down(void);
  };

cHookManager::cHookManager(int CardNum)
:cAction("hookmanager",CardNum)
{
  cardNum=CardNum;
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
  PRINTF(L_CORE_HOOK,"%d: starting hook '%s' (%04x)",cardNum,hook->name,hook->id);
  hook->delay.Set(CHAIN_HOLD);
  hook->cardNum=cardNum;
  hooks.Add(hook);
  for(cPid *pid=hook->pids.First(); pid; pid=hook->pids.Next(pid)) {
    cPidFilter *filter=AddFilter(pid->pid,pid->sct,pid->mask,pid->mode,CHAIN_HOLD/8,false);
    if(filter) {
      filter->userData=(void *)hook;
      pid->filter=filter;
      }
    }
  Unlock();
}

void cHookManager::DelHook(cLogHook *hook)
{
  PRINTF(L_CORE_HOOK,"%d: stopping hook '%s' (%04x)",cardNum,hook->name,hook->id);
  for(cPid *pid=hook->pids.First(); pid; pid=hook->pids.Next(pid)) {
    cPidFilter *filter=pid->filter;
    if(filter) {
      DelFilter(filter);
      pid->filter=0;
      }
    }
  hooks.Del(hook);
}

cPidFilter *cHookManager::AddFilter(int Pid, int Section, int Mask, int Mode, int IdleTime, bool Crc)
{
  cPidFilter *filter=NewFilter(IdleTime);
  if(filter) {
    filter->SetBuffSize(32768);
    filter->Start(Pid,Section,Mask,Mode,Crc);
    PRINTF(L_CORE_HOOK,"%d: added filter pid=0x%.4x sct=0x%.2x/0x%.2x/0x%.2x idle=%d crc=%d",cardNum,Pid,Section,Mask,Mode,IdleTime,Crc);
    }
  else PRINTF(L_GEN_ERROR,"no free slot or filter failed to open for hookmanager %d",cardNum);
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
    else PRINTF(L_CORE_HOOK,"%d: incomplete section %d != %d",cardNum,len,SCT_LEN(data));
    }
  else {
    cLogHook *hook=(cLogHook *)(filter->userData);
    if(hook && (hook->bailOut || hook->delay.TimedOut())) DelHook(hook);
    }
}

// -- cLogChain ----------------------------------------------------------------

class cLogChain : public cSimpleItem {
public:
  int cardNum, caid, source, transponder;
  bool softCSA, active, delayed;
  cTimeMs delay;
  cPids pids;
  cSimpleList<cSystem> systems;
  //
  cLogChain(int CardNum, bool soft, int src, int tr);
  void Process(int pid, const unsigned char *data);
  bool Parse(const unsigned char *cat);
  };

cLogChain::cLogChain(int CardNum, bool soft, int src, int tr)
{
  cardNum=CardNum; softCSA=soft; source=src; transponder=tr;
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
    LBPUT("%d: chain caid %04x",cardNum,caid);
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
            sys->CardNum(cardNum);
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
       LBPUT(" [%04x-%02x/%02x/%02x]",pid->pid,pid->sct,pid->mask,pid->mode);
    LBEND();
    if(systems.Count()>0 && pids.Count()>0)
      return true;
    }
  return false;
}

// -- cLogger ------------------------------------------------------------------

class cLogger : public cAction {
private:
  int cardNum;
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
  cPidFilter *AddFilter(int Pid, int Section, int Mask, int Mode, int IdleTime, bool Crc);
  void SetChains(void);
  void ClearChains(void);
  void StartChain(cLogChain *chain);
  void StopChain(cLogChain *chain, bool force);
  void ProcessCat(unsigned char *data, int len);
protected:
  virtual void Process(cPidFilter *filter, unsigned char *data, int len);
public:
  cLogger(int CardNum, bool soft);
  virtual ~cLogger();
  void EcmStatus(const cEcmInfo *ecm, bool on);
  void Up(void);
  void Down(void);
  void PreScan(int src, int tr);
  };

cLogger::cLogger(int CardNum, bool soft)
:cAction("logger",CardNum)
{
  cardNum=CardNum; softCSA=soft;
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
    PRINTF(L_CORE_AUEXTRA,"%d: UP",cardNum);
    catVers=-1;
    catfilt=AddFilter(1,0x01,0xFF,0,0,true);
    up=true;
    }
  Unlock();
}

void cLogger::Down(void)
{
  Lock();
  if(up) {
    PRINTF(L_CORE_AUEXTRA,"%d: DOWN",cardNum);
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
  PRINTF(L_CORE_AUEXTRA,"%d: ecm prgid=%d caid=%04x prov=%.4x %s",cardNum,ecm->prgId,ecm->caId,ecm->provId,on ? "active":"inactive");
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
    PRINTF(L_CORE_AUEXTRA,"%d: restarting delayed chain %04x",cardNum,chain->caid);
  chain->delayed=false;
  if(!chain->active) {
    PRINTF(L_CORE_AU,"%d: starting chain %04x",cardNum,chain->caid);
    chain->active=true;
    for(cPid *pid=chain->pids.First(); pid; pid=chain->pids.Next(pid)) {
      cPidFilter *filter=AddFilter(pid->pid,pid->sct,pid->mask,pid->mode,CHAIN_HOLD/8,false);
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
      PRINTF(L_CORE_AU,"%d: stopping chain %04x",cardNum,chain->caid);
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
      PRINTF(L_CORE_AUEXTRA,"%d: delaying chain %04x",cardNum,chain->caid);
      chain->delayed=true;
      chain->delay.Set(CHAIN_HOLD);
      }
    }
}

cPidFilter *cLogger::AddFilter(int Pid, int Section, int Mask, int Mode, int IdleTime, bool Crc)
{
  cPidFilter *filter=NewFilter(IdleTime);
  if(filter) {
    if(Pid>1) filter->SetBuffSize(KILOBYTE(64));
    filter->Start(Pid,Section,Mask,Mode,Crc);
    PRINTF(L_CORE_AUEXTRA,"%d: added filter pid=0x%.4x sct=0x%.2x/0x%.2x/0x%.2x idle=%d crc=%d",cardNum,Pid,Section,Mask,Mode,IdleTime,Crc);
    }
  else PRINTF(L_GEN_ERROR,"no free slot or filter failed to open for logger %d",cardNum);
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
        chain=new cLogChain(cardNum,softCSA,source,transponder);
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
        PRINTF(L_CORE_AUEXTRA,"%d: got CAT version %02x",cardNum,vers);
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
      else PRINTF(L_CORE_AU,"%d: incomplete section %d != %d",cardNum,len,SCT_LEN(data));
      }
    }
  else {
    cLogChain *chain=(cLogChain *)(filter->userData);
    if(chain && chain->delayed) StopChain(chain,false);
    }
}

// -- cEcmData -----------------------------------------------------------------

#define CACHE_VERS 1

class cEcmData : public cEcmInfo {
public:
  cEcmData(void):cEcmInfo() {}
  cEcmData(cEcmInfo *e):cEcmInfo(e) {}
  virtual cString ToString(bool hide);
  bool Parse(const char *buf);
  };

bool cEcmData::Parse(const char *buf)
{
  char Name[64];
  int nu=0, num, vers=0;
  Name[0]=0;
  if(sscanf(buf,"V%d:%d:%x:%x:%63[^:]:%x/%x:%x:%x/%x:%d:%d/%d%n",
             &vers,&grPrgId,&source,&transponder,Name,&caId,&emmCaId,&provId,
             &ecm_pid,&ecm_table,&rewriterId,&nu,&dataIdx,&num)>=13
     && vers==CACHE_VERS) {
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
  return cString::sprintf("V%d:%d:%x:%x:%s:%x/%x:%x:%x/%x:%d:%s",
                            CACHE_VERS,grPrgId,source,transponder,name,
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
//XXX
PRINTF(L_CORE_PIDS,"SimplyCa entry pidCa=%d",HasPidCaDescr());
DumpCaDescr(L_CORE_PIDS);
//XXX

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

//XXX
PRINTF(L_CORE_PIDS,"SimplyCa exit pidCa=%d",HasPidCaDescr());
DumpCaDescr(L_CORE_PIDS);
//XXX

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
  int cardNum, cwIndex;
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
  cEcmHandler(cCam *Cam, int CardNum, int cwindex);
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

cEcmHandler::cEcmHandler(cCam *Cam, int CardNum, int cwindex)
:cAction("ecmhandler",CardNum)
,failed(32,0)
{
  cam=Cam;
  cardNum=CardNum;
  cwIndex=cwindex;
  sys=0; filter=0; ecm=0; ecmPri=0; mode=-1;
  trigger=ecmUpd=false; triggerMode=-1;
  filterSource=filterTransponder=0; filterCwIndex=-1; filterSid=-1;
  id=bprintf("%d.%d",cardNum,cwindex);
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
    id=bprintf("%d.%d",cardNum,cwindex);
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
      if(IsIdle()) { mode=-1; break; }

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
      filter->Start(ecm->ecm_pid,ecm->ecm_table,0xfe,0,false);
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
    sys->DoLog(dolog!=0); sys->CardNum(cardNum);
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
  if(SysId==0xFFFF) {
    for(cEcmPri *ep=ecmPriList.First(); ep; ep=ecmPriList.Next(ep))
      PRINTF(L_CORE_ECMPROC,"%s: ecmPriList pri=%d ident=%04x caid=%04x pid=%04x",id,ep->pri,ep->sysIdent,ep->ecm->caId,ep->ecm->ecm_pid);
    PRINTF(L_CORE_ECMPROC,"%s: ecmPri list end",id);
    }
}

// -- cCam ---------------------------------------------------------------

cCam::cCam(cScDvbDevice *dev, int CardNum)
{
  device=dev; cardNum=CardNum;
  source=transponder=-1; liveVpid=liveApid=0; logger=0; hookman=0;
  memset(lastCW,0,sizeof(lastCW));
  memset(indexMap,0,sizeof(indexMap));
  memset(splitSid,0,sizeof(splitSid));
}

cCam::~cCam()
{
  handlerList.Clear();
  delete hookman;
  delete logger;
}

bool cCam::IsSoftCSA(bool live)
{
  return device->SoftCSA(live);
}

void cCam::Tune(const cChannel *channel)
{
  cMutexLock lock(this);
  if(source!=channel->Source() || transponder!=channel->Transponder()) {
    source=channel->Source(); transponder=channel->Transponder();
    PRINTF(L_CORE_PIDS,"%d: now tuned to source %x(%s) transponder %x",cardNum,source,*cSource::ToString(source),transponder);
    Stop();
    }
  else PRINTF(L_CORE_PIDS,"%d: tune to same source/transponder",cardNum);
}

void cCam::PostTune(void)
{
  if(ScSetup.PrestartAU) {
    LogStartup();
    if(logger) logger->PreScan(source,transponder);
    }
}

void cCam::SetPid(int type, int pid, bool on)
{
  cMutexLock lock(this);
  int oldA=liveApid, oldV=liveVpid;
  if(type==1) liveVpid=on ? pid:0;
  else if(type==0) liveApid=on ? pid:0;
  else if(liveVpid==pid && on) liveVpid=0;
  else if(liveApid==pid && on) liveApid=0;
  if(oldA!=liveApid || oldV!=liveVpid)
    PRINTF(L_CORE_PIDS,"%d: livepids video=%04x audio=%04x",cardNum,liveVpid,liveApid);
}

void cCam::Stop(void)
{
  cMutexLock lock(this);
  for(cEcmHandler *handler=handlerList.First(); handler; handler=handlerList.Next(handler))
    handler->Stop();
  if(logger) logger->Down();
  if(hookman) hookman->Down();
  memset(splitSid,0,sizeof(splitSid));
}

void cCam::AddPrg(cPrg *prg)
{
  cMutexLock lock(this);
  bool islive=false;
  for(cPrgPid *pid=prg->pids.First(); pid; pid=prg->pids.Next(pid))
    if(pid->pid==liveVpid || pid->pid==liveApid) {
      islive=true;
      break;
      }
  bool needZero=!IsSoftCSA(islive) && (islive || !ScSetup.ConcurrentFF);
  bool noshift=IsSoftCSA(true) || (prg->IsUpdate() && prg->pids.Count()==0);
  PRINTF(L_CORE_PIDS,"%d: %s SID %d (zero=%d noshift=%d)",cardNum,prg->IsUpdate()?"update":"add",prg->sid,needZero,noshift);
  if(prg->pids.Count()>0) {
    LBSTART(L_CORE_PIDS);
    LBPUT("%d: pids",cardNum);
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
      PRINTF(L_CORE_PIDS,"%d: found handler for SID %d (%s idle=%d idx=%d)",cardNum,prg->sid,handler->Id(),handler->IsIdle(),handler->CwIndex());
      prg->source=source;
      prg->transponder=transponder;
      handler->SetPrg(prg);
      }
    }
  else {
    PRINTF(L_CORE_PIDS,"%d: SID %d is handled as splitted",cardNum,prg->sid);
    // first update the splitSid list
    if(prg->pids.Count()==0) { // delete
      for(int i=0; splitSid[i]; i++)
        if(splitSid[i]==prg->sid) {
          memmove(&splitSid[i],&splitSid[i+1],sizeof(splitSid[0])*(MAX_SPLIT_SID-i));
          break;
          }
      PRINTF(L_CORE_PIDS,"%d: deleted from list",cardNum);
      }
    else { // add
      bool has=false;
      int i;
      for(i=0; splitSid[i]; i++) if(splitSid[i]==prg->sid) has=true;
      if(!has) {
        if(i<MAX_SPLIT_SID) {
          splitSid[i]=prg->sid;
          splitSid[i+1]=0;
          PRINTF(L_CORE_PIDS,"%d: added to list",cardNum);
          }
        else PRINTF(L_CORE_PIDS,"%d: split SID list overflow",cardNum);
        }
      }
    LBSTART(L_CORE_PIDS);
    LBPUT("%d: split SID list now:",cardNum);
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
      LBPUT("%d: group %d pids",cardNum,group);
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
        PRINTF(L_CORE_PIDS,"%d: found handler for group-SID %d (%s idle=%d idx=%d)",cardNum,grsid,handler->Id(),handler->IsIdle(),handler->CwIndex());
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
          PRINTF(L_CORE_PIDS,"%d: idle group handler %s idx=%d",cardNum,handler->Id(),handler->CwIndex());
          work.sid=handler->Sid();
          handler->SetPrg(&work);
          }
        }
      }
    }
}

bool cCam::HasPrg(int prg)
{
  cMutexLock lock(this);
  for(cEcmHandler *handler=handlerList.First(); handler; handler=handlerList.Next(handler))
    if(!handler->IsIdle() && handler->Sid()==prg)
      return true;
  return false;
}

char *cCam::CurrentKeyStr(int num)
{
  cMutexLock lock(this);
  cEcmHandler *handler;
  for(handler=handlerList.First(); handler; handler=handlerList.Next(handler))
    if(--num<0) return handler->CurrentKeyStr();
  return 0;
}

bool cCam::Active(bool log)
{
  cMutexLock lock(this);
  for(cEcmHandler *handler=handlerList.First(); handler; handler=handlerList.Next(handler))
    if(!handler->IsIdle()) {
      if(log) PRINTF(L_GEN_INFO,"handler %s on card %d is not idle",handler->Id(),cardNum);
      return true;
      }
  return false;
}

void cCam::HouseKeeping(void)
{
  cMutexLock lock(this);
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
  cMutexLock lock(this);
  if(!logger && ScSetup.AutoUpdate) {
    logger=new cLogger(cardNum,IsSoftCSA(false));
    LogStatsUp();
    }
}

void cCam::LogEcmStatus(const cEcmInfo *ecm, bool on)
{
  cMutexLock lock(this);
  if(on) LogStartup();
  if(logger) logger->EcmStatus(ecm,on);
}

void cCam::AddHook(cLogHook *hook)
{
  cMutexLock lock(this);
  if(!hookman) hookman=new cHookManager(cardNum);
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
    PRINTF(L_CORE_PIDS,"%d: descrambling pid %04x on index %x",cardNum,pid,index);
    if(!device->SetCaPid(&ca_pid))
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
      if(!device->SetCaDescr(&ca_descr,force))
        PRINTF(L_GEN_ERROR,"CA_SET_DESCR failed (%s). Expect a black screen.",strerror(errno));
      }

    if(force || memcmp(&cw[8],&last[8],8)) {
      memcpy(&last[8],&cw[8],8);
      ca_descr.parity=1;
      memcpy(ca_descr.cw,&cw[8],8);
      if(!device->SetCaDescr(&ca_descr,force))
        PRINTF(L_GEN_ERROR,"CA_SET_DESCR failed (%s). Expect a black screen.",strerror(errno));
      }
    }
}

void cCam::DumpAV7110(void)
{
  device->DumpAV7110();
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
  LBPUT("%d: SID=%d zero=%d |",cardNum,sid,needZero);
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
        PRINTF(L_CORE_ECM,"%d: shifting cwindex on non-idle handler.",cardNum);
      if(zerohandler) {
        if(!zerohandler->IsIdle())
          PRINTF(L_CORE_ECM,"%d: shifting non-idle zero handler. This shouldn't happen!",cardNum);
        zerohandler->ShiftCwIndex(sidhandler->CwIndex());
        sidhandler->ShiftCwIndex(0);
        }
      else if(indexMap[0]==0) {
        indexMap[0]=1;
        indexMap[sidhandler->CwIndex()]=0;
        sidhandler->ShiftCwIndex(0);
        }
      else PRINTF(L_CORE_ECM,"%d: zero index not free.",cardNum);
      }

    if(!needZero && sidhandler->CwIndex()==0 && !noshift) {
      if(!sidhandler->IsIdle())
        PRINTF(L_CORE_ECM,"%d: shifting cwindex on non-idle handler.",cardNum);
      int idx=GetFreeIndex();
      if(idx>=0) {
        indexMap[idx]=1;
        sidhandler->ShiftCwIndex(idx);
        indexMap[0]=0;
        }
      else PRINTF(L_CORE_ECM,"%d: no free cwindex. Can't free zero index.",cardNum);
      }

    return sidhandler;
    }

  if(needZero && zerohandler) {
    if(!zerohandler->IsIdle())
      PRINTF(L_CORE_ECM,"%d: changing SID on non-idle zero handler. This shouldn't happen!",cardNum);
    return zerohandler;
    }
  
  if(idlehandler) {
    if(needZero && idlehandler->CwIndex()!=0 && !noshift) {
      if(indexMap[0]==0) {
        indexMap[0]=1;
        indexMap[idlehandler->CwIndex()]=0;
        idlehandler->ShiftCwIndex(0);
        }
      else PRINTF(L_CORE_ECM,"%d: zero index not free. (2)",cardNum);
      }
    if(!needZero && idlehandler->CwIndex()==0 && !noshift) {
      int idx=GetFreeIndex();
      if(idx>=0) {
        indexMap[idx]=1;
        idlehandler->ShiftCwIndex(idx);
        indexMap[0]=0;
        }
      else PRINTF(L_CORE_ECM,"%d: no free cwindex. Can't free zero index. (2)",cardNum);
      }
    if((needZero ^ (idlehandler->CwIndex()==0)))
      PRINTF(L_CORE_ECM,"%d: idlehandler index doesn't match needZero",cardNum);
    return idlehandler;
    }

  int idx=GetFreeIndex();
  if(!needZero && idx==0) {
    indexMap[0]=1;
    idx=GetFreeIndex();
    indexMap[0]=0;
    if(idx<0) {
      idx=0;
      PRINTF(L_CORE_ECM,"%d: can't respect !needZero for new handler",cardNum);
      }
    }
  if(idx<0) {
    PRINTF(L_CORE_ECM,"%d: no free cwindex",cardNum);
    return 0;
    }
  indexMap[idx]=1;
  idlehandler=new cEcmHandler(this,cardNum,idx);
  handlerList.Add(idlehandler);
  return idlehandler;
}

void cCam::RemHandler(cEcmHandler *handler)
{
  int idx=handler->CwIndex();
  PRINTF(L_CORE_PIDS,"%d: removing %s on cw index %d",cardNum,handler->Id(),idx);
  handlerList.Del(handler);
  indexMap[idx]=0;
}

#ifndef SASC

// --- cChannelCaids -----------------------------------------------------------

#if APIVERSNUM >= 10500

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
  bool Same(cChannelCaids *ch);
  void HistAdd(unsigned short *hist);
  void Dump(int n);
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

bool cChannelCaids::Same(cChannelCaids *ch)
{
  if(numcaids!=ch->numcaids) return false;
  return memcmp(caids,ch->caids,numcaids*sizeof(caid_t))==0;
}

void cChannelCaids::HistAdd(unsigned short *hist)
{
  for(int i=numcaids-1; i>=0; i--) hist[caids[i]]++;
}

void cChannelCaids::Dump(int n)
{
  LBSTART(L_CORE_CAIDS);
  LBPUT("%d: channel %d/%x/%x",n,prg,source,transponder);
  for(const caid_t *ids=Caids(); *ids; ids++) LBPUT(" %04x",*ids);
  LBEND();
}

// --- cChannelList ------------------------------------------------------------

class cChannelList : public cSimpleList<cChannelCaids> {
private:
  int n;
public:
  cChannelList(int N);
  void Unique(void);
  void CheckIgnore(void);
  int Histo(void);
  void Purge(int caid, bool fullch);
  };

cChannelList::cChannelList(int N)
{
  n=N;
}

void cChannelList::CheckIgnore(void)
{
  for(cChannelCaids *ch=First(); ch; ch=Next(ch)) {
    const caid_t *ids=ch->Caids();
    while(*ids) {
      int pri=0;
      if(overrides.Ignore(ch->Source(),ch->Transponder(),*ids))
        ch->Del(*ids);
      else if(!cSystems::FindIdentBySysId(*ids,false,pri)) {
        for(cChannelCaids *ch2=Next(ch); ch2; ch2=Next(ch2)) ch2->Del(*ids);
        ch->Del(*ids);
        }
      else ids++;
      }
    }
  PRINTF(L_CORE_CAIDS,"%d: after check",n);
  for(cChannelCaids *ch=First(); ch; ch=Next(ch)) ch->Dump(n);
}

void cChannelList::Unique(void)
{
  for(cChannelCaids *ch1=First(); ch1; ch1=Next(ch1)) {
    for(cChannelCaids *ch2=Next(ch1); ch2;) {
      if(ch1->Same(ch2) || ch2->NumCaids()<1) {
        cChannelCaids *t=Next(ch2);
        Del(ch2);
        ch2=t;
        }
      else ch2=Next(ch2);
      }
    }
  if(Count()==1 && First() && First()->NumCaids()<1) Del(First());
  PRINTF(L_CORE_CAIDS,"%d: after unique",n);
  for(cChannelCaids *ch=First(); ch; ch=Next(ch)) ch->Dump(n);
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
    PRINTF(L_CORE_CAIDS,"%d: still left",n);
    for(cChannelCaids *ch=First(); ch; ch=Next(ch)) ch->Dump(n);
    }
}

// -- cCiFrame -----------------------------------------------------------------

#define LEN_OFF 2

class cCiFrame {
private:
  cRingBufferLinear *rb;
  unsigned char *mem;
  int len, alen, glen;
public:
  cCiFrame(void);
  ~cCiFrame();
  void SetRb(cRingBufferLinear *Rb) { rb=Rb; }
  unsigned char *GetBuff(int l);
  void Put(void);
  unsigned char *Get(int &l);
  void Del(void);
  int Avail(void);
  };

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

// -- cScCiAdapter -------------------------------------------------------------

#define CAID_TIME      300000 // time between caid scans
#define TRIGGER_TIME    10000 // min. time between caid scan trigger

#define TDPU_SIZE_INDICATOR 0x80

struct TPDU {
  unsigned char slot;
  unsigned char tcid;
  unsigned char tag;
  unsigned char len;
  unsigned char data[1];
  };

class cScCamSlot;

class cScCiAdapter : public cCiAdapter {
private:
  cDevice *device;
  cCam *cam;
  cMutex ciMutex;
  int cardIndex;
  cRingBufferLinear *rb;
  cScCamSlot *slots[MAX_CI_SLOTS];
  cCiFrame frame;
  //
  cTimeMs caidTimer, triggerTimer;
  int version[MAX_CI_SLOTS];
  caid_t caids[MAX_CI_SLOTS][MAX_CI_SLOT_CAIDS+1];
  int tcid;
  bool rebuildcaids;
  //
  cTimeMs readTimer, writeTimer;
  //
  void BuildCaids(bool force);
protected:
  virtual int Read(unsigned char *Buffer, int MaxLength);
  virtual void Write(const unsigned char *Buffer, int Length);
  virtual bool Reset(int Slot);
  virtual eModuleStatus ModuleStatus(int Slot);
  virtual bool Assign(cDevice *Device, bool Query=false);
public:
  cScCiAdapter(cDevice *Device, int CardIndex, cCam *Cam);
  ~cScCiAdapter();
  void CamStop(void);
  void CamAddPrg(cPrg *prg);
  bool CamSoftCSA(void);
  int GetCaids(int slot, unsigned short *Caids, int max);
  void CaidsChanged(void);
  };

// -- cScCamSlot ---------------------------------------------------------------

#define SLOT_CAID_CHECK 10000
#define SLOT_RESET_TIME 600

class cScCamSlot : public cCamSlot {
private:
  cScCiAdapter *ciadapter;
  unsigned short caids[MAX_CI_SLOT_CAIDS+1];
  int slot, cardIndex, version;
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
  cScCamSlot(cScCiAdapter *ca, int CardIndex, int Slot);
  void Process(const unsigned char *data, int len);
  eModuleStatus Status(void);
  bool Reset(bool log=true);
  cCiFrame *Frame(void) { return &frame; }
  };

cScCamSlot::cScCamSlot(cScCiAdapter *ca, int CardIndex, int Slot)
:cCamSlot(ca)
,checkTimer(-SLOT_CAID_CHECK-1000)
,rb(KILOBYTE(4),5+LEN_OFF,false,"SC-CI slot answer")
{
  ciadapter=ca; cardIndex=CardIndex; slot=Slot;
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
    PRINTF(L_CORE_CI,"%d.%d: status '%s'",cardIndex,slot,stext[status]);
    lastStatus=status;
    }
  return status;
}

bool cScCamSlot::Reset(bool log)
{
  reset=true; resetTimer.Set(SLOT_RESET_TIME);
  rb.Clear();
  if(log) PRINTF(L_CORE_CI,"%d.%d: reset",cardIndex,slot);
  return true;
}

bool cScCamSlot::Check(void)
{
  bool res=false;
  bool dr=ciadapter->CamSoftCSA() || ScSetup.ConcurrentFF>0;
  if(dr!=doReply && !IsDecrypting()) {
    PRINTF(L_CORE_CI,"%d.%d: doReply changed, reset triggered",cardIndex,slot);
    Reset(false);
    doReply=dr;
    }
  if(checkTimer.TimedOut()) {
    if(version!=ciadapter->GetCaids(slot,0,0)) {
      version=ciadapter->GetCaids(slot,caids,MAX_CI_SLOT_CAIDS);
      PRINTF(L_CORE_CI,"%d.%d: now using CAIDs version %d",cardIndex,slot,version);
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
  PRINTF(L_CORE_CI,"%d.%d sending CA info",cardIndex,slot);
}

void cScCamSlot::Process(const unsigned char *data, int len)
{
  const unsigned char *save=data;
  data+=3;
  int dlen=GetLength(data);
  if(dlen>len-(data-save)) {
    PRINTF(L_CORE_CI,"%d.%d TDPU length exceeds data length",cardIndex,slot);
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
    PRINTF(L_CORE_CI,"%d.%d tag length exceeds data length",cardIndex,slot);
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
        LBPUT("%d.%d CA_PMT decoding len=%x lm=%x prg=%d len=%x",cardIndex,slot,dlen,ca_lm,(data[1]<<8)+data[2],ilen);
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
        PRINTF(L_CORE_CI,"%d.%d got CA pmt ciCmd=%d caLm=%d",cardIndex,slot,ci_cmd,ca_lm);
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
            PRINTF(L_CORE_CI,"%d.%d answer to query",cardIndex,slot);
            }
          }
        if(prg->sid!=0) {
          if(ci_cmd==0x04) {
            PRINTF(L_CORE_CI,"%d.%d stop decrypt",cardIndex,slot);
            ciadapter->CamStop();
            }
          if(ci_cmd==0x01 || (ci_cmd==-1 && (ca_lm==0x04 || ca_lm==0x05))) {
            PRINTF(L_CORE_CI,"%d.%d set CAM decrypt (prg %d)",cardIndex,slot,prg->sid);
            ciadapter->CamAddPrg(prg);
            }
          }
        delete prg;
        }
      break;
    }
}

// -- cScCiAdapter -------------------------------------------------------------

cScCiAdapter::cScCiAdapter(cDevice *Device, int CardIndex, cCam *Cam)
{
  device=Device; cardIndex=CardIndex; cam=Cam;
  tcid=0; rebuildcaids=false;
  memset(version,0,sizeof(version));
  memset(slots,0,sizeof(slots));
  SetDescription("SC-CI adapter on device %d",cardIndex);
  rb=new cRingBufferLinear(KILOBYTE(8),6+LEN_OFF,false,"SC-CI adapter read");
  if(rb) {
    rb->SetTimeouts(0,CAM_READ_TIMEOUT);
    frame.SetRb(rb);
/*
    bool spare=true;
    for(int i=0; i<MAX_CI_SLOTS; i++) {
      if(GetCaids(i,0,0)<=0) {
        if(!spare) break;
        spare=false;
        }
      slots[i]=new cScCamSlot(this,cardIndex,i);
      }
*/
    BuildCaids(true);
    slots[0]=new cScCamSlot(this,cardIndex,0);
    Start();
    }
  else PRINTF(L_GEN_ERROR,"failed to create ringbuffer for SC-CI adapter %d.",cardIndex);
}

cScCiAdapter::~cScCiAdapter()
{
  Cancel(3);
  ciMutex.Lock();
  delete rb; rb=0;
  ciMutex.Unlock();
}

void cScCiAdapter::CamStop(void)
{
  if(cam) cam->Stop();
}

void cScCiAdapter::CamAddPrg(cPrg *prg)
{
  if(cam) cam->AddPrg(prg);
}

bool cScCiAdapter::CamSoftCSA(void)
{
  return cam && cam->IsSoftCSA(false);
}

int cScCiAdapter::GetCaids(int slot, unsigned short *Caids, int max)
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

void cScCiAdapter::CaidsChanged(void)
{
  rebuildcaids=true;
}

void cScCiAdapter::BuildCaids(bool force)
{
  if(caidTimer.TimedOut() || force || (rebuildcaids && triggerTimer.TimedOut())) {
    PRINTF(L_CORE_CAIDS,"%d: building caid lists",cardIndex);
    cChannelList list(cardIndex);
    Channels.Lock(false);
    for(cChannel *channel=Channels.First(); channel; channel=Channels.Next(channel)) {
      if(!channel->GroupSep() && channel->Ca()>=CA_ENCRYPTED_MIN && device->ProvidesTransponder(channel)) {
        cChannelCaids *ch=new cChannelCaids(channel);
        if(ch) list.Add(ch);
        }
      }
    Channels.Unlock();
    list.Unique();
    list.CheckIgnore();
    list.Unique();

    int n=0, h;
    caid_t c[MAX_CI_SLOT_CAIDS+1];
    memset(c,0,sizeof(c));
    do {
      if((h=list.Histo())<0) break;
      c[n++]=h;
      LBSTART(L_CORE_CAIDS);
      LBPUT("%d: added %04x caids now",cardIndex,h); for(int i=0; i<n; i++) LBPUT(" %04x",c[i]);
      LBEND();
      list.Purge(h,false);
      } while(n<MAX_CI_SLOT_CAIDS && list.Count()>0);
    c[n]=0;
    if(n==0) PRINTF(L_CORE_CI,"no active CAIDs");
    else if(list.Count()>0) PRINTF(L_GEN_ERROR,"too many CAIDs. You should ignore some CAIDs.");

    ciMutex.Lock();
    if((version[0]==0 && c[0]!=0) || memcmp(caids[0],c,sizeof(caids[0]))) {
      memcpy(caids[0],c,sizeof(caids[0]));
      version[0]++;
      if(version[0]>0) {
        LBSTART(L_CORE_CI);
        LBPUT("card %d, slot %d (v=%2d) caids:",cardIndex,0,version[0]);
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

int cScCiAdapter::Read(unsigned char *Buffer, int MaxLength)
{
  cMutexLock lock(&ciMutex);
  if(cam && rb && Buffer && MaxLength>0) {
    int s;
    unsigned char *data=frame.Get(s);
    if(data) {
      if(s<=MaxLength) memcpy(Buffer,data,s);
      else PRINTF(L_GEN_DEBUG,"internal: sc-ci %d rb frame size exceeded %d",cardIndex,s);
      frame.Del();
      if(Buffer[2]!=0x80 || LOG(L_CORE_CIFULL)) {
        LDUMP(L_CORE_CI,Buffer,s,"%d.%d <-",cardIndex,Buffer[0]);
        readTimer.Set();
        }
      return s;
      }
    }
  else cCondWait::SleepMs(CAM_READ_TIMEOUT);
  if(LOG(L_CORE_CIFULL) && readTimer.Elapsed()>2000) {
    PRINTF(L_CORE_CIFULL,"%d: read heartbeat",cardIndex);
    readTimer.Set();
    }
  return 0;
}

#define TPDU(data,slot)   do { unsigned char *_d=(data); _d[0]=(slot); _d[1]=tcid; } while(0)
#define TAG(data,tag,len) do { unsigned char *_d=(data); _d[0]=(tag); _d[1]=(len); } while(0)
#define SB_TAG(data,sb)   do { unsigned char *_d=(data); _d[0]=0x80; _d[1]=0x02; _d[2]=tcid; _d[3]=(sb); } while(0)

void cScCiAdapter::Write(const unsigned char *buff, int len)
{
  cMutexLock lock(&ciMutex);
  if(cam && buff && len>=5) {
    struct TPDU *tpdu=(struct TPDU *)buff;
    int slot=tpdu->slot;
    cCiFrame *slotframe=slots[slot]->Frame();
    if(buff[2]!=0xA0 || buff[3]>0x01 || LOG(L_CORE_CIFULL))
      LDUMP(L_CORE_CI,buff,len,"%d.%d ->",cardIndex,slot);
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
  else PRINTF(L_CORE_CIFULL,"%d: short write (cam=%d buff=%d len=%d)",cardIndex,cam!=0,buff!=0,len);
}

bool cScCiAdapter::Reset(int Slot)
{
  cMutexLock lock(&ciMutex);
  PRINTF(L_CORE_CI,"%d: reset of slot %d requested",cardIndex,Slot);
  return slots[Slot] ? slots[Slot]->Reset():false;
}

eModuleStatus cScCiAdapter::ModuleStatus(int Slot)
{
  cMutexLock lock(&ciMutex);
  bool enable=ScSetup.CapCheck(cardIndex);
  if(!enable) CamStop();
  return (enable && cam && slots[Slot]) ? slots[Slot]->Status():msNone;
}

bool cScCiAdapter::Assign(cDevice *Device, bool Query)
{
  return Device ? (Device==device) : true;
}

#endif //APIVERSNUM >= 10500

// -- cDeCSA -------------------------------------------------------------------

#define MAX_CSA_PIDS 8192
#define MAX_CSA_IDX  16
#define MAX_STALL_MS 70

#define MAX_REL_WAIT 100 // time to wait if key in used on set
#define MAX_KEY_WAIT 500 // time to wait if key not ready on change

#define FL_EVEN_GOOD 1
#define FL_ODD_GOOD  2
#define FL_ACTIVITY  4

class cDeCSA {
private:
  int cs;
  unsigned char **range, *lastData;
  unsigned char pidmap[MAX_CSA_PIDS];
  void *keys[MAX_CSA_IDX];
  unsigned int even_odd[MAX_CSA_IDX], flags[MAX_CSA_IDX];
  cMutex mutex;
  cCondVar wait;
  cTimeMs stall;
  bool active;
  int cardindex;
  //
  bool GetKeyStruct(int idx);
  void ResetState(void);
public:
  cDeCSA(int CardIndex);
  ~cDeCSA();
  bool Decrypt(unsigned char *data, int len, bool force);
  bool SetDescr(ca_descr_t *ca_descr, bool initial);
  bool SetCaPid(ca_pid_t *ca_pid);
  void SetActive(bool on);
  };

cDeCSA::cDeCSA(int CardIndex)
:stall(MAX_STALL_MS)
{
  cardindex=CardIndex;
  cs=get_suggested_cluster_size();
  PRINTF(L_CORE_CSA,"%d: clustersize=%d rangesize=%d",cardindex,cs,cs*2+5);
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
  PRINTF(L_CORE_CSA,"%d: reset state",cardindex);
  memset(even_odd,0,sizeof(even_odd));
  memset(flags,0,sizeof(flags));
  lastData=0;
}

void cDeCSA::SetActive(bool on)
{
  if(!on && active) ResetState();
  active=on;
  PRINTF(L_CORE_CSA,"%d: set active %s",cardindex,active?"on":"off");
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
        PRINTF(L_CORE_CSA,"%d.%d: %s key in use (%d ms)",cardindex,idx,ca_descr->parity?"odd":"even",MAX_REL_WAIT);
        if(wait.TimedWait(mutex,MAX_REL_WAIT)) PRINTF(L_CORE_CSA,"%d.%d: successfully waited for release",cardindex,idx);
        else PRINTF(L_CORE_CSA,"%d.%d: timed out. setting anyways",cardindex,idx);
        }
      else PRINTF(L_CORE_CSA,"%d.%d: late key set...",cardindex,idx);
      }
    LDUMP(L_CORE_CSA,ca_descr->cw,8,"%d.%d: %4s key set",cardindex,idx,ca_descr->parity?"odd":"even");
    if(ca_descr->parity==0) {
      set_even_control_word(keys[idx],ca_descr->cw);
      if(!CheckNull(ca_descr->cw,8)) flags[idx]|=FL_EVEN_GOOD|FL_ACTIVITY;
      else PRINTF(L_CORE_CSA,"%d.%d: zero even CW",cardindex,idx);
      wait.Broadcast();
      }
    else {
      set_odd_control_word(keys[idx],ca_descr->cw);
      if(!CheckNull(ca_descr->cw,8)) flags[idx]|=FL_ODD_GOOD|FL_ACTIVITY;
      else PRINTF(L_CORE_CSA,"%d.%d: zero odd CW",cardindex,idx);
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
    PRINTF(L_CORE_CSA,"%d.%d: set pid %04x",cardindex,ca_pid->index,ca_pid->pid);
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
      PRINTF(L_CORE_CSA,"%d: garbage in TS buffer",cardindex);
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
          PRINTF(L_CORE_CSA,"%d.%d: change to %s key",cardindex,idx,(ev_od&0x40)?"odd":"even");
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
            PRINTF(L_CORE_CSA,"%d.%d: %s key not ready (%d ms)",cardindex,idx,(ev_od&0x40)?"odd":"even",MAX_KEY_WAIT);
            if(flags[idx]&FL_ACTIVITY) {
              flags[idx]&=~FL_ACTIVITY;
              if(wait.TimedWait(mutex,MAX_KEY_WAIT)) PRINTF(L_CORE_CSA,"%d.%d: successfully waited for key",cardindex,idx);
              else PRINTF(L_CORE_CSA,"%d.%d: timed out. proceeding anyways",cardindex,idx);
              }
            else PRINTF(L_CORE_CSA,"%d.%d: not active. wait skipped",cardindex,idx);
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
  LBPUT("%d: %s-%d-%d : %d-%d-%d stall=%d :: ",
        cardindex,data==lastData?"SAME":"MOVE",(len+(TS_SIZE-1))/TS_SIZE,force,
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
      PRINTF(L_CORE_CSAVERB,"%d: stall timeout -> forced",cardindex);
      force=true;
      }
    else if(stallP<=10 && scanTS>=cs) {
      PRINTF(L_CORE_CSAVERB,"%d: flow factor stall -> forced",cardindex);
      force=true;
      }
    }
  lastData=data;

  if(r>=0) {                          // we have some range
    if(ccs>=cs || force) {
      if(GetKeyStruct(currIdx)) {
        int n=decrypt_packets(keys[currIdx],range);
        PRINTF(L_CORE_CSAVERB,"%d.%d: decrypting ccs=%3d cs=%3d %s -> %3d decrypted",cardindex,currIdx,ccs,cs,ccs>=cs?"OK ":"INC",n);
        if(n>0) {
          stall.Set(MAX_STALL_MS);
          return true;
          }
        }
      }
    else PRINTF(L_CORE_CSAVERB,"%d.%d: incomplete ccs=%3d cs=%3d",cardindex,currIdx,ccs,cs);
    }
  return false;
}

// -- cDeCsaTSBuffer -----------------------------------------------------------

class cDeCsaTSBuffer : public cThread {
private:
  int f;
  int cardIndex, size;
  bool delivered;
  cRingBufferLinear *ringBuffer;
  //
  cDeCSA *decsa;
  bool scActive;
  //
  virtual void Action(void);
public:
  cDeCsaTSBuffer(int File, int Size, int CardIndex, cDeCSA *DeCsa, bool ScActive);
  ~cDeCsaTSBuffer();
  uchar *Get(void);
  void SetActive(bool ScActive);
  };

cDeCsaTSBuffer::cDeCsaTSBuffer(int File, int Size, int CardIndex, cDeCSA *DeCsa, bool ScActive)
{
  SetDescription("TS buffer on device %d", CardIndex);
  f=File; size=Size; cardIndex=CardIndex; decsa=DeCsa;
  delivered=false;
  ringBuffer=new cRingBufferLinear(Size,TS_SIZE,true,"FFdecsa-TS");
  ringBuffer->SetTimeouts(100,100);
  if(decsa) decsa->SetActive(true);
  SetActive(ScActive);
  Start();
}

cDeCsaTSBuffer::~cDeCsaTSBuffer()
{
  Cancel(3);
  if(decsa) decsa->SetActive(false);
  delete ringBuffer;
}

void cDeCsaTSBuffer::SetActive(bool ScActive)
{
  scActive=ScActive;
}

void cDeCsaTSBuffer::Action(void)
{
  if(ringBuffer) {
    bool firstRead=true;
    cPoller Poller(f);
    while(Running()) {
      if(firstRead || Poller.Poll(100)) {
        firstRead=false;
        int r=ringBuffer->Read(f);
        if(r<0 && FATALERRNO) {
          if(errno==EOVERFLOW)
            esyslog("ERROR: driver buffer overflow on device %d",cardIndex);
          else { LOG_ERROR; break; }
          }
        }
      }
    }
}

uchar *cDeCsaTSBuffer::Get(void)
{
  int Count=0;
  if(delivered) { ringBuffer->Del(TS_SIZE); delivered=false; }
  uchar *p=ringBuffer->Get(Count);
  if(p && Count>=TS_SIZE) {
    if(*p!=TS_SYNC_BYTE) {
      for(int i=1; i<Count; i++)
        if(p[i]==TS_SYNC_BYTE &&
           (i+TS_SIZE==Count || (i+TS_SIZE>Count && p[i+TS_SIZE]==TS_SYNC_BYTE)) ) { Count=i; break; }
      ringBuffer->Del(Count);
      esyslog("ERROR: skipped %d bytes to sync on TS packet on device %d",Count,cardIndex);
      return NULL;
      }

    if(scActive && (p[3]&0xC0)) {
      if(decsa) {
        if(!decsa->Decrypt(p,Count,false)) {
          cCondWait::SleepMs(20);
          return NULL;
          }
        }
      else p[3]&=~0xC0; // FF hack
      }

    delivered=true;
    return p;
    }
  return NULL;
}

#endif //SASC

// --- cScDvbDeviceProbe -------------------------------------------------------

#if APIVERSNUM >= 10711
static cScDvbDeviceProbe *scProbe=0;

bool cScDvbDeviceProbe::Probe(int Adapter, int Frontend)
{
  PRINTF(L_GEN_DEBUG,"capturing device %d/%d",Adapter,Frontend);
  int cafd=open(cString::sprintf("%s%d/%s%d",DEV_DVB_ADAPTER,Adapter,DEV_DVB_CA,Frontend),O_RDWR);
  new cScDvbDevice(Adapter,Frontend,cafd);
  return true;
}
#endif

// -- cScDvbDevice -------------------------------------------------------------

int cScDvbDevice::budget=0;

#ifndef SASC

#if APIVERSNUM >= 10711
cScDvbDevice::cScDvbDevice(int Adapter, int Frontend, int cafd)
:cDvbDevice(Adapter,Frontend)
#else
cScDvbDevice::cScDvbDevice(int n, int cafd)
:cDvbDevice(n)
#endif
{
  decsa=0; tsBuffer=0; cam=0; fullts=false;
#if APIVERSNUM >= 10500
  ciadapter=0; hwciadapter=0;
#else
  memset(lrucaid,0,sizeof(lrucaid));
#endif
  fd_ca=cafd; fd_ca2=dup(fd_ca); fd_dvr=-1;
  softcsa=(fd_ca<0);
}

cScDvbDevice::~cScDvbDevice()
{
  DetachAllReceivers();
  Cancel(3);
  EarlyShutdown();
  delete decsa;
  if(fd_ca>=0) close(fd_ca);
#if APIVERSNUM >= 10500
  if(fd_ca2>=0) close(fd_ca2);
#endif
}

void cScDvbDevice::EarlyShutdown(void)
{
#if APIVERSNUM >= 10500
  SetCamSlot(0);
  delete ciadapter; ciadapter=0;
  delete hwciadapter; hwciadapter=0;
#endif
  if(cam) cam->Stop();
  delete cam; cam=0;
}

void cScDvbDevice::LateInit(void)
{
  int n=CardIndex();
  if(DeviceNumber()!=n)
    PRINTF(L_GEN_ERROR,"CardIndex - DeviceNumber mismatch! Put SC plugin first on VDR commandline!");
  if(softcsa) {
    if(HasDecoder()) PRINTF(L_GEN_ERROR,"Card %d is a full-featured card but no ca device found!",n);
    }
  else if(ForceBudget(n)) {
    PRINTF(L_GEN_INFO,"Budget mode forced on card %d",n);
    softcsa=true;
    }
  
#if APIVERSNUM >= 10500
  if(fd_ca2>=0) hwciadapter=cDvbCiAdapter::CreateCiAdapter(this,fd_ca2);
  cam=new cCam(this,n);
  ciadapter=new cScCiAdapter(this,n,cam);
#else
  if(fd_ca2>=0) {
    ciHandler=cCiHandler::CreateCiHandler(fd_ca2);
    if(!ciHandler) close(fd_ca2);
    }
  cam=ScSetup.CapCheck(n) ? new cCam(this,n):0;
#endif
  if(softcsa) {
    decsa=new cDeCSA(n);
    if(IsPrimaryDevice() && HasDecoder()) {
      PRINTF(L_GEN_INFO,"Enabling hybrid full-ts mode on card %d",n);
      fullts=true;
      }
    else PRINTF(L_GEN_INFO,"Using software decryption on card %d",n);
    }
}

void cScDvbDevice::Shutdown(void)
{
  for(int n=cDevice::NumDevices(); --n>=0;) {
    cScDvbDevice *dev=dynamic_cast<cScDvbDevice *>(cDevice::GetDevice(n));
    if(dev) dev->EarlyShutdown();
    }
}

void cScDvbDevice::Startup(void)
{
  if(ScSetup.ForceTransfer)
    SetTransferModeForDolbyDigital(2);
  for(int n=cDevice::NumDevices(); --n>=0;) {
    cScDvbDevice *dev=dynamic_cast<cScDvbDevice *>(cDevice::GetDevice(n));
    if(dev) dev->LateInit();
    }
}

void cScDvbDevice::SetForceBudget(int n)
{
   if(n>=0 && n<MAXDVBDEVICES) budget|=(1<<n);
}

bool cScDvbDevice::ForceBudget(int n)
{
   return budget && (budget&(1<<n));
}

#if APIVERSNUM < 10711
static int *vdr_nci=0, *vdr_ud=0, vdr_save_ud;
#endif

void cScDvbDevice::Capture(void)
{
#if APIVERSNUM >= 10711
  scProbe=new cScDvbDeviceProbe;
#else
/*
  This is an extremly ugly hack to access VDRs device scan parameters, which are
  protected in this context. Heavily dependant on the actual symbol names
  created by the compiler. May fail in any future version!

  To get the actual symbol names of your VDR binary you may use the command:
  objdump -T <path-to-vdr>/vdr | grep -E "(useDevice|nextCardIndex)"
  Insert the symbol names below.
*/
#if __GNUC__ >= 3
  vdr_nci=(int *)dlsym(RTLD_DEFAULT,"_ZN7cDevice13nextCardIndexE");
  vdr_ud =(int *)dlsym(RTLD_DEFAULT,"_ZN7cDevice9useDeviceE");
#else
  vdr_nci=(int *)dlsym(RTLD_DEFAULT,"_7cDevice.nextCardIndex");
  vdr_ud =(int *)dlsym(RTLD_DEFAULT,"_7cDevice.useDevice");
#endif
  if(vdr_nci && vdr_ud) { vdr_save_ud=*vdr_ud; *vdr_ud=1<<30; }
#endif
}

bool cScDvbDevice::Initialize(void)
{
#if APIVERSNUM >= 10711
  delete scProbe; scProbe=0;
  return true;
#else
  if(!vdr_nci || !vdr_ud) {
    PRINTF(L_GEN_ERROR,"Failed to locate VDR symbols. Plugin not operable");
    return false;
    }
  if(NumDevices()>0) {
    PRINTF(L_GEN_ERROR,"Number of devices != 0 on init. Put SC plugin first on VDR commandline! Aborting.");
    return false;
    }
  *vdr_nci=0; *vdr_ud=vdr_save_ud;

  int i, found=0;
  for(i=0; i<MAXDVBDEVICES; i++) {
    if(UseDevice(NextCardIndex())) {
      char name[128];
      DvbName(DEV_DVB_FRONTEND,i,name,sizeof(name));
      if(access(name,F_OK)==0) {
        PRINTF(L_GEN_DEBUG,"probing %s",name);
        int f=open(name,O_RDONLY);
        if(f>=0) {
          close(f);
          PRINTF(L_GEN_DEBUG,"capturing device %d",i);
          new cScDvbDevice(i,DvbOpen(DEV_DVB_CA,i,O_RDWR));
          found++;
          }
        else {
          if(errno!=ENODEV && errno!=EINVAL) PRINTF(L_GEN_ERROR,"open %s failed: %s",name,strerror(errno));
          break;
          }
        }
      else {
        if(errno!=ENOENT) PRINTF(L_GEN_ERROR,"access %s failed: %s",name,strerror(errno));
        break;
        }
      }
    else NextCardIndex(1);
    }
  NextCardIndex(MAXDVBDEVICES-i);
  if(found>0) PRINTF(L_GEN_INFO,"captured %d video device%s",found,found>1 ? "s" : "");
  else PRINTF(L_GEN_INFO,"no DVB device captured");
  return found>0;
#endif
}

#if APIVERSNUM >= 10501
bool cScDvbDevice::HasCi(void)
{
  return ciadapter || hwciadapter;
}
#endif

#if APIVERSNUM >= 10500
bool cScDvbDevice::Ready(void)
{
  return (ciadapter   ? ciadapter->Ready():true) &&
         (hwciadapter ? hwciadapter->Ready():true);
}
#endif

bool cScDvbDevice::SetPid(cPidHandle *Handle, int Type, bool On)
{
  if(cam) cam->SetPid(Type,Handle->pid,On);
  tsMutex.Lock();
  if(tsBuffer) tsBuffer->SetActive(ScActive());
  tsMutex.Unlock();
  return cDvbDevice::SetPid(Handle,Type,On);
}

bool cScDvbDevice::SetChannelDevice(const cChannel *Channel, bool LiveView)
{
#if APIVERSNUM < 10500
  lruMutex.Lock();
  int i=FindLRUPrg(Channel->Source(),Channel->Transponder(),Channel->Sid());
  if(i<0) i=MAX_LRU_CAID-1;
  if(i>0) memmove(&lrucaid[1],&lrucaid[0],sizeof(struct LruCaid)*i);
  for(i=0; i<=MAXCAIDS; i++) if((lrucaid[0].caids[i]=Channel->Ca(i))==0) break;
  lrucaid[0].src=Channel->Source();
  lrucaid[0].tr=Channel->Transponder();
  lrucaid[0].prg=Channel->Sid();
  lruMutex.Unlock();
#endif
  if(cam) cam->Tune(Channel);
  bool ret=cDvbDevice::SetChannelDevice(Channel,LiveView);
  if(ret && cam) cam->PostTune();
  return ret;
}

#if APIVERSNUM < 10500
int cScDvbDevice::ProvidesCa(const cChannel *Channel) const
{
  if(cam && Channel->Ca()>=CA_ENCRYPTED_MIN) {
    int caid;
    for(int j=0; (caid=Channel->Ca(j)); j++)
      if(!overrides.Ignore(Channel->Source(),Channel->Transponder(),caid)) {
        int n=cSystems::CanHandle(caid,!softcsa);
        if(n<0) break;
        if(n>0) return 2;
        }
    }
  return cDvbDevice::ProvidesCa(Channel);
}

bool cScDvbDevice::CiAllowConcurrent(void) const
{
  return softcsa || ScSetup.ConcurrentFF>0;
}

bool cScDvbDevice::GetPrgCaids(int source, int transponder, int prg, caid_t *c)
{
  cMutexLock lock(&lruMutex);
  int i=FindLRUPrg(source,transponder,prg);
  if(i>=0) {
    for(int j=0; j<MAXCAIDS && lrucaid[i].caids[j]; j++) *c++=lrucaid[i].caids[j];
    *c=0;
    return true;
    }
  return false;
}

int cScDvbDevice::FindLRUPrg(int source, int transponder, int prg)
{
  for(int i=0; i<MAX_LRU_CAID; i++)
    if(lrucaid[i].src==source && lrucaid[i].tr==transponder && lrucaid[i].prg==prg) return i;
  return -1;
}

void cScDvbDevice::CiStartDecrypting(void)
{
  if(cam) {
    cSimpleList<cPrg> prgList;
    for(cCiCaProgramData *p=ciProgramList.First(); p; p=ciProgramList.Next(p)) {
      if(p->modified) {
        cPrg *prg=new cPrg(p->programNumber,cam->HasPrg(p->programNumber));
        if(prg) {
          bool haspid=false;
          for(cCiCaPidData *q=p->pidList.First(); q; q=p->pidList.Next(q)) {
            if(q->active) {
              prg->pids.Add(new cPrgPid(q->streamType,q->pid));
              haspid=true;
              }
            }
          if(haspid) {
            caid_t casys[MAXCAIDS+1];
            if(GetPrgCaids(ciSource,ciTransponder,p->programNumber,casys)) {
              unsigned char buff[2048];
              bool streamflag;
              int len=GetCaDescriptors(ciSource,ciTransponder,p->programNumber,casys,sizeof(buff),buff,streamflag);
              if(len>0) prg->caDescr.Set(buff,len);
              }
            }
          prgList.Add(prg);
          }
        p->modified=false;
        }
      }
    for(int loop=1; loop<=2; loop++) // first delete, then add
      for(cPrg *prg=prgList.First(); prg; prg=prgList.Next(prg))
        if((loop==1)!=(prg->pids.Count()>0))
          cam->AddPrg(prg);
    }
  cDvbDevice::CiStartDecrypting();
}
#endif //APIVERSNUM < 10500

bool cScDvbDevice::ScActive(void)
{
#if APIVERSNUM >= 10500
  return dynamic_cast<cScCamSlot *>(CamSlot())!=0;
#else
  return cam && softcsa;
#endif
}

bool cScDvbDevice::OpenDvr(void)
{
  CloseDvr();
#if APIVERSNUM >= 10711
  fd_dvr=DvbOpen(DEV_DVB_DVR,adapter,frontend,O_RDONLY|O_NONBLOCK,true);
#else
  fd_dvr=DvbOpen(DEV_DVB_DVR,CardIndex(),O_RDONLY|O_NONBLOCK,true);
#endif
  if(fd_dvr>=0) {
    tsMutex.Lock();
    tsBuffer=new cDeCsaTSBuffer(fd_dvr,MEGABYTE(4),CardIndex()+1,decsa,ScActive());
    tsMutex.Unlock();
    }
  return fd_dvr>=0;
}

void cScDvbDevice::CloseDvr(void)
{
  tsMutex.Lock();
  delete tsBuffer; tsBuffer=0;
  tsMutex.Unlock();
  if(fd_dvr>=0) { close(fd_dvr); fd_dvr=-1; }
}

bool cScDvbDevice::GetTSPacket(uchar *&Data)
{
  if(tsBuffer) { Data=tsBuffer->Get(); return true; }
  return false;
}

bool cScDvbDevice::SoftCSA(bool live)
{
  return softcsa && (!fullts || !live);
}

void cScDvbDevice::CaidsChanged(void)
{
#if APIVERSNUM >= 10500
  if(ciadapter) ciadapter->CaidsChanged();
  PRINTF(L_CORE_CAIDS,"caid list rebuild triggered");
#endif
}

bool cScDvbDevice::SetCaDescr(ca_descr_t *ca_descr, bool initial)
{
  if(!softcsa || (fullts && ca_descr->index==0)) {
    cMutexLock lock(&cafdMutex);
    return ioctl(fd_ca,CA_SET_DESCR,ca_descr)>=0;
    }
  else if(decsa) return decsa->SetDescr(ca_descr,initial);
  return false;
}

bool cScDvbDevice::SetCaPid(ca_pid_t *ca_pid)
{
  if(!softcsa || (fullts && ca_pid->index==0)) {
    cMutexLock lock(&cafdMutex);
    return ioctl(fd_ca,CA_SET_PID,ca_pid)>=0;
    }
  else if(decsa) return decsa->SetCaPid(ca_pid);
  return false;
}

static unsigned int av7110_read(int fd, unsigned int addr)
{
  ca_pid_t arg;
  arg.pid=addr;
  ioctl(fd,CA_GET_MSG,&arg);
  return arg.index;
}

#if 0
static void av7110_write(int fd, unsigned int addr, unsigned int val)
{
  ca_pid_t arg;
  arg.pid=addr;
  arg.index=val;
  ioctl(fd,CA_SEND_MSG,&arg);
}
#endif

void cScDvbDevice::DumpAV7110(void)
{
  if(LOG(L_CORE_AV7110)) {
#define CODEBASE (0x2e000404+0x1ce00)
    cMutexLock lock(&cafdMutex);
    if(HasDecoder() && lastDump.Elapsed()>20000) {
      lastDump.Set();
      static unsigned int handles=0, hw_handles=0;
      static const unsigned int code[] = {
        0xb5100040,0x4a095a12,0x48094282,0xd00b4b09,0x20000044,
        0x5b1c4294,0xd0033001,0x281cdbf8,0xe001f7fe,0xfd14bc10
        };
      static const unsigned int mask[] = {
        0xffffffff,0xffffffff,0xffffffff,0xffffffff,0xffffffff,
        0xffffffff,0xffffffff,0xffffffff,0xfffff800,0xf800ffff
        };
      if(!handles) {
        handles=1;
        PRINTF(L_CORE_AV7110,"searching handle tables");
        for(int i=0; i<0x2000; i+=4) {
          int j;
          for(j=0; j<20; j+=4) {
            int r=av7110_read(fd_ca,CODEBASE+i+j);
            if((r&mask[j/4])!=(code[j/4]&mask[j/4])) break;
            }
          if(j==20) {
            handles=av7110_read(fd_ca,CODEBASE+i+44);
            hw_handles=av7110_read(fd_ca,CODEBASE+i+52);
            PRINTF(L_CORE_AV7110,"found handles=%08x hw_handles=%08x at 0x%08x",handles,hw_handles,CODEBASE+i);
            if((handles>>16)!=0x2e08 || (hw_handles>>16)!=0x2e08) {
              PRINTF(L_CORE_AV7110,"seems to be invalid");
              }
            break;
            }
          }
        }

      unsigned int hdl=0, hwhdl=0;
      PRINTF(L_CORE_AV7110,"         : 64000080 64000400");
      for(int i=0; i<=31; i++) {
        unsigned int off80 =av7110_read(fd_ca,0x64000080+i*4);
        unsigned int off400=av7110_read(fd_ca,0x64000400+i*8);
        LBSTART(L_CORE_AV7110);
        LBPUT("handle %2d: %08x %08x %s pid=%04x idx=%d",
          i,off80,off400,off80&0x2000?"ACT":"---",off80&0x1fff,(off400&0x1e)>>1);
        if(handles>1 && i<=27) {
          if((i&1)==0) {
            hdl=av7110_read(fd_ca,handles+i*2);
            hwhdl=av7110_read(fd_ca,hw_handles+i*2);
            }
          unsigned int s=((~i)&1)<<4;
          LBPUT(" | %02d hdl=%04x hwfilt=%04x",i,(hdl>>s)&0xffff,(hwhdl>>s)&0xffff);
          }
        LBEND();
        }
      }
    }
}

#else //SASC

cScDvbDevice::cScDvbDevice(int n, int cafd)
:cDvbDevice(n)
{
  softcsa=false;
  cam=new cCam(this,n);
}

cScDvbDevice::~cScDvbDevice()
{
  delete cam;
}

void cScDvbDevice::Shutdown(void)
{}

void cScDvbDevice::Startup(void)
{}

void cScDvbDevice::SetForceBudget(int n)
{}

bool cScDvbDevice::ForceBudget(int n)
{
   return true;
}

void cScDvbDevice::Capture(void)
{}

bool cScDvbDevice::Initialize(void)
{
  return true;
}

void cScDvbDevice::CaidsChanged(void)
{}

bool cScDvbDevice::SoftCSA(bool live)
{
  return softcsa && !live;
}

bool cScDvbDevice::SetCaDescr(ca_descr_t *ca_descr, bool initial)
{
  return false;
}

bool cScDvbDevice::SetCaPid(ca_pid_t *ca_pid)
{
  return false;
}

void cScDvbDevice::DumpAV7110(void)
{}

#endif //SASC
