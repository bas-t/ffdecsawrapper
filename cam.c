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
#include <unistd.h>
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
#define ECM_UPD_TIME   120000 // delay between ECM data updates
#define MAX_ECM_IDLE   300000 // delay before an idle handler can be removed
#define MAX_ECM_HOLD    15000 // delay before an idle handler stops processing
#define CAID_TIME      300000 // time between caid scans

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
  int cardNum, caid;
  bool softCSA, active, delayed;
  cTimeMs delay;
  cPids pids;
  cSimpleList<cSystem> systems;
  //
  cLogChain(int CardNum, bool soft);
  void Process(int pid, unsigned char *data);
  bool Parse(const unsigned char *cat);
  };

cLogChain::cLogChain(int CardNum, bool soft)
{
  cardNum=CardNum; softCSA=soft; active=delayed=false;
}

void cLogChain::Process(int pid, unsigned char *data)
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
        sys->ParseCAT(&pids,cat);
      }
    else {
      LBPUT(" ->");
      int Pri=0;
      while((sys=cSystems::FindBySysId(caid,!softCSA,Pri))) {
        Pri=sys->Pri();
        if(sys->HasLogger()) {
          sys->CardNum(cardNum);
          sys->ParseCAT(&pids,cat);
          systems.Add(sys);
          LBPUT(" %s(%d)",sys->Name(),sys->Pri());
          }
        else
          delete sys;
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
protected:
  virtual void Process(cPidFilter *filter, unsigned char *data, int len);
public:
  cLogger(int CardNum, bool soft);
  virtual ~cLogger();
  void EcmStatus(const cEcmInfo *ecm, bool on);
  void Up(void);
  void Down(void);
  void PreScan(void);
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

void cLogger::PreScan(void)
{
  Lock();
  prescan=pmStart; Up();
  Unlock();
}

void cLogger::EcmStatus(const cEcmInfo *ecm, bool on)
{
  Lock();
  PRINTF(L_CORE_AUEXTRA,"%d: ecm prgid=%d caid=%04x prov=%.4x %s",cardNum,ecm->prgId,ecm->caId,ecm->provId,on ? "active":"inactive");
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
    if(Pid>1) filter->SetBuffSize(32768);
    filter->Start(Pid,Section,Mask,Mode,Crc);
    PRINTF(L_CORE_AUEXTRA,"%d: added filter pid=0x%.4x sct=0x%.2x/0x%.2x/0x%.2x idle=%d crc=%d",cardNum,Pid,Section,Mask,Mode,IdleTime,Crc);
    }
  else PRINTF(L_GEN_ERROR,"no free slot or filter failed to open for logger %d",cardNum);
  return filter;
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
        for(int i=8; i<len-4; i+=data[i+1]+2) {
          if(data[i]==0x09) {
            int caid=WORD(data,i+2,0xFFFF);
            cLogChain *chain;
            for(chain=chains.First(); chain; chain=chains.Next(chain))
              if(chain->caid==caid) break;
            if(chain)
              chain->Parse(&data[i]);
            else {
              chain=new cLogChain(cardNum,softCSA);
              if(chain->Parse(&data[i]))
                chains.Add(chain);
              else
                delete chain;
              }
            }
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

class cEcmData : public cEcmInfo {
private:
  bool del;
public:
  cEcmData(void);
  cEcmData(cEcmInfo *e);
  bool Save(FILE *f);
  bool Parse(const char *buf);
  void Delete(void) { del=true; }
  bool IsDeleted(void) const { return del; }
  };

cEcmData::cEcmData(void)
:cEcmInfo()
{
  del=false;
}

cEcmData::cEcmData(cEcmInfo *e)
:cEcmInfo(e)
{
  del=false;
}

bool cEcmData::Parse(const char *buf)
{
  char Name[64];
  int nu=0, num;
  Name[0]=0;
  if(sscanf(buf,"%d:%x:%x:%63[^:]:%x/%x:%x:%x/%x:%d%n",&prgId,&source,&transponder,Name,&caId,&emmCaId,&provId,&ecm_pid,&ecm_table,&nu,&num)>=9) {
    SetName(Name);
    const char *line=buf+num;
    if(nu>0 && *line++==':') {
      unsigned char *dat=AUTOMEM(nu);
      if(GetHex(line,dat,nu,true)==nu) AddData(dat,nu);
      }
    return true;
    }
  return false;
}

bool cEcmData::Save(FILE *f)
{
  fprintf(f,"%d:%x:%x:%s:%x/%x:%x:%x/%x",prgId,source,transponder,name,caId,emmCaId,provId,ecm_pid,ecm_table);
  if(data) {
    char *str=AUTOARRAY(char,dataLen*2+2);
    fprintf(f,":%d:%s\n",dataLen,HexStr(str,data,dataLen));
    }
  else
    fprintf(f,":0\n");
  return ferror(f)==0;
}

// -- cEcmCache ----------------------------------------------------------------

cEcmCache ecmcache;

cEcmCache::cEcmCache(void)
:cLoader("ECM")
{}

void cEcmCache::New(cEcmInfo *e)
{
  Lock();
  cEcmData *dat;
  if(!(dat=Exists(e))) {
    dat=new cEcmData(e);
    Add(dat);
    Modified();
    PRINTF(L_CORE_ECM,"cache add prgId=%d source=%x transponder=%x ecm=%x/%x",e->prgId,e->source,e->transponder,e->ecm_pid,e->ecm_table);
    }
  else {
    if(strcasecmp(e->name,dat->name)) {
      dat->SetName(e->name);
      Modified();
      }
    if(dat->Update(e))
      Modified();
    }
  e->SetCached();
  Unlock();
}

cEcmData *cEcmCache::Exists(cEcmInfo *e)
{
  for(cEcmData *dat=First(); dat; dat=Next(dat))
    if(!dat->IsDeleted() && dat->Compare(e)) return dat;
  return 0;
}

int cEcmCache::GetCached(cSimpleList<cEcmInfo> *list, int sid, int Source, int Transponder)
{
  int n=0;
  list->Clear();
  Lock();
  for(cEcmData *dat=First(); dat; dat=Next(dat)) {
    if(!dat->IsDeleted() && dat->prgId==sid && dat->source==Source && dat->transponder==Transponder) {
      cEcmInfo *e=new cEcmInfo(dat);
      if(e) {
        PRINTF(L_CORE_ECM,"from cache: system %s (%04x) id %04x with ecm %x/%x",e->name,e->caId,e->provId,e->ecm_pid,e->ecm_table);
        e->SetCached();
        list->Add(e);
        n++;
        }
      }
    }
  Unlock();
  return n;
}

void cEcmCache::Delete(cEcmInfo *e)
{
  Lock();
  cEcmData *dat=Exists(e);
  if(dat) {
    dat->Delete();
    Modified();
    PRINTF(L_CORE_ECM,"invalidated cached prgId=%d source=%x transponder=%x ecm=%x/%x",dat->prgId,dat->source,dat->transponder,dat->ecm_pid,dat->ecm_table);
    }
  Unlock();
}

void cEcmCache::Flush(void)
{
  Lock();
  Clear();
  Modified();
  PRINTF(L_CORE_ECM,"cache flushed");
  Unlock();    
}

void cEcmCache::Load(void)
{
  Lock();
  Clear();
  Unlock();
}

bool cEcmCache::Save(FILE *f)
{
  bool res=true;
  Lock();
  for(cEcmData *dat=First(); dat;) {
    if(!dat->IsDeleted()) {
      if(!dat->Save(f)) { res=false; break; }
      dat=Next(dat);
      }
    else {
      cEcmData *n=Next(dat);
      Del(dat);
      dat=n;
      }
    }
  Modified(!res);
  Unlock();
  return res;
}

bool cEcmCache::ParseLine(const char *line, bool fromCache)
{
  bool res=false;
  cEcmData *dat=new cEcmData;
  if(dat && dat->Parse(line)) {
    if(!Exists(dat)) { Add(dat); dat=0; }
    res=true;
    }
  delete dat;
  return res;
}

// -- cEcmSys ------------------------------------------------------------------

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
  int sid;
  cSimpleList<cPrgPid> pids;
  //
  cSystem *sys;
  cPidFilter *filter;
  int filterCwIndex, filterSource, filterTransponder, filterSid;
  unsigned char lastCw[16];
  bool sync, noKey, trigger;
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
  cTimeMs ecmUpdTime;
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
  void SetPrg(cPrg *prg);
  void ShiftCwIndex(int cwindex);
  char *CurrentKeyStr(void) const;
  bool IsRemoveable(void);
  bool IsIdle(void);
  int Sid(void) const { return sid; }
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
  sys=0; filter=0; ecm=0; ecmPri=0; mode=-1; sid=-1;
  trigger=false; triggerMode=-1;
  filterSource=filterTransponder=0; filterCwIndex=-1; filterSid=-1;
  asprintf(&id,"%d.%d",cardNum,cwindex);
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
  int n=pids.Count();
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
  if(!IsIdle() || sid!=-1) {
    PRINTF(L_CORE_ECM,"%s: stop",id);
    sid=-1;
    idleTime.Set();
    pids.Clear();
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
    asprintf(&id,"%d.%d",cardNum,cwindex);
    dataMutex.Lock();
    trigger=true;
    cwIndex=cwindex;
    for(cPrgPid *pid=pids.First(); pid; pid=pids.Next(pid))
      cam->SetCWIndex(pid->Pid(),cwIndex);
    dataMutex.Unlock();
    if(filter) filter->Wakeup();
    }
}

void cEcmHandler::SetPrg(cPrg *prg)
{
  dataMutex.Lock();
  bool wasIdle=IsIdle();
  if(prg->Prg()!=sid) {
    PRINTF(L_CORE_ECM,"%s: setting new SID %d",id,prg->Prg());
    sid=prg->Prg();
    idleTime.Set();
    pids.Clear();
    trigger=true;
    }
  LBSTART(L_CORE_PIDS);
  LBPUT("%s: pids on entry",id);
  for(cPrgPid *pid=pids.First(); pid; pid=pids.Next(pid))
    LBPUT(" %s=%04x",TYPENAME(pid->Type()),pid->Pid());
  LBEND();

  for(cPrgPid *pid=pids.First(); pid;) {
    cPrgPid *npid;
    for(npid=prg->pids.First(); npid; npid=prg->pids.Next(npid)) {
      if(pid->Pid()==npid->Pid()) {
        npid->Proc(true);
        break;
        }
      }
    if(!npid) {
      npid=pids.Next(pid);
      pids.Del(pid);
      pid=npid;
      }
    else pid=pids.Next(pid);
    }
  LBSTART(L_CORE_PIDS);
  LBPUT("%s: pids after delete",id);
  for(cPrgPid *pid=pids.First(); pid; pid=pids.Next(pid))
    LBPUT(" %s=%04x",TYPENAME(pid->Type()),pid->Pid());
  LBEND();
  for(cPrgPid *npid=prg->pids.First(); npid; npid=prg->pids.Next(npid)) {
    if(!npid->Proc()) {
      cPrgPid *pid=new cPrgPid(npid->Type(),npid->Pid());
      pids.Add(pid);
      cam->SetCWIndex(pid->Pid(),cwIndex);
      }
    }
  LBSTART(L_CORE_PIDS);
  LBPUT("%s: pids after add",id);
  for(cPrgPid *pid=pids.First(); pid; pid=pids.Next(pid))
    LBPUT(" %s=%04x",TYPENAME(pid->Type()),pid->Pid());
  LBEND();
  if(!IsIdle()) {
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
      id,filterSid,sid,filterCwIndex,cwIndex,mode,triggerMode,(mode==3 && sync)?"sync":"-");
    trigger=false;
    if(filterSid!=sid) {
      filterSid=sid;
      filterSource=cam->Source();
      filterTransponder=cam->Transponder();
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
            PRINTF(L_CORE_ECMPROC,"%s: check result %d\n",id,n);
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
          if(!cam->IsSoftCSA())
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
  while((ident=cSystems::FindIdentBySysId(n->caId,!cam->IsSoftCSA(),pri))>0) {
    cEcmPri *ep=new cEcmPri;
    if(ep) {
      ep->ecm=n; 
      ep->pri=pri;
      ep->sysIdent=ident;
      if(n->Cached() && (!ScSetup.LocalPriority || pri!=-15)) ep->pri+=20;

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
  if(!ecm->Data() || ecmUpdTime.TimedOut()) {
    bool log=dolog;
    dolog=(sys && sys->NeedsData() && ecm->Data()==0);
    if(dolog) PRINTF(L_CORE_ECM,"%s: try to update ecm extra data",id);
    ParseCAInfo(ecm->caId);
    ecmUpdTime.Set(ECM_UPD_TIME);
    dolog=log;
    if(!ecm->Data()) return false;
    }
  return true;
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
      StopEcm(); ecmUpdTime.Set();
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
  cLoaders::SaveCache();
}

void cEcmHandler::EcmFail(void)
{
  ecm->Fail(true);
}

void cEcmHandler::ParseCAInfo(int SysId)
{
  unsigned char buff[2048];
  caid_t casys[MAXCAIDS+1];
  if(SysId==0xFFFF) {
    if(!cam->GetPrgCaids(filterSource,filterTransponder,filterSid,casys)) {
      PRINTF(L_CORE_ECM,"%s: no CAIDs for SID %d",id,sid);
      return;
      }
    }
  else {
    casys[0]=SysId;
    casys[1]=0;
    }
  bool streamFlag;
  int len=GetCaDescriptors(filterSource,filterTransponder,filterSid,casys,sizeof(buff),buff,streamFlag);
  if(len>0) {
    if(dolog) PRINTF(L_CORE_ECM,"%s: got CaDescriptors for SID %d (len=%d)",id,sid,len);
    HEXDUMP(L_HEX_PMT,buff,len,"PMT");
    for(int index=0; index<len; index+=buff[index+1]+2) {
      if(buff[index]==0x09) {
        if(dolog) LDUMP(L_CORE_ECM,&buff[index+2],buff[index+1],"%s: descriptor",id);
        int sysId=WORD(buff,index+2,0xFFFF);
        int sysPri=0;
        cSystem *sys;
        while((sys=cSystems::FindBySysId(sysId,!cam->IsSoftCSA(),sysPri))) {
          sysPri=sys->Pri();
          cSimpleList<cEcmInfo> ecms;
          sys->ParseCADescriptor(&ecms,sysId,&buff[index+2],buff[index+1]);
          delete sys;
          if(ecms.Count()) {
            cEcmInfo *n;
            while((n=ecms.First())) {
              ecms.Del(n,false);
              LBSTARTF(L_CORE_ECM);
              if(dolog) LBPUT("%s: found %04x (%s) id %04x with ecm %x ",id,n->caId,n->name,n->provId,n->ecm_pid);
              cEcmInfo *e=ecmList.First();
              while(e) {
                if(e->ecm_pid==n->ecm_pid) {
                  if(e->caId==n->caId && e->provId==n->provId) {
                    if(n->Data()) {
                      if(e->Update(n) && dolog) LBPUT("(updated) ");
                      }
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
                n->SetSource(sid,filterSource,filterTransponder);
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
  else if(len<0)
    PRINTF(L_CORE_ECM,"%s: CA parse buffer overflow",id);
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
}

cCam::~cCam()
{
  handlerList.Clear();
  delete hookman;
  delete logger;
}

bool cCam::IsSoftCSA(void)
{
  return device->SoftCSA();
}

void cCam::Tune(const cChannel *channel)
{
  cMutexLock lock(this);
  if(source!=channel->Source() || transponder!=channel->Transponder()) {
    source=channel->Source(); transponder=channel->Transponder();
    PRINTF(L_CORE_PIDS,"%d: now tuned to source %x transponder %x",cardNum,source,transponder);
    Stop();
    }
  else PRINTF(L_CORE_PIDS,"%d: tune to same source/transponder",cardNum);
}

void cCam::PostTune(void)
{
  if(ScSetup.PrestartAU) {
    LogStartup();
    if(logger) logger->PreScan();
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
}

void cCam::AddPrg(cPrg *prg)
{
  cMutexLock lock(this);
  bool islive=false;
  for(cPrgPid *pid=prg->pids.First(); pid; pid=prg->pids.Next(pid))
    if(pid->Pid()==liveVpid || pid->Pid()==liveApid) {
      islive=true;
      break;
      }
  bool needZero=!IsSoftCSA() && (islive || !ScSetup.ConcurrentFF);
  bool noshift=IsSoftCSA() || (prg->IsUpdate() && prg->pids.Count()==0);
  PRINTF(L_CORE_PIDS,"%d: %s SID %d (zero=%d noshift=%d)",cardNum,prg->IsUpdate()?"update":"add",prg->Prg(),needZero,noshift);
  if(prg->pids.Count()>0) {
    LBSTART(L_CORE_PIDS);
    LBPUT("%d: pids",cardNum);
    for(cPrgPid *pid=prg->pids.First(); pid; pid=prg->pids.Next(pid))
      LBPUT(" %s=%04x",TYPENAME(pid->Type()),pid->Pid());
    LBEND();
    }
  cEcmHandler *handler=GetHandler(prg->Prg(),needZero,noshift);
  if(handler) {
    PRINTF(L_CORE_PIDS,"%d: found handler for SID %d (%s idle=%d idx=%d)",cardNum,prg->Prg(),handler->Id(),handler->IsIdle(),handler->CwIndex());
    handler->SetPrg(prg);
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

bool cCam::Active(void)
{
  cMutexLock lock(this);
  for(cEcmHandler *handler=handlerList.First(); handler; handler=handlerList.Next(handler))
    if(!handler->IsIdle()) {
      PRINTF(L_GEN_INFO,"handler %s on card %d is not idle",handler->Id(),cardNum);
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

bool cCam::GetPrgCaids(int source, int transponder, int prg, caid_t *c)
{
  return device->GetPrgCaids(source,transponder,prg,c);
}

void cCam::LogStartup(void)
{
  cMutexLock lock(this);
  if(!logger && ScSetup.AutoUpdate) {
    logger=new cLogger(cardNum,IsSoftCSA());
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
  void Purge(int caid);
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
      if(!cSystems::FindIdentBySysId(*ids,false,pri)) {
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

void cChannelList::Purge(int caid)
{
  for(cChannelCaids *ch=First(); ch;) {
    if(ch->NumCaids()<=0 || ch->HasCaid(caid)) {
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

// -- cScCiAdapter -------------------------------------------------------------

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
  //
  cTimeMs caidTimer;
  int version[MAX_CI_SLOTS];
  caid_t caids[MAX_CI_SLOTS][MAX_CI_SLOT_CAIDS+1];
  int tcid;
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
  };

// -- cScCamSlot ---------------------------------------------------------------

#define SLOT_CAID_CHECK 10000
#define SLOT_RESET_TIME 600

class cScCamSlot : public cCamSlot, public cRingBufferLinear {
private:
  cScCiAdapter *ciadapter;
  unsigned short caids[MAX_CI_SLOT_CAIDS+1];
  int slot, cardIndex, version;
  cTimeMs checkTimer;
  bool reset, doReply;
  cTimeMs resetTimer;
  eModuleStatus lastStatus;
  //
  int GetLength(const unsigned char * &data);
  void CaInfo(unsigned char *b, int tcid, int cid);
  bool Check(void);
public:
  cScCamSlot(cScCiAdapter *ca, int CardIndex, int Slot);
  void Process(const unsigned char *data, int len);
  eModuleStatus Status(void);
  bool Reset(bool log=true);
  };

cScCamSlot::cScCamSlot(cScCiAdapter *ca, int CardIndex, int Slot)
:cCamSlot(ca)
,cRingBufferLinear(KILOBYTE(2),5+1,false,"SC-CI slot answer")
,checkTimer(-SLOT_CAID_CHECK-1000)
{
  ciadapter=ca; cardIndex=CardIndex; slot=Slot;
  version=0; caids[0]=0; doReply=false; lastStatus=msReset;
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
  Clear();
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
  if(len&0x80) {
    int i;
    for(i=len&~0x80, len=0; i>0; i--) len=(len<<8) + *data++;
    }
  return len;
}

void cScCamSlot::CaInfo(unsigned char *b, int tcid, int cid)
{
  b[0]=0xa0; b[2]=tcid;
  b[3]=0x90;
  b[4]=0x02; b[5]=cid<<8; b[6]=cid&0xff;
  b[7]=0x9f; b[8]=0x80;   b[9]=0x31; // AOT_CA_INFO
  int l=0;
  for(int i=0; caids[i]; i++) {
    b[l+11]=caids[i]>>8;
    b[l+12]=caids[i]&0xff;
    l+=2;
    }
  b[10]=l; b[1]=l+9; b[-1]=l+11; Put(b-1,l+12);
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

  unsigned char a[128], *b=&a[1];
  if(Check()) CaInfo(b,tcid,0x01);

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
      CaInfo(b,tcid,cid);
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
            LBPUT(" ci_cmd(S)=%x",ci_cmd);
            }
          data+=ilen; dlen-=ilen;
          }
        LBEND();
        PRINTF(L_CORE_CI,"%d.%d got CA pmt ciCmd=%d caLm=%d",cardIndex,slot,ci_cmd,ca_lm);
        if(doReply && (ci_cmd==0x03 || (ci_cmd==0x01 && ca_lm==0x03))) {
          b[0]=0xa0; b[2]=tcid;
          b[3]=0x90;
          b[4]=0x02; b[5]=cid<<8; b[6]=cid&0xff;
          b[7]=0x9f; b[8]=0x80; b[9]=0x33; // AOT_CA_PMT_REPLY
          b[11]=prg->Prg()<<8;
          b[12]=prg->Prg()&0xff;
          b[13]=0x00;
          b[14]=0x81; 	// CA_ENABLE
          b[10]=4; b[1]=4+9; a[0]=4+11; Put(a,4+12);
          PRINTF(L_CORE_CI,"%d.%d answer to query",cardIndex,slot);
          }
        if(prg->Prg()!=0) {
          if(ci_cmd==0x04) {
            PRINTF(L_CORE_CI,"%d.%d stop decrypt",cardIndex,slot);
            ciadapter->CamStop();
            }
          if(ci_cmd==0x01 || (ci_cmd==-1 && (ca_lm==0x04 || ca_lm==0x05))) {
            PRINTF(L_CORE_CI,"%d.%d set CAM decrypt (prg %d)",cardIndex,slot,prg->Prg());
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
  tcid=0;
  memset(version,0,sizeof(version));
  memset(slots,0,sizeof(slots));
  SetDescription("SC-CI adapter on device %d",cardIndex);
  rb=new cRingBufferLinear(KILOBYTE(5),6+1,false,"SC-CI adapter read");
  if(rb) {
    rb->SetTimeouts(0,CAM_READ_TIMEOUT);
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
  return cam && cam->IsSoftCSA();
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

void cScCiAdapter::BuildCaids(bool force)
{
  if(caidTimer.TimedOut() || force) {
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
      list.Purge(h);
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
    }
}

int cScCiAdapter::Read(unsigned char *Buffer, int MaxLength)
{
  cMutexLock lock(&ciMutex);
  if(cam && rb && Buffer && MaxLength>0) {
    int c;
    unsigned char *data=rb->Get(c);
    if(data) {
      int s=data[0];
      if(c>=s+1) {
        c=s>MaxLength ? MaxLength : s;
        memcpy(Buffer,&data[1],c);
        if(c<s) { data[c]=s-c; rb->Del(c); }
        else rb->Del(c+1);
        if(Buffer[2]!=0x80 || LOG(L_CORE_CIFULL)) {
          LDUMP(L_CORE_CI,Buffer,c,"%d.%d <-",cardIndex,Buffer[0]);
          readTimer.Set();
          }
        return c;
        }
      else {
        LDUMP(L_GEN_DEBUG,data,c,"internal: sc-ci %d rb frame sync got=%d avail=%d -",cardIndex,c,rb->Available());
        rb->Clear();
        }
      }
    }
  else cCondWait::SleepMs(CAM_READ_TIMEOUT);
  if(LOG(L_CORE_CIFULL) && readTimer.Elapsed()>2000) {
    PRINTF(L_CORE_CIFULL,"%d: read heartbeat",cardIndex);
    readTimer.Set();
    }
  return 0;
}

#define TPDU(data,slot) do { unsigned char *_d=(data); _d[0]=(slot); _d[1]=tcid; } while(0)
#define TAG(data,tag,len) do { unsigned char *_d=(data); _d[0]=(tag); _d[1]=(len); } while(0)
#define SB_TAG(data,sb) do { unsigned char *_d=(data); _d[0]=0x80; _d[1]=0x02; _d[2]=tcid; _d[3]=(sb); } while(0)
#define PUT_TAG(data,len) do { unsigned char *_d=(data)-1; int _l=(len); _d[0]=_l; if(rb) rb->Put(_d,_l+1); } while(0)

void cScCiAdapter::Write(const unsigned char *buff, int len)
{
  cMutexLock lock(&ciMutex);
  if(cam && buff && len>=5) {
    unsigned char a[128], *b=&a[1];
    struct TPDU *tpdu=(struct TPDU *)buff;
    int slot=tpdu->slot;
    if(buff[2]!=0xA0 || buff[3]>0x01 || LOG(L_CORE_CIFULL))
      LDUMP(L_CORE_CI,buff,len,"%d.%d ->",cardIndex,slot);
    if(slots[slot]) {
      switch(tpdu->tag) {
        case 0x81: // T_RCV
          {
          TPDU(b,slot);
          int l=2, c;
          unsigned char *d=slots[slot]->Get(c);
          if(d) {
            int s=d[0];
            if(c>=s) {
              memcpy(&b[l],&d[1],s);
              l+=s;
              slots[slot]->Del(s+1);
              }
            else slots[slot]->Del(c);
            }
          SB_TAG(&b[l],0x00);
          PUT_TAG(b,l+4);
          break;
          }
        case 0x82: // T_CREATE_TC
          tcid=tpdu->data[0];
          TPDU(b,slot);
          TAG(&b[2],0x83,0x01); b[4]=tcid;
          SB_TAG(&b[5],0x00);
          PUT_TAG(b,9);

          static const unsigned char reqCAS[] = { 0xA0,0x07,0x01,0x91,0x04,0x00,0x03,0x00,0x41 };
          memcpy(b,reqCAS,sizeof(reqCAS));
          b[2]=tcid;
          a[0]=sizeof(reqCAS);
          slots[slot]->Put(a,sizeof(reqCAS)+1);
          break;
        case 0xA0: // T_DATA_LAST
          slots[slot]->Process(buff,len);
          TPDU(b,slot);
          SB_TAG(&b[2],slots[slot]->Available()>0 ? 0x80:0x00);
          PUT_TAG(b,6);
          break;
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

//#define DEBUG_CSA

class cDeCSA {
private:
  int cs;
  unsigned char **range;
  unsigned char pidmap[MAX_CSA_PIDS];
  void *keys[MAX_CSA_IDX];
  unsigned int even_odd[MAX_CSA_IDX];
  cMutex mutex;
  cCondVar wait;
  bool active;
  int cardindex;
  //
  bool GetKeyStruct(int idx);
public:
  cDeCSA(int CardIndex);
  ~cDeCSA();
  bool Decrypt(unsigned char *data, int len, bool force);
  bool SetDescr(ca_descr_t *ca_descr, bool initial);
  bool SetCaPid(ca_pid_t *ca_pid);
  void SetActive(bool on);
  };

cDeCSA::cDeCSA(int CardIndex)
{
  cardindex=CardIndex;
  cs=get_suggested_cluster_size();
  PRINTF(L_CORE_CSA,"%d: clustersize=%d rangesize=%d",cardindex,cs,cs*2+5);
  range=MALLOC(unsigned char *,(cs*2+5));
  memset(keys,0,sizeof(keys));
  memset(even_odd,0,sizeof(even_odd));
  memset(pidmap,0,sizeof(pidmap));
}

cDeCSA::~cDeCSA()
{
  for(int i=0; i<MAX_CSA_IDX; i++)
    if(keys[i]) free_key_struct(keys[i]);
  free(range);
}

void cDeCSA::SetActive(bool on)
{
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
      PRINTF(L_CORE_CSA,"%d.%d: %s key in use",cardindex,idx,ca_descr->parity?"odd":"even");
      if(wait.TimedWait(mutex,100)) PRINTF(L_CORE_CSA,"%d.%d: successfully waited for release",cardindex,idx);
      else PRINTF(L_CORE_CSA,"%d.%d: timed out. setting anyways",cardindex,idx);
      }
    PRINTF(L_CORE_CSA,"%d.%d: %s key set",cardindex,idx,ca_descr->parity?"odd":"even");
    if(ca_descr->parity==0) set_even_control_word(keys[idx],ca_descr->cw);
    else                    set_odd_control_word(keys[idx],ca_descr->cw);
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
  for(int l=0; l<len; l+=TS_SIZE) {
    if(data[l]!=TS_SYNC_BYTE) {       // let higher level cope with that
      PRINTF(L_CORE_CSA,"%d: garbage in TS buffer",cardindex);
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
  if(r>=0) {                          // we have some range
    if(ccs>=cs || force) {
      if(GetKeyStruct(currIdx)) {
        int n=decrypt_packets(keys[currIdx],range);
#ifdef DEBUG_CSA
        PRINTF(L_CORE_CSA,"%d.%d: decrypting ccs=%3d cs=%3d %s -> %3d decrypted",cardindex,currIdx,ccs,cs,ccs>=cs?"OK ":"INC",n);
#endif
        if(n>0) return true;
        }
      }
#ifdef DEBUG_CSA
    else PRINTF(L_CORE_CSA,"%d.%d: incomplete cluster ccs=%3d cs=%3d",cardindex,currIdx,ccs,cs);
#endif
    }
  return false;
}

// -- cDeCsaTSBuffer -----------------------------------------------------------

class cDeCsaTSBuffer : public cThread {
private:
  int f;
  int cardIndex;
  bool delivered;
  cRingBufferLinear *ringBuffer;
  //
  cDeCSA *decsa;
  bool scActive;
  unsigned char *lastP;
  int lastCount;
  //
  virtual void Action(void);
public:
  cDeCsaTSBuffer(int File, int Size, int CardIndex, cDeCSA *DeCsa, bool ScActive);
  ~cDeCsaTSBuffer();
  uchar *Get(void);
  };

cDeCsaTSBuffer::cDeCsaTSBuffer(int File, int Size, int CardIndex, cDeCSA *DeCsa, bool ScActive)
{
  SetDescription("TS buffer on device %d", CardIndex);
  f=File; cardIndex=CardIndex; decsa=DeCsa; scActive=ScActive;
  delivered=false;
  lastP=0; lastCount=0;
  ringBuffer=new cRingBufferLinear(Size,TS_SIZE,true,"FFdecsa-TS");
  ringBuffer->SetTimeouts(100,100);
  if(decsa) decsa->SetActive(true);
  Start();
}

cDeCsaTSBuffer::~cDeCsaTSBuffer()
{
  Cancel(3);
  if(decsa) decsa->SetActive(false);
  delete ringBuffer;
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
        if(p[i]==TS_SYNC_BYTE) { Count=i; break; }
      ringBuffer->Del(Count);
      esyslog("ERROR: skipped %d bytes to sync on TS packet on device %d",Count,cardIndex);
      return NULL;
      }

    if(scActive && (p[3]&0xC0)) {
      if(decsa) {
        if(!decsa->Decrypt(p,Count,(lastP==p && lastCount==Count))) {
          lastP=p; lastCount=Count;
          cCondWait::SleepMs(20);
          return NULL;
          }
        lastP=0;
        }
      else p[3]&=~0xC0; // FF hack
      }

    delivered=true;
    return p;
    }
  return NULL;
}

#endif //SASC

// -- cScDvbDevice -------------------------------------------------------------

int cScDvbDevice::budget=0;

#ifndef SASC

cScDvbDevice::cScDvbDevice(int n, int cafd)
:cDvbDevice(n)
{
  decsa=0; tsBuffer=0; cam=0;
#if APIVERSNUM >= 10500
  ciadapter=0; hwciadapter=0;
#endif
  memset(lrucaid,0,sizeof(lrucaid));
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
    PRINTF(L_GEN_INFO,"Using software decryption on card %d",n);
    decsa=new cDeCSA(n);
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

static int *vdr_nci=0, *vdr_ud=0, vdr_save_ud;

void cScDvbDevice::Capture(void)
{
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
}

bool cScDvbDevice::Initialize(void)
{
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
  return cDvbDevice::SetPid(Handle,Type,On);
}

bool cScDvbDevice::SetChannelDevice(const cChannel *Channel, bool LiveView)
{
  lruMutex.Lock();
  int i=FindLRUPrg(Channel->Source(),Channel->Transponder(),Channel->Sid());
  if(i<0) i=MAX_LRU_CAID-1;
  if(i>0) memmove(&lrucaid[1],&lrucaid[0],sizeof(struct LruCaid)*i);
#if APIVERSNUM >= 10500
  const caid_t *c=Channel->Caids();
  for(i=0; i<MAXCAIDS && *c; i++) lrucaid[0].caids[i]=*c++;
  lrucaid[0].caids[i]=0;
#else
  for(i=0; i<=MAXCAIDS; i++) if((lrucaid[0].caids[i]=Channel->Ca(i))==0) break;
#endif
  lrucaid[0].src=Channel->Source();
  lrucaid[0].tr=Channel->Transponder();
  lrucaid[0].prg=Channel->Sid();
  lruMutex.Unlock();
  if(cam) cam->Tune(Channel);
  bool ret=cDvbDevice::SetChannelDevice(Channel,LiveView);
  if(ret && cam) cam->PostTune();
  return ret;
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

#if APIVERSNUM < 10500
int cScDvbDevice::ProvidesCa(const cChannel *Channel) const
{
  if(cam && Channel->Ca()>=CA_ENCRYPTED_MIN) {
    int j;
    caid_t ids[MAXCAIDS+1];
    for(j=0; j<=MAXCAIDS; j++) if((ids[j]=Channel->Ca(j))==0) break;
    if(cSystems::Provides(ids,!softcsa)>0) return 2;
    }
  return cDvbDevice::ProvidesCa(Channel);
}

bool cScDvbDevice::CiAllowConcurrent(void) const
{
  return softcsa || ScSetup.ConcurrentFF>0;
}

void cScDvbDevice::CiStartDecrypting(void)
{
  if(cam) {
    cSimpleList<cPrg> prgList;
    for(cCiCaProgramData *p=ciProgramList.First(); p; p=ciProgramList.Next(p)) {
      if(p->modified) {
        cPrg *prg=new cPrg(p->programNumber,cam->HasPrg(p->programNumber));
        if(prg) {
          for(cCiCaPidData *q=p->pidList.First(); q; q=p->pidList.Next(q)) {
            if(q->active)
              prg->pids.Add(new cPrgPid(q->streamType,q->pid));
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

bool cScDvbDevice::OpenDvr(void)
{
  CloseDvr();
  fd_dvr=DvbOpen(DEV_DVB_DVR,CardIndex(),O_RDONLY|O_NONBLOCK,true);
  if(fd_dvr>=0) {
#if APIVERSNUM >= 10500
    bool active=dynamic_cast<cScCamSlot *>(CamSlot())!=0;
#else
    bool active=cam && softcsa;
#endif
    tsBuffer=new cDeCsaTSBuffer(fd_dvr,MEGABYTE(2),CardIndex()+1,decsa,active);
    }
  return fd_dvr>=0;
}

void cScDvbDevice::CloseDvr(void)
{
  delete tsBuffer; tsBuffer=0;
  if(fd_dvr>=0) { close(fd_dvr); fd_dvr=-1; }
}

bool cScDvbDevice::GetTSPacket(uchar *&Data)
{
  if(tsBuffer) { Data=tsBuffer->Get(); return true; }
  return false;
}

bool cScDvbDevice::SetCaDescr(ca_descr_t *ca_descr, bool initial)
{
  if(!softcsa) {
    cMutexLock lock(&cafdMutex);
    return ioctl(fd_ca,CA_SET_DESCR,ca_descr)>=0;
    }
  else if(decsa) return decsa->SetDescr(ca_descr,initial);
  return false;
}

bool cScDvbDevice::SetCaPid(ca_pid_t *ca_pid)
{
  if(!softcsa) {
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

bool cScDvbDevice::GetPrgCaids(int source, int transponder, int prg, caid_t *c)
{
  return false;
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
