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
#include <string.h>
#include <unistd.h>

#include <vdr/sources.h>

#include "override.h"
#include "global.h"
#include "misc.h"
#include "log-core.h"

// -- cValidityRange -----------------------------------------------------------

cValidityRange::cValidityRange(void)
{
  fromCaid=toCaid=-1; fromSource=toSource=-1; fromFreq=toFreq=-1;
  wildcard=false;
}

bool cValidityRange::Match(int caid, int source, int freq) const
{
  return wildcard ||
         ((caid<0 || fromCaid<0 || (toCaid<0 && caid==fromCaid) || (caid>=fromCaid && caid<=toCaid)) &&
          (fromSource<0 || (toSource<0 && source==fromSource) || (source>=fromSource && source<=toSource)) &&
          (fromFreq<0 || (toFreq<0 && freq==fromFreq) || (freq>=fromFreq && freq<=toFreq)));
}

char *cValidityRange::Parse3(char *s)
{
  bool log=true;
  s=skipspace(s);
  char *e=index(s,'}');
  if(e && *s++=='{') {
    *e=0; e=skipspace(e+1);
    char *p;
    if((p=index(s,':'))) {
      *p=0;
      if(ParseCaidRange(s)) {
        s=p+1;
        if((p=index(s,':'))) {
          *p=0;
          if(s==p || ParseSourceRange(s)) {
            s=p+1;
            if(!*s || ParseFreqRange(s)) return e;
            }
          log=false;
          }
        }
      else log=false;
      }
    }
  if(log) PRINTF(L_CORE_LOAD,"override: RANGE format error");
  return 0;
}

char *cValidityRange::Parse2(char *s, bool wildAllow)
{
  bool log=true;
  s=skipspace(s);
  char *e=index(s,'}');
  if(e && *s++=='{') {
    *e=0; e=skipspace(e+1);
    s=skipspace(s);
    if(wildAllow && *s=='*' && isempty(s+1)) {
      wildcard=true;
      return e;
      }
    char *p;
    if((p=index(s,':'))) {
      *p=0;
      if(ParseSourceRange(s)) {
        s=p+1;
        if(!*s || ParseFreqRange(s)) return e;
        }
      log=false;
      }
    }
  if(log) PRINTF(L_CORE_LOAD,"override: RANGE format error");
  return 0;
}

bool cValidityRange::ParseCaidRange(const char *str)
{
  if(sscanf(str,"%x-%x",&fromCaid,&toCaid)<1) {
    PRINTF(L_CORE_LOAD,"override: CAID format error");
    return false;
    }
  if(fromCaid<0x0001 || fromCaid>0xFFFF ||
     (toCaid>0 && (toCaid<0x0001 || toCaid>0xFFFF || fromCaid>toCaid))) {
    PRINTF(L_CORE_LOAD,"override: CAID range error");
    return false;
    }
  return true;
}

bool cValidityRange::ParseFreqRange(const char *str)
{
  if(sscanf(str,"%d-%d",&fromFreq,&toFreq)<1) {
    PRINTF(L_CORE_LOAD,"override: FREQ format error");
    return false;
    }
  if(fromFreq<1 || fromFreq>50000 ||
     (toFreq>0 && (toFreq<1 || toFreq>50000 || fromFreq>toFreq))) {
    PRINTF(L_CORE_LOAD,"override: FREQ range error");
    return false;
    }
  return true;
}

bool cValidityRange::ParseSourceRange(const char *str)
{
  bool res=false;
  int l;
  char *s1=0, *s2=0;
  if((l=sscanf(str,"%a[^-:]-%a[^-:]",&s1,&s2))>=1) {
    if(s1 && (fromSource=cSource::FromString(s1))>0 &&
       (l<2 || (s2 && (toSource=cSource::FromString(s2))>0))) {
      if((cSource::IsSat(fromSource) && (toSource<0 || (cSource::IsSat(toSource) && toSource>fromSource))) ||
         (cSource::IsCable(fromSource) && toSource<0) ||
         (cSource::IsTerr(fromSource) && toSource<0)) {
        res=true;
        }
      else PRINTF(L_CORE_LOAD,"override: SOURCE range error");
      }
    else PRINTF(L_CORE_LOAD,"override: SOURCE parse error");
    }
  else PRINTF(L_CORE_LOAD,"override: SOURCE format error");
  free(s1); free(s2);
  return res;
}

