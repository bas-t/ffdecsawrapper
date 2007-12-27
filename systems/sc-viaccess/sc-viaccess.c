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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system-common.h"
#include "smartcard.h"
#include "parse.h"
#include "misc.h"
#include "log-sc.h"
#include "log-core.h"

#define SYSTEM_VIACCESS      0x0500

#define SYSTEM_NAME          "SC-Viaccess"
#define SYSTEM_PRI           -5
#define SYSTEM_CAN_HANDLE(x) ((x)==SYSTEM_VIACCESS)

#define SC_NAME "Viaccess"
#define SC_ID   MAKE_SC_ID('V','i','a','s')

#define L_SC        13
#define L_SC_ALL    LALL(L_SC_LASTDEF)

static const struct LogModule lm_sc = {
  (LMOD_ENABLE|L_SC_ALL)&LOPT_MASK,
  (LMOD_ENABLE|L_SC_DEFDEF)&LOPT_MASK,
  "sc-viaccess",
  { L_SC_DEFNAMES }
  };
ADD_MODULE(L_SC,lm_sc)

// -- cSystemScViaccess ------------------------------------------------------------------

class cSystemScViaccess : public cSystemScCore { //, private cTPS {
public:
  cSystemScViaccess(void);
  virtual void ParseCADescriptor(cSimpleList<cEcmInfo> *ecms, unsigned short sysId, const unsigned char *data, int len);
  };

cSystemScViaccess::cSystemScViaccess(void)
:cSystemScCore(SYSTEM_NAME,SYSTEM_PRI,SC_ID,"SC Viaccess")
{
  hasLogger=true;
}

void cSystemScViaccess::ParseCADescriptor(cSimpleList<cEcmInfo> *ecms, unsigned short sysId, const unsigned char *data, int len)
{
  const int pid=WORD(data,2,0x1FFF);
  if(pid>=0xAA && pid<=0xCF) {
    PRINTF(L_CORE_ECMPROC,"sc-viaccess: dropped \"fake\" ecm pid 0x%04xn",pid);
    return;
    }
  cSystem::ParseCADescriptor(ecms,sysId,data,len);
}

// -- cSystemLinkScViaccess --------------------------------------------------------------

class cSystemLinkScViaccess : public cSystemLink {
public:
  cSystemLinkScViaccess(void);
  virtual bool CanHandle(unsigned short SysId);
  virtual cSystem *Create(void) { return new cSystemScViaccess; }
  };

static cSystemLinkScViaccess staticInit;

cSystemLinkScViaccess::cSystemLinkScViaccess(void)
:cSystemLink(SYSTEM_NAME,SYSTEM_PRI)
{
  Feature.NeedsSmartCard();
}

bool cSystemLinkScViaccess::CanHandle(unsigned short SysId)
{
  return smartcards.HaveCard(SC_ID) && SYSTEM_CAN_HANDLE(SysId);
}

// -- cProviderScViaccess ----------------------------------------------------------

class cProviderScViaccess : public cProviderViaccess {
public:
  unsigned char availKeys[16];
  char name[33];
  //
  cProviderScViaccess(const unsigned char *id, const unsigned char *s):cProviderViaccess(id,s) {}
  };

// -- cSmartCardViaccess -----------------------------------------------------------------

class cSmartCardViaccess : public cSmartCard, public cIdSet {
private:
  unsigned char lastId[3];
  //
  unsigned char GetSW1(void) { return sb[1]; }
  bool CheckKey(cProviderScViaccess *p, const unsigned char keynr);
  bool SetProvider(const unsigned char *id);
public:
  cSmartCardViaccess(void);
  virtual bool Init(void);
  virtual bool Decode(const cEcmInfo *ecm, const unsigned char *data, unsigned char *cw);
  virtual bool Update(int pid, int caid, const unsigned char *data);
  };

static const struct StatusMsg msgs[] = {
  { { 0x6b,0x00 }, "Instruction not supported", false },
  { { 0x6d,0x00 }, "Instruction not supported", false },
  { { 0x90,0x00 }, "Instruction executed without errors", true },
  { { 0x90,0x08 }, "Instruction executed without errors", true },
  { { 0xFF,0xFF }, 0, false }
  };

static const struct CardConfig cardCfg = {
  SM_8O2,500,300
  };

cSmartCardViaccess::cSmartCardViaccess(void)
:cSmartCard(&cardCfg,msgs)
{
  memset(lastId,0,sizeof(lastId));
}

