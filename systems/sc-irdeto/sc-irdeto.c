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
#include <string.h>

#include <openssl/rand.h>

#include "system-common.h"
#include "smartcard.h"
#include "crypto.h"
#include "data.h"
#include "misc.h"
#include "parse.h"
#include "log-sc.h"
#include "log-core.h"
#include "version.h"

SCAPIVERSTAG();

#define SYSTEM_NAME          "SC-Irdeto"
#define SYSTEM_PRI           -5

#define SC_NAME "Irdeto"
#define SC_ID   MAKE_SC_ID('I','r','d','t')

#define L_SC        8
#define L_SC_ALL    LALL(L_SC_LASTDEF)

static const struct LogModule lm_sc = {
  (LMOD_ENABLE|L_SC_ALL)&LOPT_MASK,
  (LMOD_ENABLE|L_SC_DEFDEF)&LOPT_MASK,
  "sc-irdeto",
  { L_SC_DEFNAMES }
  };
ADD_MODULE(L_SC,lm_sc)

static void BN_complement(const unsigned char *data, int len, BIGNUM *bn)
{
  unsigned char *buff=AUTOMEM(len);
  for(int i=len-1; i>=0; i--) buff[i]=~data[i];
  BN_bin2bn(buff,len,bn);
  BN_add_word(bn,1);
}

// -- cSystemScIrdeto ----------------------------------------------------------

class cSystemScIrdeto : public cSystemScCore {
public:
  cSystemScIrdeto(void);
  };

cSystemScIrdeto::cSystemScIrdeto(void)
:cSystemScCore(SYSTEM_NAME,SYSTEM_PRI,SC_ID,"SC Irdeto")
{
  hasLogger=true;
}

// -- cSystemLinkScIrdeto ------------------------------------------------------

class cSystemLinkScIrdeto : public cSystemLink {
public:
  cSystemLinkScIrdeto(void);
  virtual bool CanHandle(unsigned short SysId);
  virtual cSystem *Create(void) { return new cSystemScIrdeto; }
  };

static cSystemLinkScIrdeto staticInit;

cSystemLinkScIrdeto::cSystemLinkScIrdeto(void)
:cSystemLink(SYSTEM_NAME,SYSTEM_PRI)
{
  Feature.NeedsSmartCard();
}

bool cSystemLinkScIrdeto::CanHandle(unsigned short SysId)
{
  bool res=false;
  cSmartCard *card=smartcards.LockCard(SC_ID);
  if(card) {
    res=card->CanHandle(SysId);
    smartcards.ReleaseCard(card);
    }
  return res;
}

// -- cCamCrypt ----------------------------------------------------------------

class cCamCrypt {
private:
  static const unsigned char cryptTable[];
  bool randomInit;
  cRSA rsa;
  cBN cardExp, cardMod;
  //
  void GenerateRandom(unsigned char *buf, int len);
  void RotateRight8Byte(unsigned char *key);
  void RotateLeft8Byte(unsigned char *key);
protected:
  void CamCrypt(const unsigned char *key, unsigned char *data);
  void RevCamCrypt(const unsigned char *key, unsigned char *data);
  //
  bool SetupCardFiles(unsigned char *data, int len, BIGNUM *exp, BIGNUM *mod);
  void PrepareCamMessage(unsigned char *plain);
  bool EncryptCamMessage(unsigned char *encrypted, const unsigned char *plain);
  const unsigned char *CamKey(const unsigned char *data) { return data+8; }
  const unsigned char *HelperKey(const unsigned char *data) { return data+24; }
public:
  cCamCrypt(void);
  };

