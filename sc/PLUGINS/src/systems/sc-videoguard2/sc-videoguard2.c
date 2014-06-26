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

#include "system-common.h"
#include "smartcard.h"
#include "data.h"
#include "crypto.h"
#include "misc.h"
#include "parse.h"
#include "helper.h"
#include "log-sc.h"
#include "log-core.h"
#include "version.h"

SCAPIVERSTAG();

#define SYSTEM_VIDEOGUARD2   0x0900

#define SYSTEM_NAME          "SC-VideoGuard2"
#define SYSTEM_PRI           -5

#define SC_NAME "VideoGuard2"
#define SC_ID   MAKE_SC_ID('V','i','G','2')

#define L_SC        17
#define L_SC_ALL    LALL(L_SC_LASTDEF)

static const struct LogModule lm_sc = {
  (LMOD_ENABLE|L_SC_ALL)&LOPT_MASK,
  (LMOD_ENABLE|L_SC_DEFDEF)&LOPT_MASK,
  "sc-videoguard2",
  { L_SC_DEFNAMES }
  };
ADD_MODULE(L_SC,lm_sc)

// -- cSystemScVideoGuard2 ---------------------------------------------------------------

class cSystemScVideoGuard2 : public cSystemScCore {
public:
  cSystemScVideoGuard2(void);
  };

cSystemScVideoGuard2::cSystemScVideoGuard2(void)
:cSystemScCore(SYSTEM_NAME,SYSTEM_PRI,SC_ID,"SC VideoGuard2")
{
  hasLogger=true;
}

// -- cSystemLinkScVideoGuard2 --------------------------------------------------------

class cSystemLinkScVideoGuard2 : public cSystemLink {
public:
  cSystemLinkScVideoGuard2(void);
  virtual bool CanHandle(unsigned short SysId);
  virtual cSystem *Create(void) { return new cSystemScVideoGuard2; }
  };

static cSystemLinkScVideoGuard2 staticInit;

cSystemLinkScVideoGuard2::cSystemLinkScVideoGuard2(void)
:cSystemLink(SYSTEM_NAME,SYSTEM_PRI)
{
  Feature.NeedsSmartCard();
}

bool cSystemLinkScVideoGuard2::CanHandle(unsigned short SysId)
{
  bool res=false;
  cSmartCard *card=smartcards.LockCard(SC_ID);
  if(card) {
    res=card->CanHandle(SysId);
    smartcards.ReleaseCard(card);
    }
  return res;
}

// -- cSmartCardDataVideoGuard2 -----------------------------------------------------

enum eDataType { dtBoxId, dtSeed };

class cSmartCardDataVideoGuard2 : public cSmartCardData {
public:
  eDataType type;
  unsigned char boxid[4];
  unsigned char seed0[64], seed1[64];
  //
  cSmartCardDataVideoGuard2(void);
  cSmartCardDataVideoGuard2(eDataType Type);
  virtual bool Parse(const char *line);
  virtual bool Matches(cSmartCardData *param);
  };

cSmartCardDataVideoGuard2::cSmartCardDataVideoGuard2(void)
:cSmartCardData(SC_ID)
{}

cSmartCardDataVideoGuard2::cSmartCardDataVideoGuard2(eDataType Type)
:cSmartCardData(SC_ID)
{
  type=Type;
}

bool cSmartCardDataVideoGuard2::Matches(cSmartCardData *param)
{
  cSmartCardDataVideoGuard2 *cd=(cSmartCardDataVideoGuard2 *)param;
  return cd->type==type;
}

