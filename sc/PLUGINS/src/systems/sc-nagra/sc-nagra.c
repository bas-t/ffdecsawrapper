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

#include <openssl/rand.h>

#include "system-common.h"
#include "smartcard.h"
#include "parse.h"
#include "crypto.h"
#include "helper.h"
#include "opts.h"
#include "log-sc.h"
#include "log-core.h"
#include "version.h"

SCAPIVERSTAG();

#define SYSTEM_NAGRA         0x1800

#define SYSTEM_NAME          "SC-Nagra"
#define SYSTEM_PRI           -5

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

static int allowT14=0;

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
  opts=new cOpts(SYSTEM_NAME,1);
  opts->Add(new cOptBool("AllowT14",trNOOP("SC-Nagra: use T14 Nagra mode"),&allowT14));
  Feature.NeedsSmartCard();
}

bool cSystemLinkScNagra::CanHandle(unsigned short SysId)
{
  bool res=false;
  cSmartCard *card=smartcards.LockCard(SC_ID);
  if(card) {
    res=card->CanHandle(SysId);
    smartcards.ReleaseCard(card);
    }
  return res;
}

// -- cCamCryptNagra ------------------------------------------------------------------

class cCamCryptNagra {
private:
  cIDEA idea;
  cRSA rsa;
  cBN camMod, camExp;
  IdeaKS sessKey;
  unsigned int cardid;
  unsigned char boxkey[8], signature[8];
  bool hasMod;
  //
  void InitKey(unsigned char *key, const unsigned char *bk, unsigned int id);
  void Signature(unsigned char *sig, const unsigned char *key, const unsigned char *msg, int len);
public:
  cCamCryptNagra(void);
  void SetCardData(unsigned int id, const unsigned char *bk, BIGNUM *exp);
  void SetCamMod(BIGNUM *m);
  bool HasCamMod(void) { return hasMod; }
  bool DecryptDT08(const unsigned char *dt08, unsigned int irdid, cBN *irdmod, bool fakeid);
  bool MakeSessionKey(unsigned char *out, const unsigned char *in);
  void DecryptCW(unsigned char *cw, const unsigned char *ecw1, const unsigned char *ecw2);
  };

cCamCryptNagra::cCamCryptNagra(void)
{
  hasMod=false;
}

void cCamCryptNagra::SetCardData(unsigned int id, const unsigned char *bk, BIGNUM *exp)
{
  cardid=id;
  memcpy(boxkey,bk,sizeof(boxkey));
  BN_copy(camExp,exp);
  if(LOG(L_SC_PROC)) {
    unsigned char bb[4];
    camExp.Put(bb,2);
    LDUMP(L_SC_PROC,boxkey,sizeof(boxkey),"Active card data: CARDID: %08x EXP: %02x%02x BOXKEY:",cardid,bb[0],bb[1]);
    }
}

void cCamCryptNagra::SetCamMod(BIGNUM *m)
{
  BN_copy(camMod,m);
  memcpy(signature,boxkey,sizeof(signature));
  hasMod=true;
}

void cCamCryptNagra::InitKey(unsigned char *key, const unsigned char *bk, unsigned int id)
{
  memcpy(key,bk,8);
  BYTE4_BE(key+8 , id);
  BYTE4_BE(key+12,~id);
}

void cCamCryptNagra::Signature(unsigned char *sig, const unsigned char *key, const unsigned char *msg, int len)
{
  unsigned char buff[16];
  memcpy(buff,key,16);
  for(int i=0; i<len; i+=8) {
    idea.Decrypt(msg+i,8,buff,buff,0);
    xxor(buff,8,buff,&msg[i]);
    memcpy(&buff[8],buff,8);
    }
  memcpy(sig,buff,8);
}