const unsigned char cCamCrypt::cryptTable[256] = {
  0xDA,0x26,0xE8,0x72,0x11,0x52,0x3E,0x46,0x32,0xFF,0x8C,0x1E,0xA7,0xBE,0x2C,0x29,
  0x5F,0x86,0x7E,0x75,0x0A,0x08,0xA5,0x21,0x61,0xFB,0x7A,0x58,0x60,0xF7,0x81,0x4F,
  0xE4,0xFC,0xDF,0xB1,0xBB,0x6A,0x02,0xB3,0x0B,0x6E,0x5D,0x5C,0xD5,0xCF,0xCA,0x2A,
  0x14,0xB7,0x90,0xF3,0xD9,0x37,0x3A,0x59,0x44,0x69,0xC9,0x78,0x30,0x16,0x39,0x9A,
  0x0D,0x05,0x1F,0x8B,0x5E,0xEE,0x1B,0xC4,0x76,0x43,0xBD,0xEB,0x42,0xEF,0xF9,0xD0,
  0x4D,0xE3,0xF4,0x57,0x56,0xA3,0x0F,0xA6,0x50,0xFD,0xDE,0xD2,0x80,0x4C,0xD3,0xCB,
  0xF8,0x49,0x8F,0x22,0x71,0x84,0x33,0xE0,0x47,0xC2,0x93,0xBC,0x7C,0x3B,0x9C,0x7D,
  0xEC,0xC3,0xF1,0x89,0xCE,0x98,0xA2,0xE1,0xC1,0xF2,0x27,0x12,0x01,0xEA,0xE5,0x9B,
  0x25,0x87,0x96,0x7B,0x34,0x45,0xAD,0xD1,0xB5,0xDB,0x83,0x55,0xB0,0x9E,0x19,0xD7,
  0x17,0xC6,0x35,0xD8,0xF0,0xAE,0xD4,0x2B,0x1D,0xA0,0x99,0x8A,0x15,0x00,0xAF,0x2D,
  0x09,0xA8,0xF5,0x6C,0xA1,0x63,0x67,0x51,0x3C,0xB2,0xC0,0xED,0x94,0x03,0x6F,0xBA,
  0x3F,0x4E,0x62,0x92,0x85,0xDD,0xAB,0xFE,0x10,0x2E,0x68,0x65,0xE7,0x04,0xF6,0x0C,
  0x20,0x1C,0xA9,0x53,0x40,0x77,0x2F,0xA4,0xFA,0x6D,0x73,0x28,0xE2,0xCD,0x79,0xC8,
  0x97,0x66,0x8E,0x82,0x74,0x06,0xC7,0x88,0x1A,0x4A,0x6B,0xCC,0x41,0xE9,0x9D,0xB8,
  0x23,0x9F,0x3D,0xBF,0x8D,0x95,0xC5,0x13,0xB9,0x24,0x5A,0xDC,0x64,0x18,0x38,0x91,
  0x7F,0x5B,0x70,0x54,0x07,0xB6,0x4B,0x0E,0x36,0xAC,0x31,0xE6,0xD6,0x48,0xAA,0xB4
  };

cCamCrypt::cCamCrypt(void)
{
  randomInit=false;
}

void cCamCrypt::GenerateRandom(unsigned char *randVal, int len)
{
  static const unsigned int seed = 0x9E3779B9;
  if(!randomInit) {
    RAND_seed(&seed,sizeof(seed));
    randomInit=true;
    }
  RAND_bytes(randVal,len);
}

//
// Rotates the 8 bytes bitwise right
//
void cCamCrypt::RotateRight8Byte(unsigned char *key)
{
  unsigned char t1=key[0];
  for(int k=7 ; k>=0 ; k--) {
    unsigned char t2=t1<<7;
    t1=key[k]; key[k]=(t1>>1) | t2;
    }
}

//
// Rotates the 8 bytes bitwise left
//
void cCamCrypt::RotateLeft8Byte(unsigned char *key)
{
  unsigned char t1=key[7];
  for(int k=0 ; k<8 ; k++) {
    unsigned char t2=t1>>7;
    t1=key[k]; key[k]=(t1<<1) | t2;
    }
}