bool cSmartCardDataVideoGuard2::Parse(const char *line)
{
  line=skipspace(line);
  if(!strncasecmp(line,"BOXID",5))     { type=dtBoxId; line+=5; }
  else if(!strncasecmp(line,"SEED",4)) { type=dtSeed; line+=4; }
  else {
    PRINTF(L_CORE_LOAD,"smartcarddatavideoguard2: format error: datatype");
    return false;
    }
  line=skipspace(line);
  switch(type) {
    case dtBoxId:
      if(GetHex(line,boxid,sizeof(boxid))!=sizeof(boxid)) {
        PRINTF(L_CORE_LOAD,"smartcarddatavideoguard2: format error: boxid");
        return false;
        }
      break;
    case dtSeed:
      if(GetHex(line,seed0,sizeof(seed0))!=sizeof(seed0)) {
        PRINTF(L_CORE_LOAD,"smartcarddatacryptoworks: format error: seed0");
        return false;
        }
      line=skipspace(line);
      if(GetHex(line,seed1,sizeof(seed1))!=sizeof(seed1)) {
        PRINTF(L_CORE_LOAD,"smartcarddatacryptoworks: format error: seed1");
        return false;
        }
      break;
    }
  return true;
}

// -- cCamCryptVG2 -------------------------------------------------------------

#define xor16(v1,v2,d) xxor((d),16,(v1),(v2))
#define val_by2on3(x)  ((0xaaab*(x))>>16) //fixed point *2/3

class cCamCryptVG2 : private cAES {
private:
  unsigned short cardkeys[3][32];
  unsigned char stateD3A[16];
  //
  void LongMult(unsigned short *pData, unsigned short *pLen, unsigned int mult, unsigned int carry);
  void PartialMod(unsigned short val, unsigned int count, unsigned short *outkey, const unsigned short *inkey);
  void RotateRightAndHash(unsigned char *p);
  void Reorder16A(unsigned char *dest, const unsigned char *src);
  void ReorderAndEncrypt(unsigned char *p);
  //
  void Process_D0(const unsigned char *ins, unsigned char *data, const unsigned char *status);
  void Process_D1(const unsigned char *ins, unsigned char *data, const unsigned char *status);
  void Decrypt_D3(unsigned char *ins, unsigned char *data, const unsigned char *status);
public:
  void PostProcess_Decrypt(unsigned char *buff, int len, unsigned char *cw1, unsigned char *cw2);
  void SetSeed(const unsigned char *Key1, const unsigned char *Key2);
  void GetCamKey(unsigned char *buff);
};

void cCamCryptVG2::SetSeed(const unsigned char *Key1, const unsigned char *Key2)
{
  memcpy(cardkeys[1],Key1,sizeof(cardkeys[1]));
  memcpy(cardkeys[2],Key2,sizeof(cardkeys[2]));
}

void cCamCryptVG2::GetCamKey(unsigned char *buff)
{
  unsigned short *tb2=(unsigned short *)buff, c=1;
  memset(tb2,0,64);
  tb2[0]=1;
  for(int i=0; i<32; i++) LongMult(tb2,&c,cardkeys[1][i],0);
}

void cCamCryptVG2::PostProcess_Decrypt(unsigned char *buff, int len, unsigned char *cw1, unsigned char *cw2)
{
  switch(buff[0]) {
    case 0xD0:
      Process_D0(buff,buff+5,buff+buff[4]+5);
      break;
    case 0xD1:
      Process_D1(buff,buff+5,buff+buff[4]+5);
      break;
    case 0xD3:
      Decrypt_D3(buff,buff+5,buff+buff[4]+5);
      if(buff[1]==0x54) {
        memcpy(cw1,buff+5,8);
        for(int ind=14; ind<len+5;) {
          if(buff[ind]==0x25) {
            memcpy(cw2,buff+5+ind+2,8);
            break;
            }
          if(buff[ind+1]==0) break;
          ind+=buff[ind+1];
          }
        }
      break;
    }
}

