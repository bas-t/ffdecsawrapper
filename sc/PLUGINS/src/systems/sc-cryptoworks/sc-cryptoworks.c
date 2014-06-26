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
#include <ctype.h>

#include "system-common.h"
#include "smartcard.h"
#include "data.h"
#include "parse.h"
#include "crypto.h"
#include "opts.h"
#include "misc.h"
#include "log-sc.h"
#include "log-core.h"
#include "version.h"

SCAPIVERSTAG();

#define SYSTEM_NAME          "SC-Cryptoworks"
#define SYSTEM_PRI           -5

#define SC_NAME "Cryptoworks"
#define SC_ID   MAKE_SC_ID('C','r','W','o')

#define L_SC        10
#define L_SC_EXTRA  LCLASS(L_SC,L_SC_LASTDEF<<1)
#define L_SC_ALL    LALL(L_SC_EXTRA)

static const struct LogModule lm_sc = {
  (LMOD_ENABLE|L_SC_ALL)&LOPT_MASK,
  (LMOD_ENABLE|L_SC_DEFDEF)&LOPT_MASK,
  "sc-cryptoworks",
  { L_SC_DEFNAMES,"extra" }
  };
ADD_MODULE(L_SC,lm_sc)

static int disableParental=0;

// -- cSystemScCryptoworks ---------------------------------------------------------------

class cSystemScCryptoworks : public cSystemScCore {
public:
  cSystemScCryptoworks(void);
  };

cSystemScCryptoworks::cSystemScCryptoworks(void)
:cSystemScCore(SYSTEM_NAME,SYSTEM_PRI,SC_ID,"SC Cryptoworks")
{
  hasLogger=true;
}

// -- cSystemLinkScCryptoworks --------------------------------------------------------

class cSystemLinkScCryptoworks : public cSystemLink {
public:
  cSystemLinkScCryptoworks(void);
  virtual bool CanHandle(unsigned short SysId);
  virtual cSystem *Create(void) { return new cSystemScCryptoworks; }
  };

static cSystemLinkScCryptoworks staticInit;

cSystemLinkScCryptoworks::cSystemLinkScCryptoworks(void)
:cSystemLink(SYSTEM_NAME,SYSTEM_PRI)
{
  static const char *rat[] = {
    trNOOP("don't touch"),
    trNOOP("disable")
    };

  opts=new cOpts(SYSTEM_NAME,1);
  opts->Add(new cOptSel("DisableParental",trNOOP("SC-Cryptoworks: Parental rating"),&disableParental,sizeof(rat)/sizeof(char *),rat));
  Feature.NeedsSmartCard();
}

bool cSystemLinkScCryptoworks::CanHandle(unsigned short SysId)
{
  bool res=false;
  cSmartCard *card=smartcards.LockCard(SC_ID);
  if(card) {
    res=card->CanHandle(SysId);
    smartcards.ReleaseCard(card);
    }
  return res;
}

// -- cSmartCardDataCryptoworks -----------------------------------------------------

enum eDataType { dtIPK, dtUCPK, dtPIN };

class cSmartCardDataCryptoworks : public cSmartCardData {
private:
  int IdLen(void) const { return type==dtIPK ? 2:5; }
public:
  eDataType type;
  unsigned char id[5], pin[4];
  cBN key;
  //
  cSmartCardDataCryptoworks(void);
  cSmartCardDataCryptoworks(eDataType Type, unsigned char *Id);
  virtual bool Parse(const char *line);
  virtual bool Matches(cSmartCardData *param);
  };

cSmartCardDataCryptoworks::cSmartCardDataCryptoworks(void)
:cSmartCardData(SC_ID)
{}

cSmartCardDataCryptoworks::cSmartCardDataCryptoworks(eDataType Type, unsigned char *Id)
:cSmartCardData(SC_ID)
{
  type=Type;
  memset(id,0,sizeof(id));
  memcpy(id,Id,IdLen());
}