void cCamCrypt::RevCamCrypt(const unsigned char *key, unsigned char *data)
{
  unsigned char localKey[8];
  memcpy(localKey,key,sizeof(localKey));
  for(int idx1=0 ; idx1<8 ; idx1++) {
    for(int idx2=0 ; idx2<8 ; idx2++) {
      const unsigned char tmp1=cryptTable[data[7] ^ localKey[idx2] ^ idx1];
      const unsigned char tmp2=data[0];
      data[0]=data[1];
      data[1]=data[2];
      data[2]=data[3];
      data[3]=data[4];
      data[4]=data[5];
      data[5]=data[6] ^ tmp1;
      data[6]=data[7];
      data[7]=tmp1 ^ tmp2 ;
      }
    RotateLeft8Byte(localKey);
    }
}

void cCamCrypt::CamCrypt(const unsigned char *key, unsigned char *data)
{
  unsigned char localKey[8];
  memcpy(localKey,key,sizeof(localKey));
  for(int idx1=0 ; idx1<7 ; idx1++) RotateLeft8Byte(localKey);
  for(int idx1=7 ; idx1>=0 ; idx1--) {
    for(int idx2=7 ; idx2>=0 ; idx2--) {
      const unsigned char tmp1=cryptTable[data[6] ^ localKey[idx2] ^ idx1];
      const unsigned char tmp2=data[0];
      data[0]=data[7] ^ tmp1;
      data[7]=data[6];
      data[6]=data[5] ^ tmp1;
      data[5]=data[4];
      data[4]=data[3];
      data[3]=data[2];
      data[2]=data[1];
      data[1]=tmp2;
      }
    RotateRight8Byte(localKey);
    }
}

bool cCamCrypt::SetupCardFiles(unsigned char *data, int len, BIGNUM *exp, BIGNUM *mod)
{
  if(rsa.RSA(data,data,len,exp,mod,false)<=0 || (data[0]!=0x80 || data[11]!=0x40 || data[12]!=0x06))
    return false;
  BN_complement(data+13,64,cardMod);
  BN_bin2bn(data+13+64,6,cardExp);
  return true;
}

void cCamCrypt::PrepareCamMessage(unsigned char *plain)
{
/*
  static const unsigned char camMsg[] = {
    0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00
    };
  memcpy(plain,camMsg,sizeof(camMsg));
*/
  plain[0]=0x80;
  memset(plain+1 ,0x00,7);
  GenerateRandom(plain+16,16);
  memcpy(plain+8,plain+16,8);
  memset(plain+32,0xFF,31);
  plain[63]=0x00;
}

bool cCamCrypt::EncryptCamMessage(unsigned char *encrypted, const unsigned char *plain)
{
  return rsa.RSA(encrypted,plain,64,cardExp,cardMod,false)>0;
}

// -- cSmartCardDataIrdeto -----------------------------------------------------

class cSmartCardDataIrdeto : public cSmartCardData {
public:
  int acs, caid;
  cBN mod, exp;
  bool plain;
  //
  cSmartCardDataIrdeto(void);
  cSmartCardDataIrdeto(int Acs, int Caid);
  virtual bool Parse(const char *line);
  virtual bool Matches(cSmartCardData *param);
  };

cSmartCardDataIrdeto::cSmartCardDataIrdeto(void)
:cSmartCardData(SC_ID)
{
  plain=false;
}

cSmartCardDataIrdeto::cSmartCardDataIrdeto(int Acs, int Caid)
:cSmartCardData(SC_ID)
{
  acs=Acs; caid=Caid;
  plain=false;
}

bool cSmartCardDataIrdeto::Matches(cSmartCardData *param)
{
  cSmartCardDataIrdeto *cd=(cSmartCardDataIrdeto *)param;
  return cd->acs==acs && (cd->caid==caid || caid==-1);
}