void cCamCryptVG2::Process_D0(const unsigned char *ins, unsigned char *data, const unsigned char *status)
{
  switch(ins[1]) {
    case 0xb4:
      memcpy(cardkeys[0],data,sizeof(cardkeys[0]));
      break;
    case 0xbc: 
      {
      unsigned short *idata=(unsigned short *)data;
      const unsigned short *key1=(const unsigned short *)cardkeys[1];
      unsigned short key2[32];
      memcpy(key2,cardkeys[2],sizeof(key2));
      for(int count2=0; count2<32; count2++) {
        unsigned int rem=0, div=key1[count2];
        for(int i=31; i>=0; i--) {
          unsigned int x=idata[i] | (rem<<16);
          rem=(x%div)&0xffff;
          }
        unsigned int carry=1, t=val_by2on3(div) | 1;
        while(t) {
          if(t&1) carry=((carry*rem)%div)&0xffff;
          rem=((rem*rem)%div)&0xffff;
          t>>=1;
          }
        PartialMod(carry,count2,key2,key1);
        }
      unsigned short idatacount=0;
      for(int i=31; i>=0; i--) LongMult(idata,&idatacount,key1[i],key2[i]);
      unsigned char stateD1[16];
      Reorder16A(stateD1,data);
      cAES::SetKey(stateD1);
      break;
      }
  }
}

void cCamCryptVG2::Process_D1(const unsigned char *ins, unsigned char *data, const unsigned char *status)
{
  unsigned char iter[16], tmp[16];
  memset(iter,0,sizeof(iter));
  memcpy(iter,ins,5);
  xor16(iter,stateD3A,iter);
  memcpy(stateD3A,iter,sizeof(iter));

  int datalen=status-data;
  int datalen1=datalen;
  if(datalen<0) datalen1+=15;
  int blocklen=datalen1>>4;
  for(int i=0,iblock=0; i<blocklen+2; i++,iblock+=16) {
    unsigned char in[16];
    if(blocklen==i) {
      memset(in,0,sizeof(in));
      memcpy(in,&data[iblock],datalen-(datalen1&~0xf));
      }
    else if(blocklen+1==i) {
      memset(in,0,sizeof(in));
      memcpy(&in[5],status,2);
      }
    else
      memcpy(in,&data[iblock],sizeof(in));

    xor16(iter,in,tmp);
    ReorderAndEncrypt(tmp);
    xor16(tmp,stateD3A,iter);
    }
  memcpy(stateD3A,tmp,16);
}

void cCamCryptVG2::Decrypt_D3(unsigned char *ins, unsigned char *data, const unsigned char *status)
{
  if(ins[4]>16) ins[4]-=16;
  if(ins[1]==0xbe) memset(stateD3A,0,sizeof(stateD3A));

  unsigned char tmp[16];
  memset(tmp,0,sizeof(tmp));
  memcpy(tmp,ins,5);
  xor16(tmp,stateD3A,stateD3A);

  int len1=ins[4];
  int blocklen=len1>>4;
  if(ins[1]!=0xbe) blocklen++;

  unsigned char iter[16], states[16][16];
  memset(iter,0,sizeof(iter));
  for(int blockindex=0; blockindex<blocklen; blockindex++) {
    iter[0]+=blockindex;
    xor16(iter,stateD3A,iter);
    ReorderAndEncrypt(iter);
    xor16(iter,&data[blockindex*16],states[blockindex]);
    if(blockindex==(len1>>4)) {
      int c=len1-(blockindex*16);
      if(c<16) memset(&states[blockindex][c],0,16-c);
      }
    xor16(states[blockindex],stateD3A,stateD3A);
    RotateRightAndHash(stateD3A);
    }
  memset(tmp,0,sizeof(tmp));
  memcpy(tmp+5,status,2);
  xor16(tmp,stateD3A,stateD3A);
  ReorderAndEncrypt(stateD3A);

  memcpy(stateD3A,status-16,sizeof(stateD3A));
  ReorderAndEncrypt(stateD3A);

  memcpy(data,states[0],len1);
  if(ins[1]==0xbe) {
    Reorder16A(tmp,states[0]);
    cAES::SetKey(tmp);
    }
}

void cCamCryptVG2::ReorderAndEncrypt(unsigned char *p)
{
  unsigned char tmp[16];
  Reorder16A(tmp,p); cAES::Encrypt(tmp,16,tmp); Reorder16A(p,tmp);
}

// reorder AAAABBBBCCCCDDDD to ABCDABCDABCDABCD

void cCamCryptVG2::Reorder16A(unsigned char *dest, const unsigned char *src)
{
  for(int i=0,k=0; i<4; i++)
    for(int j=i; j<16; j+=4,k++)
      dest[k]=src[j];
}