bool cSmartCardDataCryptoworks::Matches(cSmartCardData *param)
{
  cSmartCardDataCryptoworks *cd=(cSmartCardDataCryptoworks *)param;
  return cd->type==type && !memcmp(cd->id,id,IdLen());
}

bool cSmartCardDataCryptoworks::Parse(const char *line)
{
  line=skipspace(line);
  if(!strncasecmp(line,"IPK",3))       { type=dtIPK;  line+=3; }
  else if(!strncasecmp(line,"UCPK",4)) { type=dtUCPK; line+=4; }
  else if(!strncasecmp(line,"PIN",3))  { type=dtPIN;  line+=3; }
  else {
    PRINTF(L_CORE_LOAD,"smartcarddatacryptoworks: format error: datatype");
    return false;
    }
  line=skipspace(line);
  if(GetHex(line,id,IdLen())!=IdLen()) {
    PRINTF(L_CORE_LOAD,"smartcarddatacryptoworks: format error: caid/serial");
    return false;
    }
  line=skipspace(line);
  if(type==dtPIN) {
    for(int i=0; i<4; i++) {
      if(!isdigit(*line)) {
        PRINTF(L_CORE_LOAD,"smartcarddatacryptoworks: format error: pin");
        return false;
        }
      pin[i]=*line++;
      }
    }
  else {
    unsigned char buff[64];
    if(GetHex(line,buff,64,true)!=64) {
      PRINTF(L_CORE_LOAD,"smartcarddatacryptoworks: format error: ipk/ucpk");
      return false;
      }
    BN_bin2bn(buff,64,key);
    }
  return true;
}

// -- cSmartCardCryptoworks -----------------------------------------------------------

class cSmartCardCryptoworks : public cSmartCard, public cIdSet {
private:
  int caid;
  cRSA rsa;
  cBN ucpk, exp;
  bool ucpkValid;
  //
  int GetLen(void);
  bool EndOfData(void);
  bool SelectFile(int file);
  int ReadRecord(unsigned char *buff, int num);
  int ReadData(unsigned char *buff, int len);
public:
  cSmartCardCryptoworks(void);
  virtual bool Init(void);
  virtual bool Decode(const cEcmInfo *ecm, const unsigned char *data, unsigned char *cw);
  virtual bool Update(int pid, int caid, const unsigned char *data);
  virtual bool CanHandle(unsigned short CaId);
  };

static const struct StatusMsg msgs[] = {
  { { 0x90,0x00 }, "Instruction executed without errors", true },
  { { 0x92,0x40 }, "Memory problem", false },
  { { 0x94,0x02 }, "Out of range", false },
  { { 0x94,0x04 }, "File not found", false },
  { { 0x98,0x04 }, "Verification failed (wrong PIN)", false },
  { { 0x98,0x05 }, "Wrong signature", false },
  { { 0x98,0x40 }, "Verification failed, card blocked (deblocking by uplink required)", false },
  { { 0x9F,0xFF }, "Instruction accepted, data to be read", true },
  { { 0xFF,0xFF }, 0, false }
  };

static const struct CardConfig cardCfg = {
  SM_8E2,1000,100
  };

struct chid_dat {
  unsigned int chid, version;
  unsigned char id, status;
  char from[16], to[16], name[16];
  };
  
cSmartCardCryptoworks::cSmartCardCryptoworks(void)
:cSmartCard(&cardCfg,msgs)
{
  static const unsigned char cwexp[] = { 0x01,0x00,0x01 };
  BN_bin2bn(cwexp,sizeof(cwexp),exp);
  caid=0; ucpkValid=false;
}

int cSmartCardCryptoworks::GetLen(void)
{
  return (sb[0]==0x9F) ? sb[1] : -1;
}

bool cSmartCardCryptoworks::EndOfData(void)
{
  return sb[0]==0x94 && sb[1]==0x02;
}