bool cCamCryptNagra::DecryptDT08(const unsigned char *dt08, unsigned int irdid, cBN *irdmod, bool fakeid)
{
  if(LOG(L_SC_PROC)) {
    unsigned char bb[64];
    irdmod->Put(bb,sizeof(bb));
    LDUMP(L_SC_PROC,bb,sizeof(bb),"DT08 decrypt IRDID: %08x IRDMOD:",irdid);
    }
  unsigned char buff[72];
  if(rsa.RSA(buff,dt08+1,64,camExp,*irdmod)!=64) return false;
  memcpy(buff+64,dt08+1+64,8);
  buff[63]|=dt08[0]&0x80;
  LDUMP(L_SC_PROC,buff,72,"DT08 after RSA");

  unsigned char key[16];
  InitKey(key,boxkey,irdid);
  LDUMP(L_SC_PROC,key,16,"DT08 IDEA key");
  IdeaKS dks;
  idea.SetDecKey(key,&dks);
  idea.Decrypt(buff,72,&dks,0);
  LDUMP(L_SC_PROC,buff,72,"DT08 after IDEA");

  memcpy(signature,buff,8);
  LDUMP(L_SC_PROC,signature,8,"signature");
  BYTE4_BE(buff  ,0);
  BYTE4_BE(buff+4,fakeid?0xFFFFFFFF:cardid);
  Signature(buff,key,buff,72);
  LDUMP(L_SC_PROC,buff,8,"check sig");
  if(memcmp(signature,buff,8)) {
    PRINTF(L_SC_PROC,"DT08 signature failed");
    return false;
    }
  BN_bin2bn(buff+8,64,camMod);
  hasMod=true;
  return true;
}

bool cCamCryptNagra::MakeSessionKey(unsigned char *out, const unsigned char *camdata)
{
  if(!hasMod) return false;
  if(LOG(L_SC_PROC)) {
    unsigned char bb[64];
    camMod.Put(bb,sizeof(bb));
    LDUMP(L_SC_PROC,bb,sizeof(bb),"Sessionkey negotiation CAMMOD:");
    }
  //decrypt $2A data here and prepare $2B reply
  if(rsa.RSA(out,camdata,64,camExp,camMod)!=64) return false;
  LDUMP(L_SC_PROC,out,64,"CMD 2A/26 after RSA:");

  unsigned char key[16], sess[16];
  InitKey(key,signature,cardid);
  LDUMP(L_SC_PROC,key,16,"first IDEA key: ");
  Signature(sess,key,out,32);
  memcpy(key,sess,8);
  memcpy(key+8,sess,8);
  LDUMP(L_SC_PROC,key,16,"second IDEA key:");
  Signature(sess+8,key,out,32);
  LDUMP(L_SC_PROC,sess,16,"SESSION KEY:    ");
  idea.SetDecKey(sess,&sessKey);

  if(rsa.RSA(out,out,64,camExp,camMod)!=64) return false;
  LDUMP(L_SC_PROC,out,64,"CMD 2B/27 data:");
  return true;
}

void cCamCryptNagra::DecryptCW(unsigned char *cw, const unsigned char *ecw1, const unsigned char *ecw2)
{
  memcpy(cw+0,ecw1,8);
  idea.Decrypt(cw+0,8,&sessKey,0);
  memcpy(cw+8,ecw2,8);
  idea.Decrypt(cw+8,8,&sessKey,0);
}

// -- cSmartCardDataNagra ------------------------------------------------------

struct SecondaryKey {
  unsigned char irdid[4];
  unsigned char free[8];
  unsigned char exp[2];
  unsigned char y1[8];
  unsigned char mod[64];
  unsigned char y2[8];
  unsigned char cs[2];
  } __attribute__((packed));

class cSmartCardDataNagra : public cSmartCardData {
private:
  unsigned short CRC16(const unsigned char *mem, int len, bool reverse);
public:
  unsigned int cardid;
  unsigned char bk[8];
  cBN exp, mod;
  bool isIrdMod;
  //
  cSmartCardDataNagra(void);
  cSmartCardDataNagra(unsigned int id, bool irdmod);
  virtual bool Parse(const char *line);
  virtual bool Matches(cSmartCardData *param);
  };

cSmartCardDataNagra::cSmartCardDataNagra(void)
:cSmartCardData(SC_ID) {}