bool cSmartCardDataIrdeto::Parse(const char *line)
{
  unsigned char buff[512];
  acs=caid=-1; // default
  line=skipspace(line);
  if(*line=='[') { // parse acs & caid
    line++;
    if(GetHex(line,buff,2)!=2) {
      PRINTF(L_CORE_LOAD,"smartcarddatairdeto: format error: acs");
      return false;
      }
    acs=buff[0]*256+buff[1];

    line=skipspace(line);
    if(*line=='/') {
      line++;
      if(GetHex(line,buff,2)!=2) {
        PRINTF(L_CORE_LOAD,"smartcarddatairdeto: format error: caid");
        return false;
        }
      caid=buff[0]*256+buff[1];
      line=skipspace(line);
      }

    if(!*line==']') {
      PRINTF(L_CORE_LOAD,"smartcarddatairdeto: format error: closing ]");
      return false;
      }
    line++;
    }

  line=skipspace(line);
  if(!strncasecmp(line,"plain",5)) {
    plain=true;
    return true;
    }
  int l;
  if((l=GetHex(line,buff,sizeof(buff),false))<=0) {
    PRINTF(L_CORE_LOAD,"smartcarddatairdeto: format error: mod");
    return false;
    }
  BN_complement(buff,l,mod);
  if((l=GetHex(line,buff,sizeof(buff),false))<=0) {
    PRINTF(L_CORE_LOAD,"smartcarddatairdeto: format error: exp");
    return false;
    }
  BN_bin2bn(buff,l,exp);
  return true;
}

// -- cSmartCardIrdeto ---------------------------------------------------------

#define XOR_START    0x3F // Start value for xor checksumm
#define ADDRLEN      4    // Address length in EMM commands
#define MAX_PROV     16
#define RECOVER_TIME 100  // Time in ms which the card needs to recover after
                          // a failed command

class cSmartCardIrdeto : public cSmartCard, private cCamCrypt, private cIdSet {
private:
  unsigned char buff[MAX_LEN+1];
  unsigned char camKey[8];
  char asciiSerial[22], coco[4];
  int ACS, caId;
  int numProv;
  cTimeMs recoverTime;
  //
  int DoCmd(unsigned char *cmd, int goodSB, int secGoodSB=-1);
  bool ReadCardInfo(void);
  time_t Date(int date, char *buff, int len);
public:
  cSmartCardIrdeto(void);
  virtual bool Init(void);
  virtual bool Decode(const cEcmInfo *ecm, const unsigned char *data, unsigned char *cw);
  virtual bool Update(int pid, int caid, const unsigned char *data);
  virtual bool CanHandle(unsigned short CaId);
  };

static const struct StatusMsg msgs[] = {
  { { 0x00,0x00 }, "Instruction executed without error", true },
  { { 0x55,0x00 }, "Instruction executed without error", true },
  { { 0x57,0x00 }, "CAM string rejected", false },
  { { 0x58,0x00 }, "Instruction executed without error", true },
  { { 0x9D,0x00 }, "Decoding successfull", true },
  { { 0x90,0x00 }, "ChID missing. Not subscribed?", false },
  { { 0x93,0x00 }, "ChID out of date. Subscription expired?", false },
  { { 0x9C,0x00 }, "Master key error", false },
  { { 0x9E,0x00 }, "Wrong decryption key", false },
  { { 0x9F,0x00 }, "Missing key", false },
  { { 0x70,0x00 }, "Wrong hex serial", false },
  { { 0x71,0x00 }, "Wrong provider", false },
  { { 0x72,0x00 }, "Wrong provider group", false },
  { { 0x73,0x00 }, "Wrong provider group", false },
  { { 0x7C,0x00 }, "Wrong signature", false },
  { { 0x7D,0x00 }, "Masterkey missing", false },
  { { 0x7E,0x00 }, "Wrong provider identifier", false },
  { { 0x7F,0x00 }, "Invalid nano", false },
  { { 0x54,0x00 }, "No more ChID's", true },
  { { 0xFF,0xFF }, 0, false }
  };

static const struct CardConfig cardCfg = {
  SM_8N2,3000,100
  };

cSmartCardIrdeto::cSmartCardIrdeto(void)
:cSmartCard(&cardCfg,msgs)
{
  ACS=0; caId=0; numProv=0;
}