void cCamCryptVG2::LongMult(unsigned short *pData, unsigned short *pLen, unsigned int mult, unsigned int carry)
{
  for(int i=0; i<*pLen; i++) {
    carry+=pData[i]*mult;
    pData[i]=(unsigned short)carry;
    carry>>=16;
    }
  if(carry) pData[(*pLen)++]=carry;
}

void cCamCryptVG2::PartialMod(unsigned short val, unsigned int count, unsigned short *outkey, const unsigned short *inkey)
{
  if(count) {
    unsigned int mod=inkey[count];
    unsigned short mult=(inkey[count]-outkey[count-1])&0xffff;
    for(unsigned int i=0,ib1=count-2; i<count-1; i++,ib1--) {
      unsigned int t=(inkey[ib1]*mult)%mod;
      mult=t-outkey[ib1];
      if(mult>t) mult+=mod;
      }
    mult+=val;
    if((val>mult) || (mod<mult)) mult-=mod;
    outkey[count]=(outkey[count]*mult)%mod;
    }
  else 
    outkey[0]=val;
}

static const unsigned char table1[256] = {
  0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5, 0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
  0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0, 0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
  0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc, 0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
  0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a, 0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
  0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0, 0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
  0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b, 0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
  0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85, 0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
  0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5, 0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
  0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17, 0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
  0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88, 0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
  0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c, 0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
  0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9, 0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
  0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6, 0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
  0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e, 0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
  0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94, 0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
  0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68, 0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16,
  };

void cCamCryptVG2::RotateRightAndHash(unsigned char *p)
{
  unsigned char t1=p[15];
  for(int i=0; i<16; i++) {
    unsigned char t2=t1;
    t1=p[i]; p[i]=table1[(t1>>1)|((t2&1)<<7)];
    }
}

// -- cCmdTable ----------------------------------------------------------------

struct CmdTabEntry {
  unsigned char cla;
  unsigned char cmd;
  unsigned char len;
  unsigned char mode;
};

struct CmdTab {
  unsigned char index;
  unsigned char size;
  unsigned char Nentries; 
  unsigned char dummy;
  CmdTabEntry e[1];
};

class CmdTable {
private:
  struct CmdTab *tab;
public:
  CmdTable(const unsigned char *mem, int size);
  ~CmdTable();
  bool GetInfo(const unsigned char *cmd, unsigned char &rlen, unsigned char &rmode);
  };

CmdTable::CmdTable(const unsigned char *mem, int size)
{
  tab=(struct CmdTab *)new unsigned char[size];
  memcpy(tab,mem,size);
}

CmdTable::~CmdTable()
{
  delete[] tab;
}

bool CmdTable::GetInfo(const unsigned char *cmd, unsigned char &rlen, unsigned char & rmode)
{
  struct CmdTabEntry *pcte=tab->e;
  for(int i=0; i<tab->Nentries; i++,pcte++)
    if(cmd[1]==pcte->cmd) {
      rlen=pcte->len;
      rmode=pcte->mode;
      return true;
      }
  return false;
}

// -- cSmartCardVideoGuard2 -----------------------------------------------------------

#define BASEYEAR 2000 // for date calculation

class cSmartCardVideoGuard2 : public cSmartCard, private cIdSet {
private:
  unsigned char CW1[8], CW2[8];
  unsigned char cardID[4], groupID[4], boxID[4];
  unsigned int CAID;
  cCamCryptVG2 state;
  CmdTable *cmdList;
public:
  void ReadTiers(void);
  void RevDateCalc(const unsigned char *Date, int &year, int &mon, int &day, int &hh, int &mm, int &ss);
  void Datecalc(int year, int mon, int day, int hh, int mm, int ss , unsigned char * Date);
  int DoCmd(const unsigned char *ins, const unsigned char *txbuff=0, unsigned char *rxbuff=0);
  int ReadCmdLen(const unsigned char *cmd);
public:
  cSmartCardVideoGuard2(void);
  ~cSmartCardVideoGuard2();
  virtual bool Init(void);
  virtual bool Decode(const cEcmInfo *ecm, const unsigned char *data, unsigned char *cw);
  virtual bool Update(int pid, int caid, const unsigned char *data);
  virtual bool CanHandle(unsigned short SysId);
  };