bool cSmartCardCryptoworks::SelectFile(int file)
{
  static unsigned char insa4[] = { 0xA4,0xA4,0x00,0x00,0x02,0x00,0x00 };
  insa4[5]=file>>8;
  insa4[6]=file&0xFF;
  return IsoWrite(insa4,&insa4[5]) && Status();
}

int cSmartCardCryptoworks::ReadRecord(unsigned char *buff, int num)
{
  static unsigned char insa2[] = { 0xA4,0xA2,0x00,0x00,0x01,0x00 };
  static unsigned char insb2[] = { 0xA4,0xB2,0x00,0x00,0x00 };
  insa2[5]=num;
  if(IsoWrite(insa2,&insa2[5]) && Status() && (num=GetLen())>0) {
    insb2[4]=num;
    if(IsoRead(insb2,buff) && Status()) return num;
    }
  return -1;
}

int cSmartCardCryptoworks::ReadData(unsigned char *buff, int len)
{
  static unsigned char insc0[] = { 0xA4,0xC0,0x00,0x00,0x00 };
  insc0[4]=len;
  if(IsoRead(insc0,buff) && Status()) return len;
  return -1;
}

bool cSmartCardCryptoworks::Init(void)
{
  if(atr->histLen<6 || atr->hist[1]!=0xC4 || atr->hist[4]!=0x8F || atr->hist[5]!=0xF1) {
    PRINTF(L_SC_INIT,"doesn't look like a Cryptoworks card");
    return false;
    }
  infoStr.Begin();
  infoStr.Strcat("Cryptoworks smartcard\n");
  ucpkValid=false;
  unsigned char buff[MAX_LEN];
  int mfid=0x3F20;
  if(ReadData(buff,0x11)>0 && buff[0]==0xDF && buff[1]>=6)
    mfid=buff[6]*256+buff[7];
  else PRINTF(L_SC_ERROR,"reading MF-ID failed, using default 3F20");

  unsigned char Caid[2], serial[5];
  if(!SelectFile(0x2F01) || ReadRecord(buff,0xD1)<4) {
    PRINTF(L_SC_ERROR,"reading record 2f01/d1 failed");
    return false;
    }
  memcpy(Caid,&buff[2],2);
  int c=buff[2]*256+buff[3];
  if(c!=caid) CaidsChanged();
  caid=c;
  if(ReadRecord(buff,0x80)<7) {
    PRINTF(L_SC_ERROR,"reading record 2f01/80 failed");
    return false;
    }
  SetCard(new cCardCryptoworks(&buff[2]));
  memcpy(serial,&buff[2],5);
  snprintf(idStr,sizeof(idStr),"%s (V.%d)",SC_NAME,atr->hist[2]);
  char str[20];
  infoStr.Printf("Card v.%d (PINcount=%d)\n"
                 "Caid %04x Serial %s\n",
                 atr->hist[2],atr->hist[3],caid,HexStr(str,&buff[2],5));
  PRINTF(L_SC_INIT,"card v.%d (pindown=%d) caid %04x serial %s MF %04X",atr->hist[2],atr->hist[3],caid,HexStr(str,&buff[2],5),mfid);
  if(ReadRecord(buff,0x9F)>=3) {
    const char *n="(unknown)";
    if(ReadRecord(buff+10,0xC0)>=18) n=(char *)buff+10+2;
    infoStr.Printf("Issuer:     0x%02x (%.16s)\n",buff[2],n);
    PRINTF(L_SC_INIT,"card issuer: 0x%02x %.16s",buff[2],n);
    }
  if(ReadRecord(buff,0x9E)>=66) {
    HEXDUMP(L_SC_EXTRA,&buff[2],64,"card ISK");
    cSmartCardDataCryptoworks cd(dtIPK,Caid);
    cSmartCardDataCryptoworks *entry=(cSmartCardDataCryptoworks *)smartcards.FindCardData(&cd);
    if(entry) {
      PRINTF(L_SC_EXTRA,"got IPK from smartcard.conf");
      if(rsa.RSA(&buff[2],&buff[2],64,exp,entry->key,false)>0) {
        HEXDUMP(L_SC_EXTRA,&buff[2],64,"decrypted ISK");
        if(buff[2] == ((mfid&0xFF)>>1)) {
          buff[2]|=0x80;
          BN_bin2bn(&buff[2],64,ucpk);
          ucpkValid=true;
          PRINTF(L_SC_INIT,"got UCPK from IPK/ISK");
          }
        else PRINTF(L_SC_ERROR,"UCPK check failed %02x != %02x",buff[2],(mfid&0xFF)>>1);
        }
      else PRINTF(L_SC_ERROR,"RSA failed for UCPK");
      }
    }
  if(!ucpkValid) {
    cSmartCardDataCryptoworks cd(dtUCPK,serial);
    cSmartCardDataCryptoworks *entry=(cSmartCardDataCryptoworks *)smartcards.FindCardData(&cd);
    if(entry) {
      BN_copy(ucpk,entry->key);
      ucpkValid=true;
      PRINTF(L_SC_INIT,"got UCPK from smartcard.conf");
      }
    }
  if(!ucpkValid) PRINTF(L_GEN_WARN,"no valid UCPK for cryptoworks smartcard");

  // read entitlements
  static unsigned char insb8[] = { 0xA4,0xB8,0x00,0x00,0x0C };
  unsigned char provId[16];
  unsigned int count=0;
  insb8[2]=insb8[3]=0x00;
  while(IsoRead(insb8,buff) && !EndOfData() && Status()) {
    if(buff[0]==0xDF && buff[1]==0x0A) {
      int fileno=(buff[4]&0x3F)*256+buff[5];
      if((fileno&0xFF00)==0x1F00) {
        provId[count++]=fileno&0xFF;
        if(count>=sizeof(provId)) break;
        }
      }
    insb8[2]=insb8[3]=0xFF;
    }
  for(unsigned int i=0; i<count ; i++) {
    if(SelectFile(0x1F00+provId[i])) {
      const char *n="(unknown)";
      if(SelectFile(0x0E11) && ReadRecord(buff,0xD6)>=18) n=(char *)buff+2;
      infoStr.Printf("Provider %d: 0x%02x (%.16s)\n",i,provId[i],n);
      PRINTF(L_SC_INIT,"provider %d: 0x%02x %.16s",i,provId[i],n);
      static unsigned char insa2_0[] = { 0xA4,0xA2,0x01,0x00,0x03,0x83,0x01,0x00 };
      static unsigned char insa2_1[] = { 0xA4,0xA2,0x01,0x00,0x05,0x8C,0x00,0x00,0x00,0x00 };
      static unsigned char insb2[] = { 0xA4,0xB2,0x00,0x00,0x00 };
      static const unsigned int fn[] = { 0x0f00,0x0f20,0x0f40,0x0f60,0 };
      static const char *fnName[] = { "Download","Subscriptions","PPV Events","PPV Events",0 };
      bool first=true;
      for(int j=0; fn[j]; j++) {
        if(SelectFile(fn[j])) {
          insa2_0[7]=provId[i];
          unsigned char *ins=(j==1) ? insa2_1 : insa2_0;
          int l;
          if(IsoWrite(ins,&ins[5]) && Status() && (l=GetLen())>0) {
            if(first) {
              infoStr.Printf("id|chid|st|date             |name\n");
              PRINTF(L_SC_INIT,"| id | chid | stat | date              | version  | name        ");
              first=false;
              }
            infoStr.Printf(  "--+----+--+-----------------+------\n"
                           "%s\n",
                           fnName[j]);
            PRINTF(L_SC_INIT,"+----+------+------+-------------------+----------+-------------");
            PRINTF(L_SC_INIT,"| %s",fnName[j]);
            insb2[3]=0x00;
            insb2[4]=l;
            while(IsoRead(insb2,buff) && !EndOfData() && Status()) {
              struct chid_dat chid;
              memset(&chid,0,sizeof(chid));
              for(int k=0; k<l; k+=buff[k+1]+2) {
                switch(buff[k]) {
                  case 0x83:
                    chid.id=buff[k+2];
                    break;
                  case 0x8c:
                    chid.status=buff[k+2];
                    chid.chid=buff[k+3]*256+buff[k+4];
                    break;
                  case 0x8D:
                    snprintf(chid.from,sizeof(chid.from),"%02d.%02d.%02d",buff[k+3]&0x1F,((buff[k+2]&1)<<3)+(buff[k+3]>>5),(1990+(buff[k+2]>>1))%100);
                    snprintf(chid.to  ,sizeof(chid.to)  ,"%02d.%02d.%02d",buff[k+5]&0x1F,((buff[k+4]&1)<<3)+(buff[k+5]>>5),(1990+(buff[k+4]>>1))%100);
                    break;
                  case 0xD5:
                    snprintf(chid.name,sizeof(chid.name),"%.12s",&buff[k+2]);
                    chid.version=Bin2Int(&buff[k+14],4);
                    break;
                  }
                }
              infoStr.Printf("%02x|%04x|%02x|%s-%s|%.12s\n",
                  chid.id,chid.chid,chid.status,chid.from,chid.to,chid.name);
              PRINTF(L_SC_INIT,"| %02x | %04x |  %02x  | %s-%s | %08x | %.12s",
                  chid.id,chid.chid,chid.status,chid.from,chid.to,chid.version,chid.name);
              insb2[3]=0x01;
              }
            }
          }
        }
      }
    }

  if(disableParental) {
    bool pinOK=false;
    if(SelectFile(0x2F11) && ReadRecord(buff,atr->hist[3])>=7) {
      pinOK=true;
      PRINTF(L_SC_INIT,"read PIN from card.");
      }
    if(!pinOK) {
      cSmartCardDataCryptoworks cd(dtPIN,serial);
      cSmartCardDataCryptoworks *entry=(cSmartCardDataCryptoworks *)smartcards.FindCardData(&cd);
      if(entry) {
        memcpy(&buff[2],entry->pin,4);
        pinOK=true;
        PRINTF(L_SC_INIT,"got PIN from smartcard.conf.");
        }
      }
    if(pinOK) {
      //static const unsigned char ins[] = { 0xA4,0x24,0x00,0x01,0x05 };
      static const unsigned char ins[] = { 0xA4,0x20,0x00,0x00,0x04 }; // verify PIN
      //static const unsigned char ins[] = { 0xA4,0x26,0x00,0x00,0x04 }; // disable PIN
      PRINTF(L_SC_INIT,"your card PIN is %.4s",&buff[2]);
      // parental rating
      // 0x00      - undefined
      // 0x01-0x0f - minimum age=rating+3 years
      // 0x10-0xff - reserved for provider usage
      buff[6]=0;
      if(IsoWrite(ins,&buff[2]) && Status())
        PRINTF(L_SC_INIT,"parental rating set to %02x",buff[6]);
      else
        PRINTF(L_SC_ERROR,"failed to set parental rating.");
      }
    else PRINTF(L_SC_INIT,"no PIN available.");
    }

  infoStr.Finish();
  return true;
}