cString cValidityRange::Print(void)
{
  if(wildcard) return "*";
  char buff[256];
  int q=0;
  if(fromCaid>0) {
    q+=snprintf(buff+q,sizeof(buff)-q,"%04x",fromCaid);
    if(toCaid>0)
      q+=snprintf(buff+q,sizeof(buff)-q,"-%04x",toCaid);
    q+=snprintf(buff+q,sizeof(buff)-q,":");
    }
  if(fromSource>0) {
    q+=snprintf(buff+q,sizeof(buff)-q,"%s",*cSource::ToString(fromSource));
    if(toSource>0)
      q+=snprintf(buff+q,sizeof(buff)-q,"-%s",*cSource::ToString(toSource));
    }
  q+=snprintf(buff+q,sizeof(buff)-q,":");
  if(fromFreq>0) {
    q+=snprintf(buff+q,sizeof(buff)-q,"%d",fromFreq);
    if(toFreq>0)
      q+=snprintf(buff+q,sizeof(buff)-q,"-%d",toFreq);
    }
  return buff;
}

// -- cOverride ----------------------------------------------------------------

#define OV_CAT      1
#define OV_EMMCAID  2
#define OV_ECMTABLE 3
#define OV_EMMTABLE 4
#define OV_TUNNEL   5
#define OV_IGNORE   6
#define OV_ECMPRIO  7

// -- cOverrideCat -------------------------------------------------------------

class cOverrideCat : public cOverride {
private:
  int caid, pid;
public:
  cOverrideCat(void) { type=OV_CAT; }
  virtual bool Parse(char *str);
  int GetCatEntry(unsigned char *buff);
  };

bool cOverrideCat::Parse(char *str)
{
  if((str=Parse2(str))) {
    if(sscanf(str,"%x:%x",&caid,&pid)==2) {
      PRINTF(L_CORE_OVER,"cat: %s - caid %04x pid %04x",*Print(),caid,pid);
      return true;
      }
    PRINTF(L_CORE_LOAD,"override: CAT format error");
    }
  return false;
}

int cOverrideCat::GetCatEntry(unsigned char *buff)
{
  PRINTF(L_CORE_OVER,"cat: added caid %04x pid %04x",caid,pid);
  buff[0]=0x09;
  buff[1]=0x04;
  buff[2]=(caid>>8)&0xFF;
  buff[3]= caid    &0xFF;
  buff[4]=(pid >>8)&0xFF;
  buff[5]= pid     &0xFF;
  return 6;
}

// -- cOverrideEmmCaid ---------------------------------------------------------

class cOverrideEmmCaid : public cOverride {
private:
  int caid;
public:
  cOverrideEmmCaid(void) { type=OV_EMMCAID; }
  virtual bool Parse(char *str);
  int GetCaid(bool log);
  };

bool cOverrideEmmCaid::Parse(char *str)
{
  if((str=Parse3(str))) {
    if(sscanf(str,"%x",&caid)==1) {
      PRINTF(L_CORE_OVER,"emmcaid: %s - caid %04x",*Print(),caid);
      return true;
      }
    PRINTF(L_CORE_LOAD,"override: EMMCAID format error");
    }
  return false;
}

int cOverrideEmmCaid::GetCaid(bool log)
{
  if(log) PRINTF(L_CORE_OVER,"emmcaid: %04x",caid);
  return caid;
}

// -- cOverrideEcmTable --------------------------------------------------------

class cOverrideEcmTable : public cOverride {
private:
  int table;
public:
  cOverrideEcmTable(void) { type=OV_ECMTABLE; }
  virtual bool Parse(char *str);
  int GetTable(bool log);
  };

bool cOverrideEcmTable::Parse(char *str)
{
  if((str=Parse3(str))) {
    if(sscanf(str,"%x",&table)==1) {
      PRINTF(L_CORE_OVER,"ecmtable: %s - table %02x",*Print(),table);
      return true;
      }
    PRINTF(L_CORE_LOAD,"override: ECMTABLE format error");
    }
  return false;
}

int cOverrideEcmTable::GetTable(bool log)
{
  if(log) PRINTF(L_CORE_OVER,"ecmtable: %02x",table);
  return table;
}

// -- cOverrideEmmTable --------------------------------------------------------

#define OV_MAXTABLES 4