bool cSmartCardIrdeto::Init(void)
{
  ResetIdSet();
  recoverTime.Set(-RECOVER_TIME);
  if(atr->histLen<6 || memcmp(atr->hist,"IRDETO",6)) {
    PRINTF(L_SC_INIT,"doesn't looks like a Irdeto/Beta card");
    return false;
    }

  infoStr.Begin();
  infoStr.Strcat("Irdeto smartcard\n");
  int r;
  static unsigned char getCountryCode[] = { 0x01,0x02,0x02,0x03,0x00,0x00,0xCC };
  if((r=DoCmd(getCountryCode,0x0000))<=0 || !Status() || r<16) {
    PRINTF(L_SC_ERROR,"country code error");
    return false;
    }
  ACS=buff[8]*256+buff[9];
  int c=buff[13]*256+buff[14];
  if(c!=caId) CaidsChanged();
  caId=c;
  memcpy(coco,&buff[21],3); coco[3]=0;
  PRINTF(L_SC_INIT,"ACS Version %04x, CAID %04x, CoCo %s",ACS,caId,coco);
  snprintf(idStr,sizeof(idStr),"%s (ACS %x)",SC_NAME,ACS);
  infoStr.Printf("ACS: %04x CAID: %04x CoCo: %s\n",ACS,caId,coco);

  static unsigned char getAsciiSerial[] = { 0x01,0x02,0x00,0x03,0x00,0x00,0xCC };
  if((r=DoCmd(getAsciiSerial,0x0000))<=0 || !Status() || r<10) {
    PRINTF(L_SC_ERROR,"ASCII serial error");
    return false;
    }
  strn0cpy(asciiSerial,(char*)buff+8,sizeof(asciiSerial));
  PRINTF(L_SC_INIT,"ASCII serial %s",asciiSerial);

  static unsigned char getHexSerial[] = { 0x01,0x02,0x01,0x03,0x00,0x00,0xCC };
  if((r=DoCmd(getHexSerial,0x0000))<=0 || !Status() || r<25) {
    PRINTF(L_SC_ERROR,"hex serial error");
    return false;
    }
  int numProv=buff[18];
  SetCard(new cCardIrdeto(buff[23],&buff[20]));
  PRINTF(L_SC_INIT,"Providers: %d HEX Serial: %02X%02X%02X  HEX Base: %02X",numProv,buff[20],buff[21],buff[22],buff[23]);
  infoStr.Printf("HEX: %02X/%02X%02X%02X ASCII: %s\n",buff[23],buff[20],buff[21],buff[22],asciiSerial);

  static unsigned char getCardFile[] = { 0x01,0x02,0x0E,0x02,0x00,0x00,0xCC };
  unsigned char encr[128], plain[64+6+2];
  PrepareCamMessage(plain+6);
  getCardFile[3]=0x02;
  if((r=DoCmd(getCardFile,0x0000))<=0 || !Status() || r<73) {
    PRINTF(L_SC_ERROR,"cardfile2 error");
    return false;
    }
  memcpy(encr,buff+8,64);
  getCardFile[3]=0x03;
  if((r=DoCmd(getCardFile,0x0000))<=0 || !Status() || r<73) {
    PRINTF(L_SC_ERROR,"cardfile3 error");
    return false;
    }
  memcpy(encr+64,buff+8,64);

  bool doPlain=false;
  if((ACS==0x0383 || ACS==0x0384) && atr->histLen>=12 && atr->hist[12]==0x95)
    doPlain=true;
  cSmartCardDataIrdeto *entry=0;
  if(!doPlain) {
    cSmartCardDataIrdeto cd(ACS,caId);
    if(!(entry=(cSmartCardDataIrdeto *)smartcards.FindCardData(&cd))) {
      PRINTF(L_GEN_WARN,"didn't find Irdeto card specific certificate, falling back to default");
      cSmartCardDataIrdeto cd(-1,-1);
      if(!(entry=(cSmartCardDataIrdeto *)smartcards.FindCardData(&cd))) {
        PRINTF(L_GEN_WARN,"didn't find default Irdeto certificate, please add one");
        if(ACS!=0x0384) return false;
        PRINTF(L_GEN_WARN,"trying pre-coded ACS 384 challenge. This mode is DEPRECATED. There ARE valid certificates for these cards available!");
        }
      }
    else doPlain=entry->plain;
    }
  static unsigned char doCamKeyExchange[] = { 0x01,0x02,0x09,0x03,0x00,0x40 };
  if(doPlain) {
    // plain challenge
    memcpy(plain,doCamKeyExchange,sizeof(doCamKeyExchange));
    plain[4]=1; // set block counter
    r=DoCmd(plain,0x5500,0x0000);
    }
  else {
    // RSA challenge
    if(entry) {
      if(!SetupCardFiles(encr,sizeof(encr),entry->exp,entry->mod)) {
        PRINTF(L_SC_ERROR,"decrypting cardfiles failed. Probably bad certificate.");
        return false;
        }
      if(!EncryptCamMessage(encr+6,plain+6)) {
        PRINTF(L_SC_ERROR,"encrypting CAM message failed. Probably bad certificate.");
        return false;
        }

      static unsigned char doRSACheck[] = { 0x01,0x02,0x11,0x00,0x00,0x40 };
      memcpy(plain,doRSACheck,sizeof(doRSACheck));
      if((r=DoCmd(plain,0x5800,0x0000))<=0 || !Status() || r<73) {
        PRINTF(L_SC_ERROR,"card didn't give a proper reply (buggy RSA unit?), trying to continue...");
        // non-fatal
        }
      if(r==73 && memcmp(encr+6,buff+8,64)) {
        PRINTF(L_SC_ERROR,"card failed on RSA check, trying to continue...");
        // non-fatal
        }
      }
    else {
      static const unsigned char enc384cz[] = {
        0x18,0xD7,0x55,0x14,0xC0,0x83,0xF1,0x38,  0x39,0x6F,0xF2,0xEC,0x4F,0xE3,0xF1,0x85,
        0x01,0x46,0x06,0xCE,0x7D,0x08,0x2C,0x74,  0x46,0x8F,0x72,0xC4,0xEA,0xD7,0x9C,0xE0,
        0xE1,0xFF,0x58,0xE7,0x70,0x0C,0x92,0x45,  0x26,0x18,0x4F,0xA0,0xE2,0xF5,0x9E,0x46,
        0x6F,0xAE,0x95,0x35,0xB0,0x49,0xB2,0x0E,  0xA4,0x1F,0x8E,0x47,0xD0,0x24,0x11,0xD0
        };
      static const unsigned char enc384dz[] = {
        0x27,0xF2,0xD6,0xCD,0xE6,0x88,0x62,0x46,  0x81,0xB0,0xF5,0x3E,0x6F,0x13,0x4D,0xCC,
        0xFE,0xD0,0x67,0xB1,0x93,0xDD,0xF4,0xDE,  0xEF,0xF5,0x3B,0x04,0x1D,0xE5,0xC3,0xB2,
        0x54,0x38,0x57,0x7E,0xC8,0x39,0x07,0x2E,  0xD2,0xF4,0x05,0xAA,0x15,0xB5,0x55,0x24,
        0x90,0xBB,0x9B,0x00,0x96,0xF0,0xCB,0xF1,  0x8A,0x08,0x7F,0x0B,0xB8,0x79,0xC3,0x5D
        };
      static const unsigned char ck[] = { 0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88 };
      static const unsigned char hk[] = { 0x12,0x34,0x56,0x78,0x90,0xAB,0xCD,0xEF };

      if(caId==0x1702) memcpy(encr+6,enc384cz,sizeof(enc384cz));
      else if(caId==0x1722) memcpy(encr+6,enc384dz,sizeof(enc384dz));
      else {
        PRINTF(L_GEN_WARN,"no pre-coded Irdeto camkey challenge for caid %04x",caId);
        return false;
        }
      memcpy((void *)CamKey(plain+6),ck,sizeof(ck));
      memcpy((void *)HelperKey(plain+6),hk,sizeof(hk));
      }

    memcpy(encr,doCamKeyExchange,sizeof(doCamKeyExchange));
    r=DoCmd(encr,0x5500,0x0000);
    }

  if(r>0 && Status() && r>=9) {
    memcpy(camKey,CamKey(plain+6),8);
    if(r>=17) {
      RevCamCrypt(camKey,buff+8);
      if(memcmp(HelperKey(plain+6),buff+8,8)) {
        PRINTF(L_SC_ERROR,"camkey challenge failed");
        return false;
        }
      }
    LDUMP(L_SC_INIT,camKey,sizeof(camKey),"camkey");
    }
  else {
    PRINTF(L_SC_ERROR,"camkey error");
    return false;
    }

  static unsigned char getProvider[]  = { 0x01,0x02,0x03,0x03,0x00,0x00,0xCC };
  static unsigned char getChanelIds[] = { 0x01,0x02,0x04,0x00,0x00,0x01,0x00,0xCC };
  for(int i=0; i<numProv; i++) {
    getProvider[4]=i;
    if((r=DoCmd(getProvider,0x0000))>0 && Status() && r>=33) {
      AddProv(new cProviderIrdeto(buff[8]&0x0f,&buff[9]));
      PRINTF(L_SC_INIT,"provider %d with ProvBase 0x%02x ProvId 0x%02x%02x%02x",i,buff[8]&0x0f,buff[9],buff[10],buff[11]);
      infoStr.Printf("Provider %d Id: %02X/%02X%02X%02X\n",i,buff[8]&0x0f,buff[9],buff[10],buff[11]);

      getChanelIds[4]=i;
      for(int l=0; l<10; l++) {
        getChanelIds[6]=l;
        if((r=DoCmd(getChanelIds,0x0000,0x5400))<=0 || !Status() || r<69) break;
        for(int k=0; k<buff[7]; k+=6) {
          int chanId=buff[k+8+0]*256+buff[k+8+1];
          int date  =buff[k+8+2]*256+buff[k+8+3];
          int durr  =buff[k+8+4];
          if(chanId!=0x0000 && chanId!=0xFFFF) {
            char sdate[16], edate[16];
            Date(date,sdate,sizeof(sdate));
            Date(date+durr,edate,sizeof(edate));
            PRINTF(L_SC_INIT,"ChanId 0x%04x Date 0x%04x %s Duration 0x%02x %s",chanId,date,sdate,durr,edate);
            infoStr.Printf("ChanId: %04X Date: %s-%s\n",chanId,sdate,edate);
            }
          }
        }
      }
    }

#if 0
  static unsigned char getCountryCode2[] = { 0x01,0x02,0x0B,0x00,0x00,0x00,0xCC };
  if((r=DoCmd(getCountryCode2,0x0000))>0 && Status() && r>=32) {
    PRINTF(L_SC_INIT,"max ChID's %d,%d,%d,%d",buff[14],buff[15],buff[16],buff[17]);
    }
#endif

  infoStr.Finish();
  return true;
}