static const struct StatusMsg msgs[] = {
  { { 0x90,0x00 }, "Instruction executed without errors, noupdate, nofilter, IRD# not set", true },
  { { 0x90,0x01 }, "Instruction executed without errors, update, nofilter, IRD# not set", true },
  { { 0x90,0x20 }, "Instruction executed without errors, noupdate, nofilter, IRD# set", true },
  { { 0x90,0x21 }, "Instruction executed without errors, update, nofilter, IRD# set", true },
  { { 0x90,0x80 }, "Instruction executed without errors, noupdate, filter, IRD# not set", true },
  { { 0x90,0x81 }, "Instruction executed without errors, update, filter, IRD# not set", true },
  { { 0x90,0xa0 }, "Instruction executed without errors, noupdate, filter, IRD# set", true },
  { { 0x90,0xa1 }, "Instruction executed without errors, update, filter, IRD# set", true },
  { { 0x91,0x00 }, "Instruction executed without errors, noupdate, nofilter, IRD# not set", true },
  { { 0x91,0x01 }, "Instruction executed without errors, update, nofilter, IRD# not set", true },
  { { 0x91,0x20 }, "Instruction executed without errors, noupdate, nofilter, IRD# set", true },
  { { 0x91,0x21 }, "Instruction executed without errors, update, nofilter, IRD# set", true },
  { { 0x91,0x80 }, "Instruction executed without errors, noupdate, filter, IRD# not set", true },
  { { 0x91,0x81 }, "Instruction executed without errors, update, filter, IRD# not set", true },
  { { 0x91,0xa0 }, "Instruction executed without errors, noupdate, filter, IRD# set", true },
  { { 0x91,0xa1 }, "Instruction executed without errors, update, filter, IRD# set", true },
  { { 0xFF,0xFF }, 0, false }
  };

static const struct CardConfig cardCfg = {
  SM_8O2,2000,1400
  };

static const struct CardConfig cardCfgDelay = {
  SM_8O2,2000,1400,20
  };

cSmartCardVideoGuard2::cSmartCardVideoGuard2(void)
:cSmartCard(&cardCfg,msgs)
{
  cmdList=0; CAID=0;
}

cSmartCardVideoGuard2::~cSmartCardVideoGuard2(void)
{
  delete cmdList;
}

bool cSmartCardVideoGuard2::CanHandle(unsigned short SysId)
{
  return SysId==CAID;
}

