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

#include <openssl/rand.h>

#include "system-common.h"
#include "smartcard.h"
#include "crypto.h"
#include "helper.h"
#include "log-sc.h"
#include "log-core.h"

#define SYSTEM_NAGRA         0x1801

#define SYSTEM_NAME          "SC-Nagra"
#define SYSTEM_PRI           -5
#define SYSTEM_CAN_HANDLE(x) ((x)==SYSTEM_NAGRA)

#define SC_NAME "Nagra"
#define SC_ID   MAKE_SC_ID('N','a','g','r')

#define L_SC        11
#define L_SC_PROC   LCLASS(L_SC,L_SC_LASTDEF<<1)
#define L_SC_ALL    LALL(L_SC_PROC)

static const struct LogModule lm_sc = {
  (LMOD_ENABLE|L_SC_ALL)&LOPT_MASK,
  (LMOD_ENABLE|L_SC_DEFDEF|L_SC_PROC)&LOPT_MASK,
  "sc-nagra",
  { L_SC_DEFNAMES,"process" }
  };
ADD_MODULE(L_SC,lm_sc)

// -- cSystemScNagra ---------------------------------------------------------------------

class cSystemScNagra : public cSystemScCore {
public:
  cSystemScNagra(void);
  };

cSystemScNagra::cSystemScNagra(void)
:cSystemScCore(SYSTEM_NAME,SYSTEM_PRI,SC_ID,"SC Nagra")
{
  hasLogger=true;
}

// -- cSystemLinkScNagra --------------------------------------------------------------

class cSystemLinkScNagra : public cSystemLink {
public:
  cSystemLinkScNagra(void);
  virtual bool CanHandle(unsigned short SysId);
  virtual cSystem *Create(void) { return new cSystemScNagra; }
  };

static cSystemLinkScNagra staticInit;

cSystemLinkScNagra::cSystemLinkScNagra(void)
:cSystemLink(SYSTEM_NAME,SYSTEM_PRI)
{
  Feature.NeedsSmartCard();
}

bool cSystemLinkScNagra::CanHandle(unsigned short SysId)
{
  return smartcards.HaveCard(SC_ID) && SYSTEM_CAN_HANDLE(SysId);
}

// -- cCamCryptNagra ------------------------------------------------------------------

class cCamCryptNagra {
private:
  cIDEA idea;
  cRSA rsa;
  cBN camMod, camExp;
  bool hasMod;
  unsigned char sk[16];
  //
  void Signature(unsigned char *sig, const unsigned char *key, const unsigned char *msg, int len);
public:
  cCamCryptNagra(void);
  void InitCamKey(unsigned char *key, const unsigned char *bk, const unsigned char *data);
  void SetCamMod(BIGNUM *m);
  bool DecryptCamMod(const unsigned char *dt08, const unsigned char *key, const unsigned char *key2, BIGNUM *m);
  bool DecryptCamData(unsigned char *out, const unsigned char *key, const unsigned char *in);
  bool DecryptCW(unsigned char *dcw, const unsigned char *in);
  };

cCamCryptNagra::cCamCryptNagra(void)
{
  hasMod=false;
  BN_set_word(camExp,3);
}

void cCamCryptNagra::Signature(unsigned char *sig, const unsigned char *key, const unsigned char *msg, int len)
{
  unsigned char buff[16];
  memcpy(buff,key,16);
  for(int i=0; i<len; i+=8) {
    IdeaKS ks;
    idea.SetEncKey(buff,&ks);
    idea.Encrypt(msg+i,8,buff,&ks,0);
    xxor(buff,8,buff,&msg[i]);
    //for(int j=7; j>=0; j--) buff[j]^=msg[i+j];
    memcpy(&buff[8],buff,8);
    }
  memcpy(sig,buff,8);
}

void cCamCryptNagra::InitCamKey(unsigned char *key, const unsigned char *bk, const unsigned char *data)
{
  memcpy(key,bk,8);
  int d=UINT32_LE(data);
  BYTE4_LE(key+8 , d);
  BYTE4_LE(key+12,~d);
  //memcpy(key+8,data,4);
  //for(int i=0; i<4; i++) key[i+12]=~data[i];
}

void cCamCryptNagra::SetCamMod(BIGNUM *m)
{
  BN_copy(camMod,m);
  hasMod=true;
}

