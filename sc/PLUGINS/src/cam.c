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

#include <ffdecsawrapper/channels.h>
#include <ffdecsawrapper/thread.h>

#include "cam.h"
#include "device.h"
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

// -- cCam ---------------------------------------------------------------

cCam::cCam(cScDevice *dev, int CardNum)
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
  bool stop = false;
  cMutexLock lock(this);
  if(source!=channel->Source() || transponder!=channel->Transponder()) {
    source=channel->Source(); transponder=channel->Transponder();
    PRINTF(L_CORE_PIDS,"%d: now tuned to source %x(%s) transponder %x",cardNum,source,*cSource::ToString(source),transponder);
    stop = true;
    } else {
    PRINTF(L_CORE_PIDS,"%d: tune to same source/transponder",cardNum);
    }

  if (stop) {
    Stop();
  }
}

void cCam::PostTune(void)
{
  cMutexLock lock(this);
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
  if(!logger && ScSetup.AutoUpdate) {
    logger=new cLogger(cardNum,IsSoftCSA(false));
    LogStatsUp();
    }
}

void cCam::LogEcmStatus(const cEcmInfo *ecm, bool on)
{
  //cMutexLock lock(this);
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