bool cSmartCardVideoGuard2::Init(void)
{
  static const unsigned char vg2Hist[] = { 'i',0xff,'J','P' };
  if(atr->histLen<4 || memcmp(&atr->hist[3],vg2Hist,4)) {
    PRINTF(L_SC_INIT,"doesn't look like a VideoGuard2 card");
    return false;
    }

  infoStr.Begin();
  infoStr.Strcat("VideoGuard2 smartcard\n");
  snprintf(idStr,sizeof(idStr),"%s (%c%c.%d)",SC_NAME,atr->hist[10],atr->hist[11],atr->hist[12]);
  
  ResetIdSet();
  delete cmdList; cmdList=0;

  static unsigned char ins7401[] = { 0xD0,0x74,0x01,0x00,0x00 };
  int l;
  if((l=ReadCmdLen(ins7401))<0) {
    PRINTF(L_SC_ERROR,"bogus answer. Now try delayed mode");
    NewCardConfig(&cardCfgDelay);
    if((l=ReadCmdLen(ins7401))<0) return false;
    }
  ins7401[4]=l;
  unsigned char buff[256];
  if(!IsoRead(ins7401,buff) || !Status()) {
    PRINTF(L_SC_ERROR,"failed to read cmd list");
    return false;
    }
  cmdList=new CmdTable(buff,l);

  static const unsigned char ins7416[5] = { 0xD0,0x74,0x16,0x00,0x00 };
  if(DoCmd(ins7416)<0 || !Status()) {
    PRINTF(L_SC_ERROR,"cmd 7416 failed");
    return false;
    }

  static const unsigned char ins36[5] = { 0xD0,0x36,0x00,0x00,0x00 };
  bool boxidOK=false;
  if((l=DoCmd(ins36,0,buff))>0 && Status())
    for(int i=0; i<l ;i++) {
      if(buff[i]==0x00 && buff[i+1]==0xF3) {
        memcpy(&boxID,&buff[i+2],sizeof(boxID));
        boxidOK=true;
        break;
        }
      }
  if(!boxidOK) {
    cSmartCardDataVideoGuard2 cd(dtBoxId);
    cSmartCardDataVideoGuard2 *entry=(cSmartCardDataVideoGuard2 *)smartcards.FindCardData(&cd);
    if(entry) {
      memcpy(&boxID,entry->boxid,sizeof(boxID));
      boxidOK=true;
      }
    }
  if(!boxidOK) {
    PRINTF(L_SC_ERROR,"no boxID available");
    return false;
    }

  static const unsigned char ins4C[5] = { 0xD0,0x4C,0x00,0x00,0x00 }; 
  static unsigned char payload4C[9] = { 0,0,0,0, 3,0,0,2,4 };
  memcpy(payload4C,boxID,4);
  if(DoCmd(ins4C,payload4C)<0 || !Status()) {
    PRINTF(L_SC_ERROR,"sending boxid failed");
    return false;
    }

  static const unsigned char ins58[5] = { 0xD0,0x58,0x00,0x00,0x00 };
  if(DoCmd(ins58,0,buff)<0 || !Status()) {
    PRINTF(L_SC_ERROR,"failed to read card details");
    return false;
    }
  
  unsigned int c=WORD(buff,0x1D,0xFFFF);
  if(c!=CAID) CaidsChanged();
  CAID=c;
  memcpy(&cardID,&buff[8],4);
  memcpy(&groupID,&buff[8],4); groupID[3]=0;
  SetCard(new cCardNDS(cardID));
  AddProv(new cProviderNDS(groupID));
  char str[20], str2[20];
  infoStr.Printf("Cardtype: %c%c.%d\n"
                 "BoxID %s Caid %04x CardID %s\n",
                 atr->hist[10],atr->hist[11],atr->hist[12],HexStr(str,boxID,4),CAID,HexStr(str2,cardID,4));
  PRINTF(L_SC_INIT,"cardtype: %c%c.%d boxID %s caid %04x cardID %s",atr->hist[10],atr->hist[11],atr->hist[12],HexStr(str,boxID,4),CAID,HexStr(str2,cardID,4));

  cSmartCardDataVideoGuard2 cd(dtSeed);
  cSmartCardDataVideoGuard2 *entry=(cSmartCardDataVideoGuard2 *)smartcards.FindCardData(&cd);
  if(!entry) {
    PRINTF(L_SC_ERROR,"no NDS seed available");
    return false;
    }
  state.SetSeed(entry->seed0,entry->seed1);
  unsigned char tbuff[64];
  state.GetCamKey(tbuff);

  static const unsigned char insB4[5] = { 0xD0,0xB4,0x00,0x00,0x00 };
  if(DoCmd(insB4,tbuff)<0 || !Status()) {
    PRINTF(L_SC_ERROR,"cmd D0B4 failed");
    return false;
    }
  
  static const unsigned char insBC[5] = { 0xD0,0xBC,0x00,0x00,0x00 };
  if(DoCmd(insBC)<0 || !Status()) {
    PRINTF(L_SC_ERROR,"cmd D0BC failed");
    return false;
    }
  static const unsigned char insBE[5] = { 0xD3,0xBE,0x00,0x00,0x00 };
  if(DoCmd(insBE)<0 || !Status()) {
    PRINTF(L_SC_ERROR,"cmd D3BE failed");
    return false;
    }

  static const unsigned char ins58a[5] = { 0xD1,0x58,0x00,0x00,0x00 }; 
  if(DoCmd(ins58a)<0 || !Status()) {
    PRINTF(L_SC_ERROR,"cmd D158 failed");
    return false;
    }
  static const unsigned char ins4Ca[5] = { 0xD1,0x4C,0x00,0x00,0x00 }; 
  if(DoCmd(ins4Ca,payload4C)<0 || !Status()) {
    PRINTF(L_SC_ERROR,"cmd D14C failed");
    return false;
    }
  ReadTiers();
  return true;
}