class cOverrideEmmTable : public cOverride {
public:
  int num, table[OV_MAXTABLES], mask[OV_MAXTABLES];
  //
  cOverrideEmmTable(void) { type=OV_EMMTABLE; }
  virtual bool Parse(char *str);
  void AddPids(cPids *pids, int pid, int caid);
  };

bool cOverrideEmmTable::Parse(char *str)
{
  if((str=Parse3(str))) {
    num=0;
    int n=-1;
    do {
      mask[num]=0xFF;
      int l=n+1;
      if(sscanf(&str[l],"%x%n/%x%n",&table[num],&n,&mask[num],&n)<1) {
        PRINTF(L_CORE_LOAD,"override: EMMTABLE format error");
        return false;
        }
      n+=l; num++;
      } while(num<OV_MAXTABLES && str[n]==':');
    LBSTART(L_CORE_OVER);
    LBPUT("emmtable: %s - tables",*Print());
    for(int i=0; i<num; i++) LBPUT(" %02x/%02x",table[i],mask[i]);
    LBEND();
    return true;
    }
  return false;
}

void cOverrideEmmTable::AddPids(cPids *pids, int pid, int caid)
{
  LBSTARTF(L_CORE_OVER);
  LBPUT("emmtable: %04x:",caid);
  for(int i=0; i<num; i++) {
    LBPUT(" %02x/%02x",table[i],mask[i]);
    pids->AddPid(pid,table[i],mask[i]);
    }
  LBEND();
}

// -- cRewriter ----------------------------------------------------------------

cRewriter::cRewriter(const char *Name, int Id)
{
  name=Name; id=Id;
  mem=0; mlen=0;
}

cRewriter::~cRewriter()
{
  free(mem);
}

unsigned char *cRewriter::Alloc(int len)
{
  if(!mem || mlen<len) {
    free(mem);
    if(len<256) len=256;
    mem=MALLOC(unsigned char,len);
    mlen=len;
    }
  if(!mem) PRINTF(L_CORE_OVER,"rewriter %s: failed to alloc rewrite buffer",name);
  return mem;
}

// -- cRewriterNagraBeta -------------------------------------------------------

#define RWID_NAGRA_BETA   1001
#define RWNAME_NAGRA_BETA "nagra-beta"

class cRewriterNagraBeta : public cRewriter {
public:
  cRewriterNagraBeta(void);
  virtual bool Rewrite(unsigned char *&data, int &len);
  };

cRewriterNagraBeta::cRewriterNagraBeta(void)
:cRewriter(RWNAME_NAGRA_BETA,RWID_NAGRA_BETA)
{}

bool cRewriterNagraBeta::Rewrite(unsigned char *&data, int &len)
{
  unsigned char *d=Alloc(len+10);
  if(d) {
    static const unsigned char tunnel[] = { 0xc9,0x00,0x00,0x00,0x01,0x10,0x10,0x00,0x48,0x12 };
    d[0]=data[0];
    SetSctLen(d,len+7);
    memcpy(&d[3],tunnel,sizeof(tunnel));
    memcpy(&d[13],&data[3],len-3);
    if(len>0x88) { // assume N3
      d[3]=0xc7; d[11]=0x87;
      }
    if(d[0]&0x01) d[12]++;

    data=d; len+=10;
    return true;
    }
  return false;
}

// -- cRewriters ---------------------------------------------------------------

cRewriter *cRewriters::CreateById(int id)
{
  switch(id) {
    case RWID_NAGRA_BETA: return new cRewriterNagraBeta;
    default: return 0;
    }
}

int cRewriters::GetIdByName(const char *name)
{
  if(!strcasecmp(name,RWNAME_NAGRA_BETA)) return RWID_NAGRA_BETA;
  else return 0;
}

// -- cOverrideTunnel ----------------------------------------------------------

class cOverrideTunnel : public cOverride {
private:
  int caid, rewriterId;
public:
  cOverrideTunnel(void) { type=OV_TUNNEL; }
  virtual bool Parse(char *str);
  int GetTunnel(int *id, bool log);
  };