bool cSmartCardCryptoworks::CanHandle(unsigned short CaId)
{
  return CaId==caid;
}

bool cSmartCardCryptoworks::Decode(const cEcmInfo *ecm, const unsigned char *data, unsigned char *cw)
{
  static unsigned char ins4c[] = { 0xA4,0x4C,0x00,0x00,0x00 };

  unsigned char nanoD4[10];
  int l=CheckSctLen(data,-5+(ucpkValid ? sizeof(nanoD4):0));
  if(l>5) {
    unsigned char buff[MAX_LEN];
    if(ucpkValid) {
      memcpy(buff,data,l);
      nanoD4[0]=0xD4;
      nanoD4[1]=0x08;
      for(unsigned int i=2; i<sizeof(nanoD4); i++) nanoD4[i]=rand();
      memcpy(&buff[l],nanoD4,sizeof(nanoD4));
      data=buff; l+=sizeof(nanoD4);
      }
    ins4c[3]=ucpkValid ? 2 : 0;
    ins4c[4]=l-5;
    if(IsoWrite(ins4c,&data[5]) && Status() &&
       (l=GetLen())>0 && ReadData(buff,l)==l) {
      int r=0;
      for(int i=0; i<l && r<2; ) {
        int n=buff[i+1];
        switch(buff[i]) {
          case 0xDB: // CW
            PRINTF(L_SC_EXTRA,"nano DB (cw)");
            if(n==0x10) {
              memcpy(cw,&buff[i+2],16);
              r|=1;
              }
            break;
          case 0xDF: // signature
            PRINTF(L_SC_EXTRA,"nano DF %02x (sig)",n);
            if(n==0x08) {
              if((buff[i+2]&0x50)==0x50) {
                if(buff[i+3]&0x01) PRINTF(L_SC_ERROR,"not decyphered. PIN protected?");
                else if(!(buff[i+5]&0x80)) PRINTF(L_SC_ERROR,"missing entitlement. Check your subscription");
                else r|=2;
                }
              }
            else if(n==0x40) { // camcrypt
              if(ucpkValid) {
                if(rsa.RSA(&buff[i+2],&buff[i+2],n,exp,ucpk,false)<=0) {
                  PRINTF(L_SC_ERROR,"camcrypt RSA failed.");
                  return false;
                  }
                HEXDUMP(L_SC_EXTRA,&buff[i+2],n,"after camcrypt");
                if(!memmem(&buff[i+2+4],n-4,nanoD4,sizeof(nanoD4))) {
                  PRINTF(L_SC_ERROR,"camcrypt failed. Check IPK/UCPK in your smartcard.conf!");
                  return false;
                  }
                r=0; l=n-4; n=4;
                }
              else {
                PRINTF(L_SC_ERROR,"valid UCPK needed for camcrypt! Check your smartcard.conf for a IPK/UCPK!");
                return false;
                }
              }
            break;
          default:
            PRINTF(L_SC_EXTRA,"nano %02x (unhandled)",buff[i]);
            break;
          }
        i+=n+2;
        }
      return r==3;
      }
    }
  return false;
}