bool cSmartCardVideoGuard2::Decode(const cEcmInfo *ecm, const unsigned char *data, unsigned char *cw)
{
  static unsigned char ins40[5] = { 0xD1,0x40,0x40,0x80,0xFF };
  static const unsigned char ins54[5] = { 0xD3,0x54,0x00,0x00,0x00 };
  int posECMpart2=data[6]+7;
  int lenECMpart2=data[posECMpart2]+1;
  unsigned char tbuff[264];
  tbuff[0]=0;
  memcpy(&tbuff[1],&data[posECMpart2+1],lenECMpart2);
  ins40[4]=lenECMpart2;
  if(DoCmd(ins40,tbuff)>0 && Status()) {
    if(DoCmd(ins54)>0 && Status()) {
      if(data[0]&1) memcpy(cw+8,CW1,8);
      else          memcpy(cw+0,CW1,8);
      return true;
      }
    }
  return false;
}

bool cSmartCardVideoGuard2::Update(int pid, int caid, const unsigned char *data)
{
  static unsigned char ins42[5] = { 0xD1,0x42,0x00,0x00,0xFF }; 
  if(MatchEMM(data)) {
    const unsigned char *payloaddata=cParseNDS::PayloadStart(data,cardID); //points to 02 xx yy
    int lenEMM;
    switch(payloaddata[0]) {
      case 2:
        lenEMM=payloaddata[payloaddata[1]+2];
        payloaddata+=3+payloaddata[1]; // skip len specifier
        break;
      default:
//        di(printf("scvg2: ***ERROR***: EMM: bad payload type byte %02x\n",payloaddata[0]));
//        DUMP3("VG2EMM",data,32);
        return false;
      }
//    di(printf("scvg2: EMM: pid %d (%x) caid %d (%x) len %d (%x)\n",pid,pid,caid,caid,lenEMM,lenEMM));
//    DUMP3("VG2EMM",data,32);
    if(lenEMM<=8 || lenEMM>188) {
//      di(printf("scvg2: ***ERROR***: EMM: len %d bad\n",lenEMM));
      return false;
      }
    ins42[4]=lenEMM;
    if(DoCmd(ins42,payloaddata)>0 && Status())
      return true;
    }
  return false;
}

/*
bool cSmartCardVideoGuard2::ZKT(void)
{
  static unsigned char payload4A[1] = { 0x01 };
  static unsigned char ins4A[5] = { 0xD0,0x4A,0x15,0x01,0x01 };
  DoCmd(ins4A,payload4A);
  static unsigned char ins5A1[5] = { 0xD0,0x5A,0x15,0x01,0x10 };
  DoCmd(ins5A1);
  static unsigned char ins5A2[5] = { 0xD0,0x5A,0x11,0x02,0x40 };
  DoCmd(ins5A2);
  DoCmd(ins4A,payload4A);
  DoCmd(ins5A1);
  static unsigned char ins5A3[5] = { 0xD0,0x5A,0x10,0x02,0x40 };
  DoCmd(ins5A3);
  return true;
}
*/

