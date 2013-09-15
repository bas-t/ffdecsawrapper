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
#include <ffdecsawrapper/channels.h>
#include <ffdecsawrapper/ci.h>
#include <ffdecsawrapper/dvbdevice.h>
#include <ffdecsawrapper/thread.h>
#ifndef FFDECSAWRAPPER
#include "FFdecsa/FFdecsa.h"
#include <ffdecsawrapper/dvbci.h>
#endif //FFDECSAWRAPPER
#include "device.h"
#include "cam.h"
#include "scsetup.h"
#include "system.h"
#include "data.h"
#include "override.h"
#include "misc.h"
#include "log-core.h"

#define MAX_CI_SLOTS      8

#ifdef FFDECSAWRAPPER_MAXCAID
#define MAX_CI_SLOT_CAIDS FFDECSAWRAPPER_MAXCAID
#else
#define MAX_CI_SLOT_CAIDS 16
#endif

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

// --- cChannelCaids -----------------------------------------------------------

#ifndef FFDECSAWRAPPER

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
  void Unique(bool full);
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
  PRINTF(L_CORE_CAIDS,"%d: after check",n);
  for(cChannelCaids *ch=First(); ch; ch=Next(ch)) ch->Dump(n);
  PRINTF(L_CORE_CAIDS,"%d: check cache usage: %d requests, %d hits, %d%% hits",n,cTotal,cHits,(cTotal>0)?(cHits*100/cTotal):0);
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
  PRINTF(L_CORE_CAIDS,"%d: after unique (%d)",n,full);
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

#endif //FFDECSAWRAPPER

// --- cScDeviceProbe ----------------------------------------------------------

#define DEV_DVB_ADAPTER  "/dev/dvb/adapter"
#define DEV_DVB_FRONTEND "frontend"
#define DEV_DVB_DVR      "dvr"
#define DEV_DVB_DEMUX    "demux"
#define DEV_DVB_CA       "ca"

#if APIVERSNUM >= 10711

class cScDeviceProbe : public cDvbDeviceProbe {
private:
  static cScDeviceProbe *probe;
public:
  virtual bool Probe(int Adapter, int Frontend);
  static void Install(void);
  static void Remove(void);
  };

cScDeviceProbe *cScDeviceProbe::probe=0;

void cScDeviceProbe::Install(void)
{
  if(!probe) probe=new cScDeviceProbe;
}

void cScDeviceProbe::Remove(void)
{
  delete probe; probe=0;
}

bool cScDeviceProbe::Probe(int Adapter, int Frontend)
{
  PRINTF(L_GEN_DEBUG,"capturing device %d/%d",Adapter,Frontend);
  new cScDevice(Adapter,Frontend,cScDevices::DvbOpen(DEV_DVB_CA,Adapter,Frontend,O_RDWR));
  return true;
}
#endif

// -- cScDevices ---------------------------------------------------------------

int cScDevices::budget=0;

void cScDevices::DvbName(const char *Name, int a, int f, char *buffer, int len)
{
  snprintf(buffer,len,"%s%d/%s%d",DEV_DVB_ADAPTER,a,Name,f);
}

int cScDevices::DvbOpen(const char *Name, int a, int f, int Mode, bool ReportError)
{
  char FileName[128];
  DvbName(Name,a,f,FileName,sizeof(FileName));
  int fd=open(FileName,Mode);
  if(fd<0 && ReportError) LOG_ERROR_STR(FileName);
  return fd;
}

#ifndef FFDECSAWRAPPER

#if APIVERSNUM < 10711
static int *vdr_nci=0, *vdr_ud=0, vdr_save_ud;
#endif