bool cCamCryptNagra::DecryptCamMod(const unsigned char *dt08, const unsigned char *key, const unsigned char *cardid, BIGNUM *m)
{
  unsigned char buff[72];
  if(rsa.RSA(buff,dt08+13,64,camExp,m)!=64) return false;
  memcpy(buff+64,dt08+13+64,8);
  buff[63]|=dt08[12]&0x80;
  IdeaKS dks;
  idea.SetDecKey(key,&dks);
  idea.Decrypt(buff,72,&dks,0);
  unsigned char sig[8];
  memcpy(sig,buff,8);

  memset(buff+0,0,4);
  memcpy(buff+4,cardid,4);
  Signature(buff,key,buff,72);
  BN_bin2bn(buff+8,64,camMod);
  if(memcmp(sig,buff,8)) {
    PRINTF(L_SC_PROC,"DT08 signature failed. Check boxkey/IRD modulus");
    return false;
    }
  hasMod=true;
  return true;
}

bool cCamCryptNagra::DecryptCamData(unsigned char *out, const unsigned char *key, const unsigned char *camdata)
{
  if(!hasMod) return false;
  //decrypt $2A data here and prepare $2B reply
  if(rsa.RSA(out,camdata,64,camExp,camMod)!=64) return false;
  Signature(sk,key,out,32);
  memcpy(sk+8,sk,8);
  Signature(sk+8,sk,out,32);;
  if(rsa.RSA(out,out,64,camExp,camMod)!=64) return false;
  LDUMP(L_SC_PROC,sk,16,"established session key ->");
  return true;
}

bool cCamCryptNagra::DecryptCW(unsigned char *dcw, const unsigned char *ecw)
{
  const int nCw=ecw[4]/2;
  if(nCw<10) return false;
  IdeaKS ks;
  ecw+=5;
  idea.SetDecKey(sk,&ks);
  memcpy(dcw+8,ecw    +2,8);
  memcpy(dcw+0,ecw+nCw+2,8);
  idea.Decrypt(dcw+0,8,&ks,0);
  idea.Decrypt(dcw+8,8,&ks,0);
  return true;
}

// -- cSmartCardDataNagra ------------------------------------------------------

class cSmartCardDataNagra : public cSmartCardData {
public:
  bool global;
  int provId;
  unsigned char bk[8];
  cBN mod;
  //
  cSmartCardDataNagra(void);
  cSmartCardDataNagra(int ProvId, bool Global);
  virtual bool Parse(const char *line);
  virtual bool Matches(cSmartCardData *param);
  };

cSmartCardDataNagra::cSmartCardDataNagra(void)
:cSmartCardData(SC_ID) {}

cSmartCardDataNagra::cSmartCardDataNagra(int ProvId, bool Global=false)
:cSmartCardData(SC_ID)
{
  provId=ProvId; global=Global;
}

bool cSmartCardDataNagra::Matches(cSmartCardData *param)
{
  cSmartCardDataNagra *cd=(cSmartCardDataNagra *)param;
  return (cd->provId==provId) && (cd->global==global);
}

bool cSmartCardDataNagra::Parse(const char *line)
{
  unsigned char buff[512];
  line=skipspace(line);
  if(*line++!='[' || GetHex(line,buff,2)!=2 || *(line=skipspace(line))!=']' ) {
    PRINTF(L_CORE_LOAD,"smartcarddatanagra: format error: provider id");
    return false;
    }
  provId=buff[0]*256+buff[1];
  line=skipspace(line+1);
  if(!strncasecmp(line,"IRDMOD",6)) {
    global=true;
    line+=6;
    }
  if(GetHex(line,bk,sizeof(bk),false)<=0) {
    PRINTF(L_CORE_LOAD,"smartcarddatanagra: format error: boxkey");
    return false;
    }
  int l=GetHex(line,buff,sizeof(buff),false);
  if(l!=64) {
    PRINTF(L_CORE_LOAD,"smartcarddatanagra: format error: %s",global?"IRDMOD":"CAMMOD");
    return false;
    }
  BN_bin2bn(buff,l,mod);
  return true;
}

// -- cSmartCardNagra -----------------------------------------------------------------