cSmartCardDataNagra::cSmartCardDataNagra(unsigned int id, bool irdmod)
:cSmartCardData(SC_ID)
{
  cardid=id;
  isIrdMod=irdmod;
}

bool cSmartCardDataNagra::Matches(cSmartCardData *param)
{
  cSmartCardDataNagra *cd=(cSmartCardDataNagra *)param;
  return cd->cardid==cardid && cd->isIrdMod==isIrdMod;
}

bool cSmartCardDataNagra::Parse(const char *line)
{
  bool isSK=false;
  int dlen=64;
  BN_set_word(exp,3);
  line=skipspace(line);
  unsigned char id[4];
  if(GetHex(line,id,sizeof(id))!=sizeof(id)) {
    PRINTF(L_CORE_LOAD,"smartcarddatanagra: format error: card id");
    return false;
    }
  cardid=UINT32_BE(id);
  line=skipspace(line);
  if(!strncasecmp(line,"SK",2)) { isSK=true; dlen=96; line+=2; }
  else {
    if(GetHex(line,bk,sizeof(bk))!=sizeof(bk)) {
      PRINTF(L_CORE_LOAD,"smartcarddatanagra: format error: boxkey");
      return false;
      }
    line=skipspace(line);
    if(!strncasecmp(line,"IRDMOD",6)) { isIrdMod=true; line+=6; }
    else if(!strncasecmp(line,"CARDMOD",7)) { isIrdMod=false; line+=7; }
    else {
      PRINTF(L_CORE_LOAD,"smartcarddatanagra: format error: IRDMOD/CARDMOD");
      return false;
      }
    }
  line=skipspace(line);

  unsigned char *buff=AUTOMEM(dlen);
  if(GetHex(line,buff,dlen)!=dlen) {
    PRINTF(L_CORE_LOAD,"smartcarddatanagra: format error: data block");
    return false;
    }
  if(!isSK) {
    mod.Get(buff,64);
    }
  else {
    struct SecondaryKey *sk=(struct SecondaryKey *)buff;
    if(UINT16_BE(sk->cs)!=CRC16(buff,sizeof(*sk)-sizeof(sk->cs),false)) {
      PRINTF(L_CORE_LOAD,"smartcarddatanagra: secondary key CRC failed");
      return false;
      }
    unsigned short e=UINT16_BE(sk->exp);
    if(e!=0x0003 && e!=CRC16(buff,12,false)) BN_set_word(exp,e);
    xxor(bk,sizeof(bk),sk->y1,sk->y2);
    mod.Get(sk->mod,sizeof(sk->mod));
    }
  return true;
}

#define CRC_POLY 0x1021 // CRC-16(XMODEM) Polynomial: x16 + x12 + x5 + 1
#define CRC_INIT 0x0000

unsigned short cSmartCardDataNagra::CRC16(const unsigned char *mem, int len, bool reverse)
{
  int index=0, dir=1;
  if(reverse) { index=len-1; dir=-1; }
  unsigned short crc=CRC_INIT;
  for(int i=0; i<len; i++) {
    crc^=(unsigned short)(mem[index]<<8);
    index+=dir;
    for(int j=0; j<8; j++) {
      if((crc&0x8000)) crc=(unsigned short)(((crc<<1)&0xFFFF)^CRC_POLY);
      else             crc=(unsigned short)( (crc<<1)&0xFFFF);
      }
    }
  return(crc);
}

// -- cSmartCardNagra -----------------------------------------------------------------

#define SETIFS      0xC1

// Datatypes
#define IRDINFO     0x00
#define DT01        0x01
#define DT04        0x04
#define TIERS       0x05
#define DT06        0x06
#define CAMDATA     0x08

#define MAX_REC     20

// Card Status checks
#define HAS_CW()           ((state[2]&6)==6)
#define RENEW_SESSIONKEY() (state[0]&224)
#define SENDDATETIME()     (state[0]&16)