void cScDevices::OnPluginLoad(void)
{
#if APIVERSNUM >= 10711
  cScDeviceProbe::Install();
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

void cScDevices::OnPluginUnload(void)
{
#if APIVERSNUM >= 10711
  cScDeviceProbe::Remove();
#endif
}

bool cScDevices::Initialize(void)
{
#if APIVERSNUM >= 10711
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
      cScDevices::DvbName(DEV_DVB_FRONTEND,i,0,name,sizeof(name));
      if(access(name,F_OK)==0) {
        PRINTF(L_GEN_DEBUG,"probing %s",name);
        int f=open(name,O_RDONLY);
        if(f>=0) {
          close(f);
          PRINTF(L_GEN_DEBUG,"capturing device %d",i);
          new cScDevice(i,0,cScDevices::DvbOpen(DEV_DVB_CA,i,0,O_RDWR));
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

void cScDevices::Startup(void)
{
  if(ScSetup.ForceTransfer)
    SetTransferModeForDolbyDigital(2);
  for(int n=cDevice::NumDevices(); --n>=0;) {
    cScDevice *dev=dynamic_cast<cScDevice *>(cDevice::GetDevice(n));
    if(dev) dev->LateInit();
    }
}

void cScDevices::Shutdown(void)
{
  for(int n=cDevice::NumDevices(); --n>=0;) {
    cScDevice *dev=dynamic_cast<cScDevice *>(cDevice::GetDevice(n));
    if(dev) dev->EarlyShutdown();
    }
}

void cScDevices::SetForceBudget(int n)
{
   if(n>=0 && n<MAXDVBDEVICES) budget|=(1<<n);
}

bool cScDevices::ForceBudget(int n)
{
   return budget && (budget&(1<<n));
}

#else //FFDECSAWRAPPER

void cScDevices::OnPluginLoad(void) {}
void cScDevices::OnPluginUnload(void) {}
bool cScDevices::Initialize(void) { return true; }
void cScDevices::Startup(void) {}
void cScDevices::Shutdown(void) {}
void cScDevices::SetForceBudget(int n) {}
bool cScDevices::ForceBudget(int n) { return true; }

#endif //FFDECSAWRAPPER

// -- cScDevice ----------------------------------------------------------------

#if APIVERSNUM >= 10711
#define DVB_DEV_SPEC adapter,frontend
#else
#define DVB_DEV_SPEC CardIndex(),0
#endif

cScDevice::cScDevice(int Adapter, int Frontend, int cafd)
#if APIVERSNUM >= 10711
:cDvbDevice(Adapter,Frontend)
#else
:cDvbDevice(Adapter)
#endif
{
#ifndef FFDECSAWRAPPER
  decsa=0; tsBuffer=0; cam=0; fullts=false;
  ciadapter=0; hwciadapter=0;
  fd_ca=cafd; fd_ca2=dup(fd_ca); fd_dvr=-1;
  softcsa=(fd_ca<0);
#else
  softcsa=fullts=false;
  cam=new cCam(this,Adapter);
#endif // !FFDECSAWRAPPER
}

cScDevice::~cScDevice()
{
#ifndef FFDECSAWRAPPER
  DetachAllReceivers();
  Cancel(3);
  EarlyShutdown();
  delete decsa;
  if(fd_ca>=0) close(fd_ca);
  if(fd_ca2>=0) close(fd_ca2);
#else
  delete cam;
#endif // !FFDECSAWRAPPER
}

#ifndef FFDECSAWRAPPER

void cScDevice::EarlyShutdown(void)
{
  SetCamSlot(0);
  delete ciadapter; ciadapter=0;
  delete hwciadapter; hwciadapter=0;
  if(cam) cam->Stop();
  delete cam; cam=0;
}

void cScDevice::LateInit(void)
{
  int n=CardIndex();
  if(DeviceNumber()!=n)
    PRINTF(L_GEN_ERROR,"CardIndex - DeviceNumber mismatch! Put SC plugin first on VDR commandline!");
  if(softcsa) {
    if(HasDecoder()) PRINTF(L_GEN_ERROR,"Card %d is a full-featured card but no ca device found!",n);
    }
  else if(cScDevices::ForceBudget(n)) {
    PRINTF(L_GEN_INFO,"Budget mode forced on card %d",n);
    softcsa=true;
    }
  
  if(fd_ca2>=0) hwciadapter=cDvbCiAdapter::CreateCiAdapter(this,fd_ca2);
  cam=new cCam(this,n);
  ciadapter=new cScCiAdapter(this,n,cam);
  if(softcsa) {
    decsa=new cDeCSA(n);
    if(IsPrimaryDevice() && HasDecoder()) {
      PRINTF(L_GEN_INFO,"Enabling hybrid full-ts mode on card %d",n);
      fullts=true;
      }
    else PRINTF(L_GEN_INFO,"Using software decryption on card %d",n);
    }
}

bool cScDevice::HasCi(void)
{
  return ciadapter || hwciadapter;
}

bool cScDevice::Ready(void)
{
  return (ciadapter   ? ciadapter->Ready():true) &&
         (hwciadapter ? hwciadapter->Ready():true);
}

bool cScDevice::SetPid(cPidHandle *Handle, int Type, bool On)
{
  if(cam) cam->SetPid(Type,Handle->pid,On);
  tsMutex.Lock();
  if(tsBuffer) tsBuffer->SetActive(ScActive());
  tsMutex.Unlock();
  return cDvbDevice::SetPid(Handle,Type,On);
}

bool cScDevice::SetChannelDevice(const cChannel *Channel, bool LiveView)
{
  if(cam) cam->Tune(Channel);
  bool ret=cDvbDevice::SetChannelDevice(Channel,LiveView);
  if(ret && cam) cam->PostTune();
  return ret;
}

bool cScDevice::ScActive(void)
{
  return dynamic_cast<cScCamSlot *>(CamSlot())!=0;
}

bool cScDevice::OpenDvr(void)
{
  CloseDvr();
  fd_dvr=cScDevices::DvbOpen(DEV_DVB_DVR,DVB_DEV_SPEC,O_RDONLY|O_NONBLOCK,true);
  if(fd_dvr>=0) {
    tsMutex.Lock();
    tsBuffer=new cDeCsaTSBuffer(fd_dvr,MEGABYTE(ScSetup.DeCsaTsBuffSize),CardIndex()+1,decsa,ScActive());
    tsMutex.Unlock();
    }
  return fd_dvr>=0;
}

void cScDevice::CloseDvr(void)
{
  tsMutex.Lock();
  delete tsBuffer; tsBuffer=0;
  tsMutex.Unlock();
  if(fd_dvr>=0) { close(fd_dvr); fd_dvr=-1; }
}

bool cScDevice::GetTSPacket(uchar *&Data)
{
  if(tsBuffer) { Data=tsBuffer->Get(); return true; }
  return false;
}

#endif // !FFDECSAWRAPPER

bool cScDevice::SoftCSA(bool live)
{
  return softcsa && (!fullts || !live);
}

void cScDevice::CaidsChanged(void)
{
#ifndef FFDECSAWRAPPER
  if(ciadapter) ciadapter->CaidsChanged();
  PRINTF(L_CORE_CAIDS,"caid list rebuild triggered");
#endif // !FFDECSAWRAPPER
}

bool cScDevice::SetCaDescr(ca_descr_t *ca_descr, bool initial)
{
#ifndef FFDECSAWRAPPER
  if(!softcsa || (fullts && ca_descr->index==0)) {
    cMutexLock lock(&cafdMutex);
    return ioctl(fd_ca,CA_SET_DESCR,ca_descr)>=0;
    }
  else if(decsa) return decsa->SetDescr(ca_descr,initial);
#endif // !FFDECSAWRAPPER
  return false;
}

bool cScDevice::SetCaPid(ca_pid_t *ca_pid)
{
#ifndef FFDECSAWRAPPER
  if(!softcsa || (fullts && ca_pid->index==0)) {
    cMutexLock lock(&cafdMutex);
    return ioctl(fd_ca,CA_SET_PID,ca_pid)>=0;
    }
  else if(decsa) return decsa->SetCaPid(ca_pid);
#endif // !FFDECSAWRAPPER
  return false;
}

int cScDevice::FilterHandle(void)
{
  return cScDevices::DvbOpen(DEV_DVB_DEMUX,DVB_DEV_SPEC,O_RDWR|O_NONBLOCK);
}

#ifndef FFDECSAWRAPPER
static unsigned int av7110_read(int fd, unsigned int addr)
{
  ca_pid_t arg;
  arg.pid=addr;
  ioctl(fd,CA_GET_MSG,&arg);
  return arg.index;
}
#endif // !FFDECSAWRAPPER

#if 0
static void av7110_write(int fd, unsigned int addr, unsigned int val)
{
  ca_pid_t arg;
  arg.pid=addr;
  arg.index=val;
  ioctl(fd,CA_SEND_MSG,&arg);
}
#endif

void cScDevice::DumpAV7110(void)
{
#ifndef FFDECSAWRAPPER
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
#endif // !FFDECSAWRAPPER
}