bool cOverrideTunnel::Parse(char *str)
{
  bool res=false;
  if((str=Parse3(str))) {
    char *name=0;
    if(sscanf(str,"%x:%a[^:]",&caid,&name)>=1) {
      char rw[48];
      if(name) {
        if((rewriterId=cRewriters::GetIdByName(name))>0) {
          snprintf(rw,sizeof(rw),"%s(%d)",name,rewriterId);
          res=true;
          }
        else PRINTF(L_CORE_LOAD,"override: REWRITER name error");
        }
      else {
        strcpy(rw,"<none>");
        rewriterId=0;
        res=true;
        }
      if(res) {
        PRINTF(L_CORE_OVER,"tunnel: %s - to %04x, rewriter %s",*Print(),caid,rw);
        }
      }
    else PRINTF(L_CORE_LOAD,"override: TUNNEL format error");
    free(name);
    }
  return res;
}

int cOverrideTunnel::GetTunnel(int *id, bool log)
{
  if(log) PRINTF(L_CORE_OVER,"tunnel: to %04x (%d)",caid,rewriterId);
  if(id) *id=rewriterId;
  return caid;
}

// -- cOverrideIgnore ----------------------------------------------------------

#define OV_MAXIGNORES 64

class cOverrideIgnore : public cOverride {
private:
  int num, caid[OV_MAXIGNORES];
public:
  cOverrideIgnore(void) { type=OV_IGNORE; }
  virtual bool Parse(char *str);
  bool Ignore(int Caid);
  };

bool cOverrideIgnore::Parse(char *str)
{
  if((str=Parse2(str,true))) {
    num=0;
    int n=-1;
    do {
      int l=n+1;
      if(sscanf(&str[l],"%x%n",&caid[num],&n)!=1) {
        PRINTF(L_CORE_LOAD,"override: IGNORE format error");
        return false;
        }
      n+=l; num++;
      } while(num<OV_MAXIGNORES && str[n]==':');
    LBSTART(L_CORE_OVER);
    LBPUT("ignore: %s - caids",*Print());
    for(int i=0; i<num; i++) LBPUT(" %02x",caid[i]);
    LBEND();
    return true;
    }
  return false;
}

bool cOverrideIgnore::Ignore(int Caid)
{
  for(int i=0; i<num; i++)
    if(Caid==caid[i]) return true;
  return false;
}

// -- cOverrideEcmPrio ---------------------------------------------------------

#define OV_MAXPRIOS 16

class cOverrideEcmPrio : public cOverride {
private:
  int num, caid[OV_MAXPRIOS], prov[OV_MAXPRIOS];
  //
  bool UsesProvId(int caid);
public:
  cOverrideEcmPrio(void) { type=OV_ECMPRIO; }
  virtual bool Parse(char *str);
  int GetPrio(int Caid, int Prov);
  };

bool cOverrideEcmPrio::Parse(char *str)
{
  if((str=Parse2(str))) {
    num=0;
    int n=-1;
    do {
      prov[num]=-1;
      int l=n+1;
      if(sscanf(&str[l],"%x%n/%x%n",&caid[num],&n,&prov[num],&n)<1) {
        PRINTF(L_CORE_LOAD,"override: ECMPRIO format error");
        return false;
        }
      if(prov[num]>=0 && !UsesProvId(caid[num])) {
        PRINTF(L_CORE_LOAD,"override: ECMPRIO provider ID not supported for caid %04x",caid[num]);
        return false;
        }
      n+=l; num++;
      } while(num<OV_MAXPRIOS && str[n]==':');
    LBSTART(L_CORE_OVER);
    LBPUT("ecmprio: %s - chain",*Print());
    for(int i=0; i<num; i++) LBPUT(prov[i]>=0 ? " %04x/%x":" %04x",caid[i],prov[i]);
    LBEND();
    return true;
    }
  return false;
}

bool cOverrideEcmPrio::UsesProvId(int caid)
{
  switch(caid>>8) {
    case 0x01:
    case 0x05: return true;
    }
  return false;
}

int cOverrideEcmPrio::GetPrio(int Caid, int Prov)
{
  int pri=0;
  for(int i=0; i<num; i++) {
    if(Caid==caid[i] && (prov[i]<0 || Prov==prov[i])) break;
    pri--;
    }
  PRINTF(L_CORE_OVER,"ecmprio: %04x/%x pri %d",Caid,Prov,pri);
  return pri;
}

// -- cOverrides ---------------------------------------------------------------

cOverrides overrides;

cOverrides::cOverrides(void)
:cStructList<cOverride>("overrides","override.conf",SL_MISSINGOK|SL_WATCH|SL_VERBOSE)
{}