void cSmartCardVideoGuard2::ReadTiers(void)
{
  static const unsigned char ins2a[5] = { 0xd0,0x2a,0x00,0x00,0x00 };
  if(DoCmd(ins2a)<0 || !Status()) return;
  static unsigned char ins76[5] = { 0xd0,0x76,0x00,0x00,0x00 };
  ins76[3]=0x7f; ins76[4]=2;
  unsigned char buff[270];
  if(!IsoRead(ins76,buff) || !Status()) return;
  ins76[3]=0; ins76[4]=0;
  int num=buff[1];
  if(num>0) {
    infoStr.Strcat("Tier\tExpiry Date\n");
    infoStr.Strcat("----\t-----------\n");
    }
  for(int i=0; i<num; i++) {
    ins76[2]=i;
    if(DoCmd(ins76,0,buff)<0 || !Status()) return;
    if(buff[5+2]==0 && buff[5+3]==0) break;
    int y,m,d,H,M,S;
    RevDateCalc(&buff[5+4],y,m,d,H,M,S);
    char str[100];
    snprintf(str,sizeof(str),"%02x%02x\t%04d/%02d/%02d-%02d:%02d:%02d\n",buff[5+2],buff[5+3],y,m,d,H,M,S);
    infoStr.Strcat(str);
    PRINTF(L_SC_INIT,"Tier %02x%02x expires %04d/%02d/%02d-%02d:%02d:%02d",buff[5+2],buff[5+3],y,m,d,H,M,S);
    }
}

void cSmartCardVideoGuard2::RevDateCalc(const unsigned char *Date, int &year, int &mon, int &day, int &hh, int &mm, int &ss)
{ 
  year=(Date[0]/12)+BASEYEAR; 
  mon=(Date[0]%12)+1; 
  day=Date[1]; 
  hh=Date[2]/8; 
  mm=(0x100*(Date[2]-hh*8)+Date[3])/32; 
  ss=(Date[3]-mm*32)*2; 
}

void cSmartCardVideoGuard2::Datecalc(int year, int mon, int day, int hh, int mm, int ss , unsigned char *Date)
{ 
  Date[0]=((year-BASEYEAR)*12 + (mon-1)); 
  Date[1]=day; 
  Date[2]=hh*8+mm/8; 
  Date[3]=ss/2+mm*32; 
} 

int cSmartCardVideoGuard2::DoCmd(const unsigned char *ins, const unsigned char *txbuff, unsigned char *rxbuff)
{
  unsigned char ins2[5];
  memcpy(ins2,ins,5);
  unsigned char len=0, mode=0;
  if(cmdList->GetInfo(ins2,len,mode)) {
    if(len==0xFF && mode==2) {
      if(ins2[4]==0) ins2[4]=len=ReadCmdLen(ins2);
      }
    else if(mode!=0) ins2[4]=len;
    }
  if(ins2[0]==0xd3) ins2[4]=len+16;
  len=ins2[4];

  unsigned char b[256], tmp[264];
  if(!rxbuff) rxbuff=tmp;
  if(mode>1) {
    if(!IsoRead(ins2,b) || !Status()) return -1;
    memcpy(rxbuff,ins2,5);
    memcpy(rxbuff+5,b,len);
    memcpy(rxbuff+5+len,sb,2);
    }
  else {
    if(!IsoWrite(ins2,txbuff) || !Status()) return -1;
    memcpy(rxbuff,ins2,5);
    memcpy(rxbuff+5,txbuff,len);
    memcpy(rxbuff+5+len,sb,2);
    }
  state.PostProcess_Decrypt(rxbuff,len,CW1,CW2);
  return len;
}

int cSmartCardVideoGuard2::ReadCmdLen(const unsigned char *cmd)
{
  unsigned char cmd2[5], resp[16];
  memcpy(cmd2,cmd,5);
  cmd2[3]=0x80;
  cmd2[4]=1;
  if(!IsoRead(cmd2,resp) || !Status()) {
    PRINTF(L_SC_ERROR,"failed to read %02x%02x cmd length",cmd[1],cmd[2]);
    return -1;
    }
  return resp[0];  
}

// -- cSmartCardLinkVG2 -------------------------------------------------------

class cSmartCardLinkVG2 : public cSmartCardLink {
public:
  cSmartCardLinkVG2(void):cSmartCardLink(SC_NAME,SC_ID) {}
  virtual cSmartCard *Create(void) { return new cSmartCardVideoGuard2; }
  virtual cSmartCardData *CreateData(void) { return new cSmartCardDataVideoGuard2; }
  };

static cSmartCardLinkVG2 staticScInit;