class cSmartCardNagra : public cSmartCard, public cIdSet, private cCamCryptNagra {
private:
  unsigned char buff[MAX_LEN+16], state[3], rominfo[15];
  unsigned int cardId, irdId;
  int caid, provId, block;
  bool isTiger, isT14Nagra, isN3, swapCW;
  //
  bool GetCardStatus(void);
  bool GetDataType(unsigned char dt, int len, int shots=MAX_REC);
  bool ParseDataType(unsigned char dt);
  bool DoCamExchange(void);
  bool SendDateTimeCmd(void);
  time_t Date(const unsigned char *data, char *buf, int len);
  bool DoBlkCmd(unsigned char cmd, int ilen, unsigned char res, int rlen, const unsigned char *data=0);
  bool SetIFS(unsigned char len);
  void PostProcess(void);
public:
  cSmartCardNagra(void);
  virtual bool Init(void);
  virtual bool Decode(const cEcmInfo *ecm, const unsigned char *data, unsigned char *cw);
  virtual bool Update(int pid, int caid, const unsigned char *data);
  virtual bool CanHandle(unsigned short CaId);
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
{}

bool cSmartCardNagra::SetIFS(unsigned char size)
{
  unsigned char buf[5];
  // NAD, PCB, LEN
  buf[0]=0x21; buf[1]=0xC1; buf[2]=0x01;
  // Information Field size
  buf[3]=size; buf[4]=XorSum(buf,4);
  PRINTF(L_SC_INIT,"Setting IFS to 0x%02X",size);
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
  memcpy(state,buff+6,3);
  return true;
}

bool cSmartCardNagra::CanHandle(unsigned short CaId)
{
  return CaId==caid;
}

bool cSmartCardNagra::Init(void)
{
  block=0; isTiger=isT14Nagra=isN3=swapCW=false;
  caid=SYSTEM_NAGRA;
  ResetIdSet();

  static const unsigned char atrDNASP[] = { 'D','N','A','S','P' };
  static const unsigned char atrTIGER[] = { 'T','I','G','E','R' };
  static const unsigned char atrNCMED[] = { 'N','C','M','E','D' };
  static const unsigned char atrIRDET[] = { 'I','R','D','E','T','O' };
  if(!memcmp(atr->hist,atrDNASP,sizeof(atrDNASP))) {
    if(atr->hist[5]=='2' && atr->hist[6]=='4') isN3=true;
    PRINTF(L_SC_INIT,"detected native T1 nagra card (N%d Mode)",isN3?3:2);
    if(!SetIFS(0xFE)) return false;
    memcpy(rominfo,atr->hist,sizeof(rominfo));
    }
  else if(!memcmp(atr->hist,atrTIGER,sizeof(atrTIGER)) || !memcmp(atr->hist,atrNCMED,sizeof(atrNCMED))) {
    PRINTF(L_SC_INIT,"detected nagra tiger card");
    if(!SetIFS(0xFE)) return false;
    memcpy(rominfo,atr->hist,sizeof(rominfo));
    cardId=0xFFFFFFFF;
    isTiger=true;
    }
  else if(!memcmp(atr->hist,atrIRDET,sizeof(atrIRDET))) {
    PRINTF(L_SC_INIT,"detected tunneled T14 nagra card");
    if(!allowT14) {
      PRINTF(L_SC_INIT,"Nagra mode for T14 card disabled in setup");
      return false;
      }
    PRINTF(L_SC_INIT,"using nagra mode");
    isT14Nagra=true;
    if(!DoBlkCmd(0x10,0x02,0x90,0x11)) {
      PRINTF(L_SC_ERROR,"get rom version failed");
      return false;
      }
    memcpy(rominfo,&buff[2],15);
    }
  else {
    PRINTF(L_SC_INIT,"doesn't look like a nagra card");
    return false;
    }

  infoStr.Begin();
  infoStr.Strcat("Nagra smartcard\n");
  char rom[12], rev[12];
  snprintf(rom,sizeof(rom),"%c%c%c%c%c%c%c%c",rominfo[0],rominfo[1],rominfo[2],rominfo[3],rominfo[4],rominfo[5],rominfo[6],rominfo[7]);
  snprintf(rev,sizeof(rev),"%c%c%c%c%c%c",rominfo[9],rominfo[10],rominfo[11],rominfo[12],rominfo[13],rominfo[14]);
  PRINTF(L_SC_INIT,"rom version: %s revision: %s",rom,rev);
  infoStr.Printf("Rom %s Rev %s\n",rom,rev);

  if(!isTiger) {
    GetCardStatus();
    if(!DoBlkCmd(0x12,0x02,0x92,0x06) || !Status()) return false;
    cardId=UINT32_BE(buff+5);
    SetCard(new cCardNagra2(buff+5));

    if(!GetDataType(DT01,0x0E)) return false;
    GetCardStatus();
    if(!GetDataType(IRDINFO,0x39)) return false;
    GetCardStatus();
    if(!GetDataType(CAMDATA,0x55)) return false;
    GetCardStatus();
    if(!GetDataType(DT04,0x44)) return false;
    GetCardStatus();
    if(memcmp(rominfo+5,"181",3)!=0) { // not working on ROM181
      infoStr.Printf("Tiers\n");
      infoStr.Printf("|id  |chid| dates               |\n");
      infoStr.Printf("+----+----+---------------------+\n");
      if(!GetDataType(TIERS,0x57)) return false;
      GetCardStatus();
      }
    if(!GetDataType(DT06,0x16)) return false;
    GetCardStatus();
    }

  if(!HasCamMod()) {
    cSmartCardDataNagra cd(cardId,false);
    cSmartCardDataNagra *entry=(cSmartCardDataNagra *)smartcards.FindCardData(&cd);
    if(entry) {
      SetCardData(cardId,entry->bk,entry->exp);
      SetCamMod(entry->mod);
      }
    else {
      PRINTF(L_SC_ERROR,"can't find CARD modulus");
      return false;
      }
    }
  if(!DoCamExchange()) return false;
  infoStr.Finish();
  return true;
}

bool cSmartCardNagra::DoCamExchange(void)
{
  if(!isTiger) {
    // N3 cam session negotiation start
    if(isN3 && (!DoBlkCmd(0x29,0x02,0xA9,0x04) || !Status())) return false;

    unsigned char ext[5];
    BYTE4_BE(ext,irdId); ext[4]=0; // tuner
    if((!isN3 && !DoBlkCmd(0x2A,0x02,0xAA,0x42)) ||
       ( isN3 && !DoBlkCmd(0x26,0x07,0xA6,0x42,ext)) ||
       !Status()) return false;
    unsigned char res[64+5];
    if(!MakeSessionKey(res,buff+5)) return false;
    memcpy(res+64,ext,5);
    cCondWait::SleepMs(500);
    if((!isN3 && !DoBlkCmd(0x2B,0x42,0xAB,0x02,res)) ||
       ( isN3 && !DoBlkCmd(0x27,0x47,0xA7,0x02,res)) ||
       !Status() ||
       !GetCardStatus()) return false;
    if(SENDDATETIME())
       SendDateTimeCmd();
    if(RENEW_SESSIONKEY()) {
      PRINTF(L_SC_ERROR,"Session key negotiation failed!");
      return false;
      }
    }
  else {
    static const unsigned char d1[] = { 0xd2 };
    if(!DoBlkCmd(0xD1,0x03,0x51,0x42,d1) || !Status()) return false;

    //XXX need implement Tiger session key handling (see oscam)
    return false;

    unsigned char res[64];
    if(!DoBlkCmd(0xD2,0x42,0x52,0x03,res) || !Status()) return false;
    }
  return true;
}

bool cSmartCardNagra::SendDateTimeCmd(void)
{
  DoBlkCmd(0xC8,0x02,0xB8,0x06);
  return true;
}

void cSmartCardNagra::PostProcess(void)
{
  if(!isTiger) {
    GetCardStatus();
    cCondWait::SleepMs(10);
    if(RENEW_SESSIONKEY()) DoCamExchange();
    if(SENDDATETIME()) SendDateTimeCmd();
    }
}

bool cSmartCardNagra::GetDataType(unsigned char dt, int len, int shots)
{
  bool isdt8 = (dt==0x08);
  for(int i=0; i<shots; i++) {
    if(!DoBlkCmd(0x22,0x03,0xA2,len,&dt) || !Status()) {
     PRINTF(L_SC_ERROR,"failed to get datatype %02X",dt);
     return false;
     }
    if(buff[5]==0) break;
    if(!ParseDataType(dt&0x0F)) return false;
    if(isdt8 && buff[14]==0x49) break;
    dt|=0x80; // get next item
    }
  return true;
}

bool cSmartCardNagra::ParseDataType(unsigned char dt)
{
  switch(dt) {
    case IRDINFO:
      {
      provId=(buff[10]*256)|buff[11];
      if(provId==0x0401 || provId==0x3411) swapCW=true;
      caid=SYSTEM_NAGRA+buff[14];
      irdId=UINT32_BE(buff+17);
      PRINTF(L_SC_INIT,"CAID: %04x PROV: %04x CARD ID: %08x IRD ID: %08x",caid,provId,cardId,irdId);
      infoStr.Printf("CAID: %04x PROV: %04x\nCARD ID: %08x\nIRD ID: %08x\n",caid,provId,cardId,irdId);
      break;
      }
    case TIERS:
      {
      int chid=(buff[14]*256)|buff[15];
      if(chid) {
        int id=(buff[10]*256)|buff[11];
        char ds[16], de[16];
        Date(buff+23,ds,sizeof(ds));
        Date(buff+16,de,sizeof(de));
        infoStr.Printf("|%04X|%04X|%s-%s|\n",id,chid,ds,de);
        PRINTF(L_SC_INIT,"|%04X|%04X|%s-%s|",id,chid,ds,de);
        }
      break;
      }
    case CAMDATA:
      if(buff[14]==0x49) {
        cSmartCardDataNagra cd(cardId,true);
        cSmartCardDataNagra *entry=(cSmartCardDataNagra *)smartcards.FindCardData(&cd);
        if(entry) {
          SetCardData(cardId,entry->bk,entry->exp);
          return DecryptDT08(buff+15,irdId,&entry->mod,swapCW);
          }
        else {
          PRINTF(L_SC_ERROR,"can't find IRD modulus");
          return false;
          }
        }
      break;
    }
  return true;
}

time_t cSmartCardNagra::Date(const unsigned char *data, char *buf, int len)
{
  int day=((data[0]<<8)|data[1])-0x7f7;
  time_t ut=870393600L+day*(24*3600);
  if(buf) {
    struct tm rt;
    struct tm *t=gmtime_r(&ut,&rt);
    if(t) snprintf(buf,len,"%04d/%02d/%02d",t->tm_year+1900,t->tm_mon+1,t->tm_mday);
    else buf[0]=0;
    }
  return ut;
}

bool cSmartCardNagra::Decode(const cEcmInfo *ecm, const unsigned char *data, unsigned char *cw)
{
  if(!isTiger) {
    unsigned char pkt[256+16];
    memset(pkt,0,sizeof(pkt));
    memcpy(pkt,data+3+2,data[4]);
    for(int i=1; i>=0; i--) {
      if(((!isN3 && DoBlkCmd(data[3],data[4]+2,0x87,0x02,pkt)) ||
          ( isN3 && DoBlkCmd(data[3]+1,data[4]+5+2,0x88,0x04,pkt))) &&
          Status())
        break;
      PRINTF(L_SC_ERROR,"ECM cmd failed%s",(i>0)?", retrying":"");
      if(i==0) return false;
      }
    cCondWait::SleepMs(10);
    for(int retry=0; !GetCardStatus() && retry<5; retry++) cCondWait::SleepMs(5);
    cCondWait::SleepMs(10);
    if(HAS_CW() && DoBlkCmd(0x1C,0x02,0x9C,0x36) && Status()) {
      DecryptCW(cw,buff+33,buff+7);
      PostProcess();
      if(swapCW) {
        unsigned char tt[8];
        memcpy(&tt[0],&cw[0],8);
        memcpy(&cw[0],&cw[8],8);
        memcpy(&cw[8],&tt[0],8);
        }
      return true;
      }
    }
  else {
    //                  ecm_data: 80 30 89 D3 87 54 11 10 DA A6 0F 4B 92 05 34 00 ...
    //serial_data: A0 CA 00 00 8C D3 8A 00 00 00 00 00 10 DA A6 0F .
    unsigned char ecm_trim[150];
    memset(ecm_trim,0,150);
    memcpy(&ecm_trim[5],data+3+2+2,data[4]+2);
    if(DoBlkCmd(data[3],data[4]+5,0x53,0x16,ecm_trim) && Status()) {
      DecryptCW(cw,buff+17,buff+9);
      PostProcess();
      return true;
      }
    }
  return false;
}

bool cSmartCardNagra::Update(int pid, int caid, const unsigned char *data)
{
  if(MatchEMM(data)) {
    if(DoBlkCmd(data[8],data[9]+2,0x84,0x02,data+8+2) && Status()) {
      cCondWait::SleepMs(300);
      PostProcess();
      }
    return true;
    }
  return false;
}

bool cSmartCardNagra::DoBlkCmd(unsigned char cmd, int ilen, unsigned char res, int rlen, const unsigned char *data)
{
  /*
  here we build the command related to the protocol T1 for ROM142 or T14 for ROM181
  the only different that i know is the command length byte msg[4], this msg[4]+=1 by a ROM181 smartcard (_nighti_)
  one example for the cmd$C0
  T14 protocol:       01 A0 CA 00 00 03 C0 00 06 91
  T1  protocol: 21 00 08 A0 CA 00 00 02 C0 00 06 87
  */
  unsigned char msg[MAX_LEN+16];
  static char nagra_head[] = { 0xA0,0xCA,0x00,0x00 };

  memset(msg,0,sizeof(msg));
  int c=0;
  if(!isT14Nagra) {
    msg[c++]=0x21;
    msg[c++]=block; block^=0x40;
    msg[c++]=ilen+6;
    }
  else {
    msg[c++]=0x01;
    }
  memcpy(msg+c,nagra_head,sizeof(nagra_head)); c+=sizeof(nagra_head);
  msg[c]=ilen;
  msg[c+1]=cmd;
  int dlen=ilen-2;
  if(dlen<0) {
    PRINTF(L_SC_ERROR,"invalid data length encountered");
    return false;
    }
  msg[c+2]=dlen;
  if(isT14Nagra) msg[c]++;
  c+=3;
  if(data && dlen>0) { memcpy(msg+c,data,dlen); c+=dlen; }
  msg[c++]=rlen;
  msg[c]=XorSum(msg,c); c++;

  if(SerWrite(msg,c)==c) {
    LDUMP(L_CORE_SC,msg,c,"NAGRA: <-");
    cCondWait::SleepMs(10);
    if(SerRead(buff,3,cardCfg.workTO)!=3) {
      PRINTF(L_SC_ERROR,"reading back reply failed");
      return false;
      }
    int xlen=buff[2]+1;
    if(SerRead(buff+3,xlen,cardCfg.workTO)!=xlen) {
      PRINTF(L_SC_ERROR,"reading back information block failed");
      return false;
      }
    xlen+=3;
    if(XorSum(buff,xlen)) {
      PRINTF(L_SC_ERROR,"checksum failed");
      return false;
      }
    LDUMP(L_CORE_SC,buff,xlen,"NAGRA: ->");

    if(buff[3]!=res) {
      PRINTF(L_SC_ERROR,"result not expected (%02X != %02X)",buff[3],res);
      return false;
      }
    if(buff[2]-2!=rlen) {
      PRINTF(L_SC_ERROR,"result length not expected (%d != %d)",buff[2]-2,rlen);
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