// ISO 7816 T=1 defines
#define NAD(a)      (a[0])
#define PCB(a)      (a[1])
#define LEN(a)      (a[2])
#define CLA(a)      (a[3])
#define INS(a)      (a[4])
#define P1(a)       (a[5])
#define P2(a)       (a[6])
#define L(a)        (a[7])

// Nagra NAD and reply NAD
#define N2_NAD      0x21
#define N2_R_NAD    sn8(N2_NAD)
#define N2_CLA      0xA0
#define N2_INS      0xCA
#define N2_P1       0x00
#define N2_P2       0x00
#define N2_CMD(a)   (a[8])
#define N2_CLEN(a)  (a[9]) 
#define N2_DATA(a)  (a[10]) 

#define SETIFS      0xC1
#define CAMDATA     0x08

// Datatypes
#define IRDINFO     0x00
#define TIERS       0x05
#define MAX_REC     20

// Card Status checks
#define HAS_CW      ((status[1]&1)&&((status[3]&6)==6))

class cSmartCardNagra : public cSmartCard, private cCamCryptNagra {
private:
  unsigned char buff[MAX_LEN+1], status[4], cardId[4], irdId[4], block;
  unsigned short provId[MAX_REC], irdProvId;
  //
  bool GetCardStatus(void);
  bool GetDataType(unsigned char dt, int len, int shots=MAX_REC);
  bool ParseDataType(unsigned char dt);
  bool DoCamExchange(const unsigned char *key);
  void Date(const unsigned char *date, char *dt, char *ti);
  bool DoBlkCmd(unsigned char cmd, int ilen, unsigned char res, int rlen, const unsigned char *data=0);
  bool SetIFS(unsigned char len);
public:
  cSmartCardNagra(void);
  virtual bool Init(void);
  virtual bool Decode(const cEcmInfo *ecm, const unsigned char *data, unsigned char *cw);
  virtual bool Update(int pid, int caid, const unsigned char *data);
  };

static const struct StatusMsg msgs[] = {
  { { 0x6F,0x00 }, "Instruction not supported", false },
  { { 0x90,0x00 }, "Instruction executed without errors", true },
  { { 0xFF,0xFF }, 0, false }
  };

static const struct CardConfig cardCfg = {
  SM_8O2,500,800
  };

cSmartCardNagra::cSmartCardNagra(void)
:cSmartCard(&cardCfg,msgs)
{
  block=0;
}

bool cSmartCardNagra::SetIFS(unsigned char size)
{
  unsigned char buf[5];
  // NAD, PCB, LEN
  NAD(buf)=N2_NAD; PCB(buf)=SETIFS; LEN(buf)=1;
  // Information Field size
  buf[3]=size; buf[4]=XorSum(buf,4);
  if(SerWrite(buf,5)!=5) {
    PRINTF(L_SC_ERROR,"failed to write IFS command");
    return false;
    }
  if(SerRead(buff,5,cardCfg.workTO)!=5 || XorSum(buff,5)) {
    PRINTF(L_SC_ERROR,"setting IFS to %02x failed",size);
    return false;
    }
  return true;
}

bool cSmartCardNagra::GetCardStatus(void)
{
  if(!DoBlkCmd(0xC0,0x02,0xB0,0x06) || !Status()) {
    PRINTF(L_SC_ERROR,"failed to read card status");
    return false;
    }
  memcpy(status,buff+5,4);
  return true;
}