time_t cSmartCardIrdeto::Date(int date, char *buff, int len)
{
  // Irdeto date starts 01.08.1997 which is
  // 870393600 seconds in unix calendar time
  time_t utcTime=870393600L+date*(24*3600);
  if(buff) {
    struct tm utcTm;
    gmtime_r(&utcTime,&utcTm);
    snprintf(buff,len,"%04d/%02d/%02d",utcTm.tm_year+1900,utcTm.tm_mon+1,utcTm.tm_mday);
    }
  return utcTime;
}

bool cSmartCardIrdeto::CanHandle(unsigned short CaId)
{
  return (CaId==caId);
}

bool cSmartCardIrdeto::Decode(const cEcmInfo *ecm, const unsigned char *data, unsigned char *cw)
{
  static const unsigned char ecmCmd[] = { 0x01,0x05,0x00,0x00,0x02,0x00 };

  int r=SCT_LEN(data)-6;
  if(r<=255) {
    unsigned char cmd[257+sizeof(ecmCmd)];
    memcpy(cmd,ecmCmd,sizeof(ecmCmd));
    cmd[5]=r;
    memcpy(cmd+sizeof(ecmCmd),&data[6],r);
    if((r=DoCmd(cmd,0x9D00))>0) {
      if(Status() && r>=31) {
        RevCamCrypt(camKey,&buff[14]);
        RevCamCrypt(camKey,&buff[22]);
        memcpy(cw,&buff[14],16);
        return true;
        }
      }
    }
  return false;
}