bool cSmartCardViaccess::Init(void)
{
  static const unsigned char verifyBytes[] = { 0x90,0x00 };
  if(atr->T!=0 || atr->histLen<7 || memcmp(&atr->hist[5],verifyBytes,sizeof(verifyBytes))) {
    PRINTF(L_SC_INIT,"doesn't look like a Viaccess card");
    return false;
    }

  infoStr.Begin();
  infoStr.Strcat("Viaccess smartcard\n");
  char *ver=0;
  switch((atr->hist[3]<<8)|atr->hist[4]) {
    case 0x6268: ver="2.3"; break;
    case 0x6468:
    case 0x6668: ver="2.4"; break;
    case 0xa268: ver="2.5"; break;
    case 0xc168: ver="2.6"; break;
    default: ver="unknown"; break;
    }
      
  PRINTF(L_SC_INIT,"card v.%s",ver);
  snprintf(idStr,sizeof(idStr),"%s (V.%s)",SC_NAME,ver);

  static unsigned char insac[] = { 0xca, 0xac, 0x00, 0x00, 0x00 }; // select data
  static unsigned char insb8[] = { 0xca, 0xb8, 0x00, 0x00, 0x00 }; // read selected data
  static unsigned char insa4[] = { 0xca, 0xa4, 0x00, 0x00, 0x00 }; // select issuer
  static unsigned char insc0[] = { 0xca, 0xc0, 0x00, 0x00, 0x00 }; // read data item
  unsigned char buff[MAX_LEN];

  ResetIdSet();
  insac[2]=0xa4; // request unique id
  if(!IsoWrite(insac,buff) || !Status()) {
    PRINTF(L_SC_ERROR,"failed to request ua");
    return false;
    }
  insb8[4]=0x07; // read unique id
  if(!IsoRead(insb8,buff) || !Status() || buff[1]!=0x05) {
    PRINTF(L_SC_ERROR,"failed to read ua");
    return false;
    }
  SetCard(new cCardViaccess(&buff[2]));
  PRINTF(L_SC_INIT,"card UA: %llu",Bin2LongLong(&buff[2],5));
  infoStr.Printf("Card v.%s UA %010llu\n",ver,Bin2LongLong(&buff[2],5));

  insa4[2]=0x00; // select issuer 0
  if(!IsoWrite(insa4,buff) || !Status()) {
    PRINTF(L_SC_ERROR,"failed to select issuer 0");
    return false;
    }
  do {
    insc0[4]=0x1a; // show provider properties
    if(!IsoRead(insc0,buff) || !Status()) {
      PRINTF(L_SC_ERROR,"failed to read prov properties");
      return false;
      }

    unsigned char buff2[MAX_LEN];
    insac[2]=0xa5; // request sa
    if(!IsoWrite(insac,buff2) || !Status()) {
      PRINTF(L_SC_ERROR,"failed to request sa");
      return false;
      }
    insb8[4]=0x06; // read sa
    if(!IsoRead(insb8,buff2) || !Status()) {
      PRINTF(L_SC_ERROR,"failed to read sa");
      return false;
      }
    cProviderScViaccess *p=new cProviderScViaccess(&buff[0],&buff2[2]);
    if(p) {
      AddProv(p);
      memcpy(p->availKeys,buff+10,sizeof(p->availKeys));

      insac[2]=0xa7; // request name
      if(!IsoWrite(insac,buff) || !Status()) {
        PRINTF(L_SC_ERROR,"failed to request prov name");
        return false;
        }
      insb8[4]=0x02; // read name nano + len
      if(!IsoRead(insb8,buff) || !Status()) {
        PRINTF(L_SC_ERROR,"failed to read prov name length");
        return false;
        }
      unsigned int nameLen=buff[1];
      if(nameLen>=sizeof(p->name)) {
        PRINTF(L_SC_ERROR,"provider name buffer overflow");
        nameLen=sizeof(p->name)-1;
        }
      insb8[4]=nameLen;
      if(!IsoRead(insb8,buff) || !Status()) {
        PRINTF(L_SC_ERROR,"failed to read prov name");
        return false;
        }
      memcpy(p->name,buff,nameLen); p->name[nameLen]=0;

      PRINTF(L_SC_INIT,"provider %06x (%s)",(int)p->ProvId(),p->name);
      infoStr.Printf("Prov %06x (%s) SA %08u\n",(int)p->ProvId(),p->name,Bin2Int(&buff2[2],4));
      }
    else PRINTF(L_SC_ERROR,"no memory for provider");

    insa4[2]=0x02; // next issuer
    if(!IsoWrite(insa4,buff) || !Status()) {
      PRINTF(L_SC_ERROR,"failed to select next issuer");
      return false;
      }
    } while(GetSW1()==0x00);
  infoStr.Finish();
  return true;
}