bool cSmartCardNagra::Init(void)
{
  static const unsigned char verifyBytes[] = { 'D','N','A','S','P','1' };
  if(atr->T!=1 || (atr->histLen<8 && memcmp(atr->hist,verifyBytes,sizeof(verifyBytes)))) {
    PRINTF(L_SC_INIT,"doesn't look like a nagra V2 card");
    return false;
    }

  infoStr.Begin();
  infoStr.Strcat("Nagra smartcard\n");

  char rom[12], rev[12];
  snprintf(rom,sizeof(rom),"%.3s",(char*)&atr->hist[5]);
  snprintf(rev,sizeof(rev),"%.3s",(char*)&atr->hist[12]);

  PRINTF(L_SC_INIT,"card version: %s revision: %s",rom,rev);
  infoStr.Printf("Card v.%s Rev.%s\n",rom,rev);

  if(!GetCardStatus() || !SetIFS(0x80)) return false;
  // get UA/CardID here
  if(!DoBlkCmd(0x12,0x02,0x92,0x06) || !Status()) return false;

  memcpy(cardId,buff+5,4);
  if(!GetDataType(IRDINFO,0x39) || !GetDataType(0x04,0x44)) return false;
  infoStr.Printf("Tiers\n");
  infoStr.Printf("|id  |tier low|tier high| dates    |\n");
  infoStr.Printf("+----+--------+---------+----------+\n");

  if(!GetDataType(TIERS,0x57))
    PRINTF(L_SC_ERROR,"failed to get tiers");
    
  if(!GetDataType(CAMDATA,0x55,1)) return false;

  unsigned char key[16];
  cSmartCardDataNagra *entry=0;
  bool sessOk=false;
  if(buff[5]!=0 && irdProvId==((buff[10]*256)|buff[11])) { // prepare DT08 data
    cSmartCardDataNagra cd(irdProvId,true);
    if(!(entry=(cSmartCardDataNagra *)smartcards.FindCardData(&cd))) {
      PRINTF(L_GEN_WARN,"didn't find smartcard Nagra IRD modulus");
      }
    else {
      InitCamKey(key,entry->bk,irdId);
      if(DecryptCamMod(buff+3,key,cardId,entry->mod)) sessOk=true;
      }
    }
  else {
    cSmartCardDataNagra cd(irdProvId);
    if(!(entry=(cSmartCardDataNagra *)smartcards.FindCardData(&cd))) {
      PRINTF(L_GEN_WARN,"didn't find smartcard Nagra CAM modulus");
      }
    else {
      InitCamKey(key,entry->bk,cardId);
      SetCamMod(entry->mod);
      if(GetDataType(0x08,0x03,1)) sessOk=true;;
      }
    }
  if(sessOk) DoCamExchange(key);
  infoStr.Finish();
  return true;
}

bool cSmartCardNagra::GetDataType(unsigned char dt, int len, int shots)
{
  for(int i=0; i<shots; i++) {
    if(!DoBlkCmd(0x22,0x03,0xA2,len,&dt) || !Status()) {
     PRINTF(L_SC_ERROR,"failed to get datatype %02X",dt);
     return false;
     }
    if(buff[5]==0) return true;
    if(!ParseDataType(dt&0x0F)) return false;
    dt|=0x80; // get next item
    }
  return true;
}

bool cSmartCardNagra::ParseDataType(unsigned char dt)
{
  switch(dt) {
   case IRDINFO:
     {
     irdProvId=(buff[10]*256)|buff[11];
     char id[12];
     memcpy(irdId,buff+17,4);
     LDUMP(L_SC_INIT,irdId,4,"IRD system-ID: %04X, IRD ID:",irdProvId);
     HexStr(id,irdId,sizeof(irdId));
     infoStr.Printf("IRD system-ID: %04X\nIRD ID: %s\n",irdProvId,id);
     return true;
     }
   case TIERS:
     if(buff[7]==0x88 || buff[7]==0x08  || buff[7]==0x0C) {
       int id=(buff[10]*256)|buff[11];
       int tLowId=(buff[20]*256)|buff[21];
       int tHiId=(buff[31]*256)|buff[32];
       char date1[12], time1[12], date2[12], time2[12];
       Date(buff+23,date1,time1);
       Date(buff+27,date2,time2);

       infoStr.Printf("|%04X|%04X    |%04X     |%s|\n",id,tLowId,tHiId,date1);
       infoStr.Printf("|    |        |         |%s|\n",date2);
       PRINTF(L_SC_INIT,"|%04X|%04X|%04X|%s|%s|",id,tLowId,tHiId,date1,time1);
       PRINTF(L_SC_INIT,"|    |    |    |%s|%s|",date2,time2);
       }
   default:
     return true;
   }
  return false;
}

bool cSmartCardNagra::DoCamExchange(const unsigned char *key)
{
  if(!GetCardStatus()) return false;
  if(!DoBlkCmd(0x2A,0x02,0xAA,0x42) || !Status()) return false;
  if(buff[4]!=64) {
    PRINTF(L_SC_ERROR,"reading CAM 2A data failed");
    return false;
    }
  unsigned char res[64];
  if(!DecryptCamData(res,key,buff+5)) return false;
  if(!DoBlkCmd(0x2B,0x42,0xAB,0x02,res) || !Status()) return false;
  return true;
}