bool cSmartCardIrdeto::Update(int pid, int caid, const unsigned char *data)
{
  static const unsigned char emmCmd[] = { 0x01,0x01,0x00,0x00,0x00,0x00 };

  if(MatchEMM(data)) {
    int len=cParseIrdeto::AddrLen(data)+1;
    if(len<=ADDRLEN) {
      const int dataLen=SCT_LEN(data)-5-len; // sizeof of data bytes (nanos)
      if(dataLen<=255-ADDRLEN) {
        unsigned char cmd[257+sizeof(emmCmd)];
        memcpy(cmd,emmCmd,sizeof(emmCmd));
        cmd[5]=dataLen+ADDRLEN;
        memset(cmd+sizeof(emmCmd),0,ADDRLEN);
        memcpy(cmd+sizeof(emmCmd),&data[3],len);
        memcpy(cmd+sizeof(emmCmd)+ADDRLEN,&data[len+5],dataLen);
        if(DoCmd(cmd,0x0000)>0 && Status()) return true;
        }
      }
    else PRINTF(L_SC_ERROR,"addrlen %d > %d",len,ADDRLEN);
    }
  return false;
}

int cSmartCardIrdeto::DoCmd(unsigned char *cmd, int goodSB, int secGoodSB)
{
  int len=cmd[5]+6;
  cmd[len]=XorSum(cmd,len) ^ XOR_START;
  // wait until recover time is over
  int r=RECOVER_TIME-recoverTime.Elapsed();
  if(r>0) {
    PRINTF(L_SC_ERROR,"recover time, waiting %d ms",r);
    cCondWait::SleepMs(r+1);
    }
  r=-1;
  LDUMP(L_CORE_SC,cmd,len+1,"IRDETO: CMD ->");
  if(SerWrite(cmd,len+1)>0 && SerRead(buff,4,cardCfg.workTO)>0) {
    len=4;
    if(buff[0]==cmd[0] && buff[1]==cmd[1]) {
      sb[0]=buff[2]; sb[1]=buff[3];
      int SB=buff[2]*256+buff[3];
      if(SB==goodSB || (secGoodSB>=0 && SB==secGoodSB)) {
        if(SerRead(buff+len,5)>0) {
          len+=5;
          if(buff[7]) {
            if(SerRead(buff+len,buff[7])<=0) return -1;
            len+=buff[7];
            }
          if(XorSum(buff,len)==XOR_START) r=len;
          else LDUMP(L_CORE_SC,buff,len,"IRDETO: checksum failed");
          }
        }
      else r=len;
      }
    else {
      sb[0]=buff[1]; sb[1]=buff[2];
      r=3;
      }
    }
  if(r>0) LDUMP(L_CORE_SC,buff,r,"IRDETO: RESP <-");
  if(r<=4) {
    recoverTime.Set();
    PRINTF(L_SC_ERROR,"setting %d ms recover time",RECOVER_TIME);
    }
  return r;
}

// -- cSmartCardLinkIrdeto -----------------------------------------------------

class cSmartCardLinkIrdeto : public cSmartCardLink {
public:
  cSmartCardLinkIrdeto(void):cSmartCardLink(SC_NAME,SC_ID) {}
  virtual cSmartCard *Create(void) { return new cSmartCardIrdeto(); }
  virtual cSmartCardData *CreateData(void) { return new cSmartCardDataIrdeto; }
  };

static cSmartCardLinkIrdeto staticScInit;