bool cSmartCardCryptoworks::Update(int pid, int caid, const unsigned char *data)
{
  static unsigned char ins[] = { 0xA4,0x42,0x00,0x00,0x00 };

  cAssembleData ad(data);
  if(MatchAndAssemble(&ad,0,0)) {
    while((data=ad.Assembled())) {
      int c, n;
      switch(data[0]) {
        case 0x82: c=0x42; n=10; break;
        case 0x84: c=0x48; n=9; break;
        case 0x88:
        case 0x89: c=0x44; n=5; break;
        default:   continue;
        }

      int len=CheckSctLen(data,-n);
      if(len>n) {
        ins[1]=c;
        ins[4]=len-n;
        if(IsoWrite(ins,&data[n])) Status();
        }
      }
    return true;
    }
  return false;
}

// -- cSmartCardLinkCryptoworks -------------------------------------------------------

class cSmartCardLinkCryptoworks : public cSmartCardLink {
public:
  cSmartCardLinkCryptoworks(void):cSmartCardLink(SC_NAME,SC_ID) {}
  virtual cSmartCard *Create(void) { return new cSmartCardCryptoworks(); }
  virtual cSmartCardData *CreateData(void) { return new cSmartCardDataCryptoworks; }
  };

static cSmartCardLinkCryptoworks staticScInit;