bool cSmartCardViaccess::CheckKey(cProviderScViaccess *p, const unsigned char keynr)
{
  for(unsigned int j=0; j<sizeof(p->availKeys); j++)
    if(p->availKeys[j]==keynr) return true;
  return false;
}

bool cSmartCardViaccess::SetProvider(const unsigned char *id)
{
  static const unsigned char insa4[] = { 0xca,0xa4,0x04,0x00,0x03 };

  if(id[0]!=lastId[0] || id[1]!=lastId[1] || (id[2]&0xF0)!=lastId[2]) {
    memcpy(lastId,id,3);
    lastId[2]&=0xF0;
    if(!IsoWrite(insa4,lastId) || !Status()) return false;
    }
  return true;
}

bool cSmartCardViaccess::Decode(const cEcmInfo *ecm, const unsigned char *data, unsigned char *cw)
{
  static unsigned char ins88[] = { 0xca,0x88,0x00,0x00,0x00 }; // set ecm
  static unsigned char insf8[] = { 0xca,0xf8,0x00,0x00,0x00 }; // set geographic info 
  static unsigned char insc0[] = { 0xca,0xc0,0x00,0x00,0x12 }; // read dcw

  cProviderScViaccess *p=(cProviderScViaccess *)FindProv(data);
  if(p) {
    unsigned char keynr=cParseViaccess::KeyNr(data);
    if(CheckKey(p,keynr)) {
      if(!SetProvider(cParseViaccess::ProvIdPtr(data))) return false;

      const unsigned char *start=cParseViaccess::NanoStart(data)+5;
      const unsigned char *ecm88Data=start;
      int ecm88Len=SCT_LEN(data)-(start-data);
      int ecmf8Len=0;
      while(ecm88Len>0 && ecm88Data[0]<0xA0) {
        const int nanoLen=ecm88Data[1]+2;
        ecmf8Len+=nanoLen;
        ecm88Len-=nanoLen; ecm88Data+=nanoLen;
        }
      if(ecmf8Len) {
        insf8[3]=keynr;
        insf8[4]=ecmf8Len;
        if(!IsoWrite(insf8,start) || !Status()) return false;
        }

      ins88[2]=ecmf8Len?1:0;
      ins88[3]=keynr;
      ins88[4]=ecm88Len;
      if(!IsoWrite(ins88,ecm88Data) || !Status()) return false; // request dcw
      unsigned char buff[MAX_LEN];
      if(!IsoRead(insc0,buff) || !Status()) return false; // read dcw
      switch(buff[0]) {
        case 0xe8: // even
          if(buff[1]==8) { memcpy(cw,buff+2,8); return true; }
          break;
        case 0xe9: // odd
          if(buff[1]==8) { memcpy(cw+8,buff+2,8); return true; }
          break;
        case 0xea: // complete
          if(buff[1]==16) { memcpy(cw,buff+2,16); return true; }
          break;
        }
      }
    else PRINTF(L_SC_ERROR,"key not found on card");
    }
  else PRINTF(L_SC_ERROR,"provider not found on card");
  return false;
}

bool cSmartCardViaccess::Update(int pid, int caid, const unsigned char *data)
{
  static unsigned char insf0[] = { 0xca,0xf0,0x00,0x00,0x00 };
  static unsigned char ins18[] = { 0xca,0x18,0x00,0x00,0x00 };

  int updtype;
  cAssembleData ad(data);
  if(MatchAndAssemble(&ad,&updtype,0)) {
    while((data=ad.Assembled())) {
      const unsigned char *start=cParseViaccess::NanoStart(data);
      int nanolen=SCT_LEN(data)-(start-data);

      if(start && start[0]==0x90 && start[1]==0x03 && nanolen>=5 && SetProvider(&start[2])) {
        insf0[3]=ins18[3]=cParseViaccess::KeyNrFromNano(start);
        start+=5; nanolen-=5;

        int n;
        if(start[0]==0x9e && nanolen>=(n=start[1]+2)) {
          insf0[4]=n;
          if(!IsoWrite(insf0,start) || !Status()) continue;
          start+=n; nanolen-=n;
          }

        if(nanolen>0) {
          ins18[2]=updtype>0 ? updtype-1 : updtype;
          ins18[4]=nanolen;
          if(IsoWrite(ins18,start)) Status();
          }
        }
      }
    return true;
    }
  return false;
}

// -- cSmartCardLinkViaccess -------------------------------------------------------------

class cSmartCardLinkViaccess : public cSmartCardLink {
public:
  cSmartCardLinkViaccess(void):cSmartCardLink(SC_NAME,SC_ID) {}
  virtual cSmartCard *Create(void) { return new cSmartCardViaccess(); }
  };

static cSmartCardLinkViaccess staticScInit;