void cOverrides::PreLoad(void)
{
  caidTrigger=false;
}

void cOverrides::PostLoad(void)
{
  if(caidTrigger) cGlobal::CaidsChanged();
}

cOverride *cOverrides::ParseLine(char *line)
{
  cOverride *ov=0;
  line=skipspace(line);
  char *p=index(line,':');
  if(p) {
    *p++=0;
    if(!strncasecmp(line,"cat",3)) ov=new cOverrideCat;
    else if(!strncasecmp(line,"emmcaid",7)) ov=new cOverrideEmmCaid;
    else if(!strncasecmp(line,"ecmtable",8)) ov=new cOverrideEcmTable;
    else if(!strncasecmp(line,"emmtable",8)) ov=new cOverrideEmmTable;
    else if(!strncasecmp(line,"tunnel",6)) ov=new cOverrideTunnel;
    else if(!strncasecmp(line,"ignore",6)) { ov=new cOverrideIgnore; caidTrigger=true; }
    else if(!strncasecmp(line,"ecmprio",7)) ov=new cOverrideEcmPrio;
    if(ov && !ov->Parse(p)) { delete ov; ov=0; }
    }
  return ov;
}

cOverride *cOverrides::Find(int type, int caid, int source, int transponder, cOverride *ov)
{
  for(ov=ov?Next(ov):First(); ov; ov=Next(ov))
    if(ov->Type()==type && ov->Match(caid,source,transponder%100000)) break;
  return ov;
}

int cOverrides::GetCat(int source, int transponder, unsigned char *buff, int len)
{
  int n=0;
  ListLock(false);
  for(cOverride *ov=0; (ov=Find(OV_CAT,-1,source,transponder,ov)) && n<len-32;) {
    cOverrideCat *ovc=dynamic_cast<cOverrideCat *>(ov);
    if(ovc) n+=ovc->GetCatEntry(&buff[n]);
    }
  ListUnlock();
  return n;
}

void cOverrides::UpdateEcm(cEcmInfo *ecm, bool log)
{
  ListLock(false);
  cOverrideEcmTable *ovt=dynamic_cast<cOverrideEcmTable *>(Find(OV_ECMTABLE,ecm->caId,ecm->source,ecm->transponder));
  if(ovt) ecm->ecm_table=ovt->GetTable(log);
  cOverrideEmmCaid *ovc=dynamic_cast<cOverrideEmmCaid *>(Find(OV_EMMCAID,ecm->caId,ecm->source,ecm->transponder));
  if(ovc) ecm->emmCaId=ovc->GetCaid(log);
  cOverrideTunnel *ovu=dynamic_cast<cOverrideTunnel *>(Find(OV_TUNNEL,ecm->caId,ecm->source,ecm->transponder));
  if(ovu) {
    if(ecm->emmCaId==0) ecm->emmCaId=ecm->caId;
    ecm->caId=ovu->GetTunnel(&ecm->rewriterId,log);
    ecm->provId=0;
    if(ecm->rewriterId) ecm->rewriter=cRewriters::CreateById(ecm->rewriterId);
    }
  ListUnlock();
}

bool cOverrides::AddEmmPids(int caid, int source, int transponder, cPids *pids, int pid)
{
  bool res=false;
  ListLock(false);
  cOverrideEmmTable *ovt=dynamic_cast<cOverrideEmmTable *>(Find(OV_EMMTABLE,caid,source,transponder));
  if(ovt) {
    ovt->AddPids(pids,pid,caid);
    res=true;
    }
  ListUnlock();
  return res;
}

bool cOverrides::Ignore(int source, int transponder, int caid)
{
  bool res=false;
  ListLock(false);
  for(cOverride *ov=0; (ov=Find(OV_IGNORE,-1,source,transponder,ov)) && !res;) {
    cOverrideIgnore *ovi=dynamic_cast<cOverrideIgnore *>(ov);
    if(ovi && ovi->Ignore(caid)) res=true;
    }
  ListUnlock();
  return res;
}

int cOverrides::GetEcmPrio(int source, int transponder, int caid, int prov)
{
  int pri=0;
  ListLock(false);
  cOverrideEcmPrio *ovp=dynamic_cast<cOverrideEcmPrio *>(Find(OV_ECMPRIO,-1,source,transponder));
  if(ovp) pri=ovp->GetPrio(caid,prov);
  ListUnlock();
  return pri;
}
