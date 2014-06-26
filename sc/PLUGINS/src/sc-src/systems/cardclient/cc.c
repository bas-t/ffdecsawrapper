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

#include <vdr/thread.h>
#include <vdr/tools.h>

#include "system.h"
#include "cc.h"
#include "misc.h"
#include "opts.h"
#include "log-core.h"
#include "version.h"

SCAPIVERSTAG();

#define SYSTEM_NAME          "Cardclient"
#define SYSTEM_PRI           -15

#define CONF_FILE            "cardclient.conf"

static const struct LogModule lm_cc = {
  (LMOD_ENABLE|L_CC_ALL)&LOPT_MASK,
  (LMOD_ENABLE|L_CC_CORE|L_CC_LOGIN|L_CC_ECM|L_CC_EMM|L_CC_CAMD|L_CC_CAMD35|
   L_CC_RDGD|L_CC_NEWCAMD|L_CC_GBOX|L_CC_CCCAM|L_CC_CCCAM2|L_CC_CCCAM2SH|L_CC_CCCAM2EX)&LOPT_MASK,
  "cardclient",
  { "core","login","ecm","emm","camd","camd35","camd35extra","radegast",
    "newcamd","gbox","cccam","cccam2","cccam2data","cccam2shares","cccam2extra"}
  };
ADD_MODULE(L_CC,lm_cc)

static int immediate=true;

// -- cCardClient --------------------------------------------------------------

cCardClient::cCardClient(const char *Name)
:msECM(32,16)
,msEMM(32,0)
{
  name=Name;
  emmAllowed=0; emmCaid[0]=0x1700; emmMask[0]=0xFF00; numCaid=1;
}

bool cCardClient::Immediate(void)
{
  return immediate;
}

void cCardClient::CaidsChanged(void)
{
  cSystem::CaidsChanged();
}

bool cCardClient::ParseStdConfig(const char *config, int *num)
{
  int n, emm=0;
  if(!num) num=&n;
  if(sscanf(config,"%63[^:]:%d:%d%n",hostname,&port,&emm,num)<3) return false;
  if(emm>0) emmAllowed=1;
  if(emm==2) emmAllowed=2; // EMM caching disabled
  if(config[*num]=='/') {
    numCaid=0;
    do {
      emmMask[numCaid]=0xFFFF;
      int start=(*num)+1;
      if(sscanf(&config[start],"%x%n/%x%n",&emmCaid[numCaid],num,&emmMask[numCaid],num)<1)
        return false;
      *num+=start;
      if(emmMask[numCaid]==0x0000 && emmCaid[numCaid]!=0x0000)
        PRINTF(L_GEN_WARN,"CAID %04x MASK %04x in cardclient config doesn't match anything!",emmCaid[numCaid],emmMask[numCaid]);
      numCaid++;
      } while(numCaid<MAX_CC_CAID && config[*num]==',');
    }
  LBSTART(L_CC_CORE);
  LBPUT("hostname=%s port=%d emm=%d emmCaids",hostname,port,emmAllowed);
  for(int i=0; i<numCaid; i++) LBPUT(" %04x/%04x",emmCaid[i],emmMask[i]);
  LBEND();
  return true;
}

bool cCardClient::CanHandle(unsigned short SysId)
{
  for(int i=0; i<numCaid; i++)
    if((SysId&emmMask[i])==emmCaid[i]) return true;
  return false;
}

void cCardClient::Logout(void)
{
  so.Disconnect();
}

bool cCardClient::SendMsg(const unsigned char *data, int len)
{
  if(!so.Connected() && !Login()) return false;
  if(so.Write(data,len)<0) {
    PRINTF(L_CC_CORE,"send error. reconnecting...");
    Logout();
    return false;
    }
  return true;
}

int cCardClient::RecvMsg(unsigned char *data, int len, int to)
{
  if(!so.Connected() && !Login()) return -1;
  int n=so.Read(data,len,to);
  if(n<0) {
    if(errno==ETIMEDOUT && (len<0 || to==0)) return 0;
    PRINTF(L_CC_CORE,"recv error. reconnecting...");;
    Logout();
    }
  return n;
}

// -- cCardClients -------------------------------------------------------------

class cCardClients {
friend class cCardClientLink;
private:
  static cCardClientLink *first;
  //
  static void Register(cCardClientLink *ccl);
public:
  cCardClientLink *FindByName(const char *name);
  };

cCardClientLink *cCardClients::first=0;

static cCardClients cardclients;

void cCardClients::Register(cCardClientLink *ccl)
{
  PRINTF(L_CORE_DYN,"cardclients: registering cardclient %s",ccl->name);
  ccl->next=first;
  first=ccl;
}

cCardClientLink *cCardClients::FindByName(const char *name)
{
  cCardClientLink *ccl=first;
  while(ccl) {
    if(!strcasecmp(ccl->name,name)) break;
    ccl=ccl->next;
    }
  return ccl;
}

// -- cCardClientLink ----------------------------------------------------------

cCardClientLink::cCardClientLink(const char *Name)
{
  name=Name;
  cCardClients::Register(this);
}