void cSmartCardNagra::Date(const unsigned char *data, char *dt, char *ti)
{
  int date=(data[0]<<8)|data[1];
  int time=(data[2]<<8)|data[3];
  struct tm t;
  memset(&t,0,sizeof(struct tm));
  t.tm_min =0;//-300;
  t.tm_year=92;
  t.tm_mday=date + 1;
  t.tm_sec =(time - 1) * 2;
  mktime(&t);
  snprintf(dt,11,"%.2d.%.2d.%.4d",t.tm_mon+1,t.tm_mday,t.tm_year+1900);
  snprintf(ti,9,"%.2d:%.2d:%.2d",t.tm_hour,t.tm_min,t.tm_sec);
}

bool cSmartCardNagra::Decode(const cEcmInfo *ecm, const unsigned char *data, unsigned char *cw)
{
  if(DoBlkCmd(data[3],data[4]+2,0x87,0x02,data+3+2) && Status()) {
    cCondWait::SleepMs(100);
    if(GetCardStatus() && HAS_CW &&
       DoBlkCmd(0x1C,0x02,0x9C,0x36) && Status() &&
       DecryptCW(cw,buff)) return true;
    }
  return false;
}

bool cSmartCardNagra::Update(int pid, int caid, const unsigned char *data)
{
  if(DoBlkCmd(data[8],data[9]+2,0x84,0x02,data+8+2) && Status()) {
    cCondWait::SleepMs(500);
    if(GetCardStatus()) return true;
    }
  return false;
}

bool cSmartCardNagra::DoBlkCmd(unsigned char cmd, int ilen, unsigned char res, int rlen, const unsigned char *data)
{
  unsigned char msg[MAX_LEN+3+1];

  NAD(msg)=N2_NAD; PCB(msg)=0; PCB(msg)^=block; block^=0x40; LEN(msg)=ilen+6;
  CLA(msg)=N2_CLA; INS(msg)=N2_INS; P1(msg)=N2_P1; P2(msg)=N2_P2; L(msg)=ilen;

  int dlen=ilen-2;
  if(dlen<0) {
    PRINTF(L_SC_ERROR,"invalid data length encountered");
    return false;
    }

  N2_CMD(msg)=cmd; N2_CLEN(msg)=dlen;

  if(data && dlen>0) memcpy(&N2_DATA(msg),data,dlen);
  int msglen=LEN(msg)+3;
  msg[msglen-1]=rlen;
  msg[msglen]=XorSum(msg,msglen);
  msglen++;

  if(SerWrite(msg,msglen)==msglen) {
    LDUMP(L_CORE_SC,msg,msglen,"NAGRA: <-");
    cCondWait::SleepMs(10);
    if(SerRead(buff,3,cardCfg.workTO)!=3) {
      PRINTF(L_SC_ERROR,"reading back reply failed");
      return false;
      }
    int reslen=buff[2]+1;
    if(SerRead(buff+3,reslen,cardCfg.workTO)!=reslen) {
      PRINTF(L_SC_ERROR,"reading back information block failed");
      return false;
      }
    if(XorSum(buff,reslen+3)) {
      PRINTF(L_SC_ERROR,"checksum failed");
      return false;
      }
    LDUMP(L_CORE_SC,buff,3+reslen,"NAGRA: ->");

    if(buff[3]!=res) {
      PRINTF(L_SC_ERROR,"result not expected (%02X != %02X)",buff[3],res);
      return false;
      }
    memcpy(sb,&buff[3+rlen],2);
    LDUMP(L_CORE_SC,sb,2,"NAGRA: ->");
    cCondWait::SleepMs(10);
    return true;
    }
  return false;
}

// -- cSmartCardLinkNagra -------------------------------------------------------------

class cSmartCardLinkNagra : public cSmartCardLink {
public:
  cSmartCardLinkNagra(void):cSmartCardLink(SC_NAME,SC_ID) {}
  virtual cSmartCard *Create(void) { return new cSmartCardNagra(); }
  virtual cSmartCardData *CreateData(void) { return new cSmartCardDataNagra; }
  };

static cSmartCardLinkNagra staticScInit;