// -- cSystemLinkCardClient -----------------------------------------------------------

class cSystemLinkCardClient : public cSystemLink, public cStructListPlain<cCardClient> {
protected:
  virtual bool ParseLinePlain(const char *line);
public:
  cSystemLinkCardClient(void);
  virtual bool CanHandle(unsigned short SysId);
  virtual cSystem *Create(void);
  virtual void Clean(void);
  cCardClient *FindBySysId(unsigned short id, cCardClient *cc);
  };

static cSystemLinkCardClient staticCcl;

// -- cSystemCardClient ---------------------------------------------------------------

class cSystemCardClient : public cSystem {
private:
  cCardClient *cc;
public:
  cSystemCardClient(void);
  virtual bool ProcessECM(const cEcmInfo *ecm, unsigned char *data);
  virtual void ProcessEMM(int pid, int caid, const unsigned char *buffer);
  };

cSystemCardClient::cSystemCardClient(void)
:cSystem(SYSTEM_NAME,SYSTEM_PRI)
{
  cc=0;
  local=false; hasLogger=true;
}

bool cSystemCardClient::ProcessECM(const cEcmInfo *ecm, unsigned char *data)
{
  cCardClient *startCc=cc, *oldcc;
  do {
    if(cc) {
      cTimeMs start;
      int id=cc->msECM.Get(data,SCT_LEN(data),cw);
      if(id==0 || (id>0 && cc->ProcessECM(ecm,data,cw))) {
        int dur=start.Elapsed();
        if(dur>2000) {
          char bb[32];
          time_t now=time(0);
          ctime_r(&now,bb); stripspace(bb);
          PRINTF(L_CC_CORE,"%s: lagged cw %d ms (%s)",bb,dur,cc->Name());
          }
        char buff[32];
        snprintf(buff,sizeof(buff),"CC %s",cc->Name());
        KeyOK(buff);
        if(id>0) cc->msECM.Cache(id,true,cw);
        return true;
        }
      if(id>0) {
        PRINTF(L_CC_CORE,"client %s (%s:%d) ECM failed (%d ms)",cc->Name(),cc->hostname,cc->port,(int)start.Elapsed());
        cc->msECM.Cache(id,false,cw);
        }
      if(id<0) {
        PRINTF(L_CC_CORE,"client %s (%s:%d) ECM already cached as failed",cc->Name(),cc->hostname,cc->port);
        }
      }
    if(!cc) PRINTF(L_CC_CORE,"cc-loop");
    oldcc=cc;
    cc=staticCcl.FindBySysId(ecm->caId,cc);
    if(cc && cc!=startCc) PRINTF(L_CC_CORE,"now trying client %s (%s:%d)",cc->Name(),cc->hostname,cc->port);
    } while(cc!=startCc && cc!=oldcc);
  return false;
}

void cSystemCardClient::ProcessEMM(int pid, int caid, const unsigned char *buffer)
{
  cCardClient *cc=0;
  while((cc=staticCcl.FindBySysId(caid,cc)))
    cc->ProcessEMM(caid,buffer);
}

// -- cSystemLinkCardClient -----------------------------------------------------------

cSystemLinkCardClient::cSystemLinkCardClient(void)
:cSystemLink(SYSTEM_NAME,SYSTEM_PRI)
,cStructListPlain<cCardClient>("cardclient config",CONF_FILE,SL_MISSINGOK|SL_VERBOSE|SL_NOPURGE)
{
  opts=new cOpts(SYSTEM_NAME,1);
  opts->Add(new cOptBool("Immediate",trNOOP("Cardclient: connect immediately"),&immediate));
}

cCardClient *cSystemLinkCardClient::FindBySysId(unsigned short id, cCardClient *cc)
{
  ListLock(false);
  for(cc=cc ? Next(cc):First(); cc; cc=Next(cc)) 
    if(cc->CanHandle(id)) break;
  ListUnlock();
  return cc;
}

bool cSystemLinkCardClient::CanHandle(unsigned short SysId)
{
  return FindBySysId(SysId,0)!=0;
}

cSystem *cSystemLinkCardClient::Create(void)
{
  return new cSystemCardClient();
}

bool cSystemLinkCardClient::ParseLinePlain(const char *line)
{
  char name[32];
  int num;
  if(sscanf(line,"%31[^:]:%n",name,&num)==1) {
    cCardClientLink *ccl=cardclients.FindByName(name);
    if(ccl) {
      cCardClient *cc=ccl->Create();
      if(cc) {
        if(cc->Init(&line[num])) {
          Add(cc);
          PRINTF(L_CC_CORE,"client '%s' ready",cc->Name());
          if(cc->Immediate()) cc->Login();
          return true;
          }
        else {
          delete cc;
          PRINTF(L_GEN_ERROR,"init of cardclient '%s' failed",name);
          }
        }
      else PRINTF(L_GEN_ERROR,"failed to create cardclient '%s'",name);
      }
    else PRINTF(L_GEN_ERROR,"no client found for card server type '%s'",name);
    }
  return false;
}

void cSystemLinkCardClient::Clean(void)
{
  SafeClear();
}
