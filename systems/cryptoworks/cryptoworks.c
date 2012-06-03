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
#include <byteswap.h>

#include "system-common.h"
#include "data.h"
#include "opts.h"
#include "helper.h"
#include "crypto.h"
#include "misc.h"
#include "log-sys.h"
#include "version.h"

SCAPIVERSTAG();

#define SYSTEM_CRYPTOWORKS   0x0D00

#define SYSTEM_NAME          "Cryptoworks"
#define SYSTEM_PRI           -10
#define SYSTEM_CAN_HANDLE(x) ((x)==SYSTEM_CRYPTOWORKS)

#define L_SYS        4
#define L_SYS_ALL    LALL(L_SYS_LASTDEF)

static const struct LogModule lm_sys = {
  (LMOD_ENABLE|L_SYS_ALL)&LOPT_MASK,
  (LMOD_ENABLE|L_SYS_DEFDEF)&LOPT_MASK,
  "cryptoworks",
  { L_SYS_DEFNAMES }
  };
ADD_MODULE(L_SYS,lm_sys)

#define DUMPNANO(t,p,n) \
  LBSTART(L_SYS_VERBOSE); \
  LBPUT("%s",(t)); \
  unsigned char *__p=(unsigned char *)(p); \
  int __n=(n); \
  for(int i=0; i<__n;) { \
    LBFLUSH(); \
    LBPUT("%02X %02X -",__p[i],__p[i+1]); \
    i+=2; \
    for(int l=__p[i-1]; l>0; l--) { LBPUT(" %02X",__p[i]); i++; }\
    if(i>__n) { LBFLUSH(); LBPUT("length exceeded %d != %d",i,__n); } \
    } \
  LBEND();

static int minEcmTime=150; // ms

// -- cCwDes -------------------------------------------------------------------

static const unsigned char cryptoPC1[] = {
   53,46,39,32,50,43,36,29,
   22,15, 8, 1,51,44,37,30,
   23,16, 9, 2,52,45,38,31,
   24,17,10, 3,53,46,39,32,
   25,18,11, 4,56,49,42,35,
   28,21,14, 7,55,48,41,34,
   27,20,13, 6,54,47,40,33,
   26,19,12, 5,25,18,11, 4
  };

static const unsigned char cryptoPC2[] = {
  18,21,15,28, 5, 9,   7,32,19,10,25,14,
  27,23,16, 8,30,12,  20,11,31,24,17, 6,
  49,60,39,45,55,63,  38,48,59,53,41,56,
  52,57,47,64,42,61,  54,50,58,44,37,40,
  };

#define shiftin(V,R,n) ((V<<1)+(((R)>>(n))&1))
#define rol28(V,n) (V<<(n) | ((V&0x0fffffffL)>>(28-(n))))
#define ror28(V,n) (V>>(n) | ((V&0xfffffff0L)<<(28-(n))))

#define DESROUND(C,D,T) { \
   unsigned int s=0; \
   for(int j=7, k=0; j>=0; j--) { \
     unsigned int v=0, K=0; \
     for(int t=5; t>=0; t--, k++) { \
       v=shiftin(v,T,E[k]); \
       if(PC2[k]<33) K=shiftin(K,C,32-PC2[k]); \
       else          K=shiftin(K,D,64-PC2[k]); \
       } \
     s=(s<<4) + S[7-j][v^K]; \
     } \
   T=0; \
   for(int j=31; j>=0; j--) T=shiftin(T,s,P[j]); \
   }

class cCwDes : public cDes {
private:
  void PrepKey(unsigned char *out, const unsigned char *key) const;
  void PrepKey48(unsigned char *out, const unsigned char *key22, unsigned char algo) const;
public:
  cCwDes(void);
  void CwDes(unsigned char *data, const unsigned char *key22, int mode) const;
  void CwL2Des(unsigned char *data, const unsigned char *key22, unsigned char algo) const;
  void CwR2Des(unsigned char *data, const unsigned char *key22, unsigned char algo) const;
  };

cCwDes::cCwDes(void)
:cDes(cryptoPC1,cryptoPC2)
{}

void cCwDes::PrepKey(unsigned char *out, const unsigned char *key) const
{
  if(key!=out) memcpy(out,key,8);
  Permute(out,PC1,64);
}

void cCwDes::PrepKey48(unsigned char *out, const unsigned char *key22, unsigned char algo) const
{
  memset(out,0,8);
  memcpy(out,&key22[16],6);
  int ctr=7-(algo&7);
  for(int i=8; i>=2 && i>ctr; i--) out[i-2]=key22[i];
}

void cCwDes::CwDes(unsigned char *data, const unsigned char *key22, int mode) const
{
  unsigned char mkey[8];
  PrepKey(mkey,key22+9);
  LDUMP(L_SYS_VERBOSE,mkey,8,"prepped key DES:");
  unsigned int C=UINT32_BE(mkey  );
  unsigned int D=UINT32_BE(mkey+4);
  unsigned int L=UINT32_BE(data  );
  unsigned int R=UINT32_BE(data+4);
  if(!(mode&DES_RIGHT)) {
    for(int i=15; i>=0; i--) {
      C=rol28(C,LS[15-i]); D=rol28(D,LS[15-i]);
      unsigned int T=R;
      DESROUND(C,D,T);
      T^=L; L=R; R=T;
      }
    }
  else {
    for(int i=15; i>=0; i--) {
      unsigned int T=R;
      DESROUND(C,D,T);
      T^=L; L=R; R=T;
      C=ror28(C,LS[i]); D=ror28(D,LS[i]);
      }
    }
  BYTE4_BE(data  ,L);
  BYTE4_BE(data+4,R);
}

void cCwDes::CwL2Des(unsigned char *data, const unsigned char *key22, unsigned char algo) const
{
  unsigned char mkey[8];
  PrepKey48(mkey,key22,algo);
  PrepKey(mkey,mkey);
  LDUMP(L_SYS_VERBOSE,mkey,8,"prepped key L2:");
  unsigned int C=UINT32_BE(mkey  );
  unsigned int D=UINT32_BE(mkey+4);
  unsigned int L=UINT32_BE(data  );
  unsigned int R=UINT32_BE(data+4);
  for(int i=1; i>=0; i--) {
    C=rol28(C,1); D=rol28(D,1);
    unsigned int T=R;
    DESROUND(C,D,T);
    T^=L; L=R; R=T;
    PRINTF(L_SYS_VERBOSE,"round %d key L2: %08x %08x",i,C,D);
    }
  BYTE4_BE(data  ,L);
  BYTE4_BE(data+4,R);
}

void cCwDes::CwR2Des(unsigned char *data, const unsigned char *key22, unsigned char algo) const
{
  unsigned char mkey[8];
  PrepKey48(mkey,key22,algo);
  PrepKey(mkey,mkey);
  LDUMP(L_SYS_VERBOSE,mkey,8,"prepped key R2:");
  unsigned int C=UINT32_BE(mkey  );
  unsigned int D=UINT32_BE(mkey+4);
  unsigned int L=UINT32_BE(data  );
  unsigned int R=UINT32_BE(data+4);
  for(int i=1; i>=0; i--) {
    C=rol28(C,15); D=rol28(D,15);
    }
  for(int i=1; i>=0; i--) {
    unsigned int T=R;
    DESROUND(C,D,T);
    T^=L; L=R; R=T;
    C=ror28(C,1); D=ror28(D,1);
    }
  BYTE4_BE(data  ,R);
  BYTE4_BE(data+4,L);
}

// -- cCryptoworks -------------------------------------------------------------

class cCryptoworks {
private:
  cCwDes des;
  cRSA rsa;
  cBN exp;
  //
  void EncDec(unsigned char *data, const unsigned char *key22, unsigned char algo, int mode);
public:
  cCryptoworks(void);
  void Signature(const unsigned char *data, int len, const unsigned char *key22, unsigned char *sig);
  void DecryptDES(unsigned char *data, unsigned char algo, const unsigned char *key22);
  bool DecryptRSA(unsigned char *data, int len, unsigned char algo, const unsigned char *key22, BIGNUM *mod);
  };

cCryptoworks::cCryptoworks(void)
{
  BN_set_word(exp,2);
}

void cCryptoworks::EncDec(unsigned char *data, const unsigned char *key22, unsigned char algo, int mode)
{
  LDUMP(L_SYS_VERBOSE,data,8,"encdec in :");
  PRINTF(L_SYS_VERBOSE,"algo %d",algo);
  LDUMP(L_SYS_VERBOSE,key22,22,"encdec key:");
  des.CwL2Des(data,key22,algo);
  des.CwDes(data,key22,mode);
  des.CwR2Des(data,key22,algo);
  LDUMP(L_SYS_VERBOSE,data,8,"encdec out:");
}

void cCryptoworks::Signature(const unsigned char *data, int len, const unsigned char *key22, unsigned char *sig)
{
  PRINTF(L_SYS_VERBOSE,"sig start");
  int algo=data[0]&7;
  if(algo==7) algo=6;
  memset(sig,0,8);
  int j=0;
  bool first=true;
  for(int i=0; i<len; i++) {
    sig[j]^=data[i];
    if(++j>7) {
      LDUMP(L_SYS_VERBOSE,sig,8,"sig buf");
      if(first) {
        des.CwL2Des(sig,key22,algo);
        LDUMP(L_SYS_VERBOSE,sig,8,"sig -> ");
        }
      des.CwDes(sig,key22,DES_LEFT);
      j=0; first=false;
      LDUMP(L_SYS_VERBOSE,sig,8,"sig -> ");
      }
    }
  if(j>0) {
    LDUMP(L_SYS_VERBOSE,sig,8,"sig buf final ");
    des.CwDes(sig,key22,DES_LEFT);
    LDUMP(L_SYS_VERBOSE,sig,8,"sig        -> ");
    }
  des.CwR2Des(sig,key22,algo);
  LDUMP(L_SYS_VERBOSE,sig,8,"sig calc ");
}

void cCryptoworks::DecryptDES(unsigned char *data, unsigned char algo, const unsigned char *key22)
{
  algo&=7;
  if(algo<7) {
    EncDec(data,key22,algo,DES_RIGHT);
    }
  else {
    unsigned char k[22], t[8];
    memcpy(k,key22,22);
    for(int i=0; i<3; i++) {
      EncDec(data,k,algo,i&1);
      memcpy(t,k,8); memcpy(k,k+8,8); memcpy(k+8,t,8);
      }
    }
}

bool cCryptoworks::DecryptRSA(unsigned char *data, int len, unsigned char algo, const unsigned char *key22, BIGNUM *mod)
{
  unsigned char buf[64], *mask=AUTOMEM(len);

  LDUMP(L_SYS_VERBOSE,data+len,8,"rsa in:");
  memcpy(buf,data+len,8);
  EncDec(buf,key22,algo,DES_LEFT);
  buf[0]|=0x80;
  if((algo&0x18)<0x18) buf[0]=0xFF;
  if(algo&8) buf[1]=0xFF;
  LDUMP(L_SYS_VERBOSE,buf,8,"rsa seed:");
  
  static const unsigned char t1[] = { 0xE,0x3,0x5,0x8,0x9,0x4,0x2,0xF,0x0,0xD,0xB,0x6,0x7,0xA,0xC,0x1 };
  for(int k=0; k<len; k+=32) {
    memcpy(buf+8,buf,8);
    for(int i=0; i<8; i++) {
      int n=i<<1;
      buf[n+1]=buf[i+8];
      buf[n  ]=(t1[buf[n+1]>>4]<<4) | t1[buf[i+8]&0xF];
      LDUMP(L_SYS_VERBOSE,buf,16,"rsa buf:");
      }
    for(int i=16; i<64; i+=16) memcpy(&buf[i],buf,16);
    buf[31]=((buf[15]<<4)&0xFF) | 6;
    buf[16]=buf[0]^1;
    buf[32]&=0x7F;
    buf[32]|=0x40;
    RotateBytes(buf,32);
    RotateBytes(buf+32,32);
    LDUMP(L_SYS_VERBOSE,buf,64,"rsa data:");

    if(rsa.RSA(buf,buf,64,exp,mod,true)==0) {
      PRINTF(L_SYS_CRYPTO,"RSA failed");
      return false;
      }
    RotateBytes(buf,8);
    RotateBytes(mask+k,buf+8,min(32,len-k));
    }
  LDUMP(L_SYS_VERBOSE,mask,len,"rsa out:");

  xxor(data,len,data,mask);
  return true;
}

// -- cPlainKeyCryptoworks -----------------------------------------------------

#define PLAINLEN_CW_D  16
#define PLAINLEN_CW_CC  6
#define PLAINLEN_CW_R  64

#define CCTYP               0x00
#define CCID                0xFF

#define PROV(keynr)         (((keynr)>>16)&0xFF)
#define TYPE(keynr)         (((keynr)>> 8)&0xFF)
#define ID(keynr)           (((keynr)   )&0xFF)
#define KEYSET(prov,typ,id) ((((prov)&0xFF)<<16)|(((typ)&0xFF)<<8)|((id)&0xFF))

class cPlainKeyCryptoworks : public cDualKey {
protected:
  virtual bool IsBNKey(void) const;
  virtual int IdSize(void) { return 4; }
  virtual cString PrintKeyNr(void);
public:
  cPlainKeyCryptoworks(bool Super);
  virtual bool Parse(const char *line);
  };

static cPlainKeyTypeReg<cPlainKeyCryptoworks,'W'> KeyReg;

cPlainKeyCryptoworks::cPlainKeyCryptoworks(bool Super)
:cDualKey(Super,true)
{}

bool cPlainKeyCryptoworks::IsBNKey(void) const
{
  return TYPE(keynr)==0x10;
}

bool cPlainKeyCryptoworks::Parse(const char *line)
{
  unsigned char sid[2], sprov;
  if(GetChar(line,&type,1) && GetHex(line,sid,2) && GetHex(line,&sprov,1)) {
    int keylen, prov;
    type=toupper(type); id=Bin2Int(sid,2); prov=sprov;
    line=skipspace(line);
    bool ok;
    if(!strncasecmp(line,"CC",2)) { // cardkey
      keynr=KEYSET(prov,CCTYP,CCID);
      keylen=PLAINLEN_CW_CC;
      line+=2;
      ok=true;
      }
    else {
      unsigned char sid, styp;
      ok=GetHex(line,&styp,1) && GetHex(line,&sid,1);
      keynr=KEYSET(prov,styp,sid);
      keylen=IsBNKey() ? PLAINLEN_CW_R : PLAINLEN_CW_D;
      }
    if(ok) {
      unsigned char *skey=AUTOMEM(keylen);
      if(GetHex(line,skey,keylen)) {
        SetBinKey(skey,keylen);
        return true;
        }
      }
    }
  return false;
}

cString cPlainKeyCryptoworks::PrintKeyNr(void)
{
  int prov=PROV(keynr);
  int keytyp=TYPE(keynr);
  int keyid=ID(keynr);
  return cString::sprintf(keytyp==CCTYP && keyid==CCID ? "%02X CC":"%02X %02X %02X",prov,keytyp,keyid);
}

// -- cSystemCryptoworks -------------------------------------------------------

#define ECM_ALGO_TYP   5
#define ECM_DATA_START ECM_ALGO_TYP
#define ECM_NANO_LEN   7
#define ECM_NANO_START 8

class cSystemCryptoworks : public cSystem, private cCryptoworks {
private:
public:
  cSystemCryptoworks(void);
  virtual bool ProcessECM(const cEcmInfo *ecmInfo, unsigned char *data);
//  virtual void ProcessEMM(int pid, int caid, unsigned char *data);
  };

cSystemCryptoworks::cSystemCryptoworks(void)
:cSystem(SYSTEM_NAME,SYSTEM_PRI)
{
//  hasLogger=true;
}

bool cSystemCryptoworks::ProcessECM(const cEcmInfo *ecmInfo, unsigned char *data)
{
  cTimeMs minTime;
  int len=SCT_LEN(data);
  if(data[ECM_NANO_LEN]!=len-ECM_NANO_START) {
    PRINTF(L_SYS_ECM,"invalid ECM structure");
    return false;
    }

  int prov=-1, keyid=0;
  for(int i=ECM_NANO_START; i<len; i+=data[i+1]+2) {
    if(data[i]==0x83) {
      prov =data[i+2]&0xFC;
      keyid=data[i+2]&0x03;
      break;
      }
    }
  if(prov<0) {
    PRINTF(L_SYS_ECM,"provider ID not located in ECM");
    return false;
    }

  unsigned char key[22];
  cPlainKey *pk;
  if(!(pk=keys.FindKey('W',ecmInfo->caId,KEYSET(prov,CCTYP,CCID),6))) {
    if(doLog) PRINTF(L_SYS_KEY,"missing %04X %02X CC key",ecmInfo->caId,prov);
    return false;
    }
  pk->Get(key+16);

  // RSA stage
  DUMPNANO("pre-RSA",&data[ECM_NANO_START],len-ECM_NANO_START);
  for(int i=ECM_NANO_START; i<len; i+=data[i+1]+2) {
    int l=data[i+1]+2;
    switch(data[i]) {
      case 0x85:
        {
        if(!(pk=keys.FindKey('W',ecmInfo->caId,KEYSET(prov,0x31,keyid),16))) {
          if(doLog) PRINTF(L_SYS_KEY,"missing %04X %02X 31 %02X key",ecmInfo->caId,prov,keyid);
          return false;
          }
        pk->Get(key);
        cBN mod;
        if(!(pk=keys.FindKey('W',ecmInfo->caId,KEYSET(prov,0x10,0x00),64))) {
          if(doLog) PRINTF(L_SYS_KEY,"missing %04X %02X 10 00 key",ecmInfo->caId,prov);
          return false;
          }
        pk->Get(mod);

        l-=10;
        if(!DecryptRSA(&data[i+2],l,data[ECM_ALGO_TYP],key,mod))
          return false;
        memmove(&data[i],&data[i+2],l);
        memmove(&data[i+l],&data[i+l+10],len-i-l);
        len-=10;
        break;
        }
      case 0x86:
        memmove(&data[i],&data[i+l],len-i-l);
        len-=l;
        continue;
      }
    }
  DUMPNANO("post-RSA",&data[ECM_NANO_START],len-ECM_NANO_START);

  cKeySnoop ks(this,'W',ecmInfo->caId,KEYSET(prov,0x20,keyid));
  if(!(pk=keys.FindKey('W',ecmInfo->caId,KEYSET(prov,0x20,keyid),16))) {
    if(doLog) PRINTF(L_SYS_KEY,"missing %04X %02X 20 %02X key",ecmInfo->caId,prov,keyid);
    return false;
    }
  pk->Get(key);

  // DES stage
  unsigned char sig[8];
  LDUMP(L_SYS_VERBOSE,&data[len-8],8,"sig org:");
  data[ECM_NANO_LEN]=len-ECM_NANO_START;
  Signature(&data[ECM_DATA_START],len-ECM_DATA_START-10,key,sig);
  for(int i=ECM_NANO_START; i<len; i+=data[i+1]+2) {
    switch(data[i]) {
      case 0xDA:
      case 0xDB:
      case 0xDC:
        for(int j=0; j<data[i+1]; j+=8)
          DecryptDES(&data[i+2+j],data[ECM_ALGO_TYP],key);
        break;
      case 0xDF:
        if(memcmp(&data[i+2],sig,8)) {
          PRINTF(L_SYS_ECM,"signature failed in ECM");
          return false;
          }
        break;
      }
    }
  
  // CW stage
  for(int i=ECM_NANO_START; i<len; i+=data[i+1]+2) {
    switch(data[i]) {
      case 0xDB:
        if(data[i+1]==0x10) {
          memcpy(cw,&data[i+2],16);
          LDUMP(L_SYS_VERBOSE,cw,16,"cw:");
          ks.OK(pk);
          int i=minEcmTime-minTime.Elapsed();
          if(i>0) cCondWait::SleepMs(i);
          return true;
          }
        break;
      }
    }

  return false;
}

/*
void cSystemCryptoworks::ProcessEMM(int pid, int caid, unsigned char *data)
{
}
*/

// -- cSystemLinkCryptoworks ---------------------------------------------------

class cSystemLinkCryptoworks : public cSystemLink {
public:
  cSystemLinkCryptoworks(void);
  virtual bool CanHandle(unsigned short SysId);
  virtual cSystem *Create(void) { return new cSystemCryptoworks; }
  };

static cSystemLinkCryptoworks staticInit;

cSystemLinkCryptoworks::cSystemLinkCryptoworks(void)
:cSystemLink(SYSTEM_NAME,SYSTEM_PRI)
{
  opts=new cOpts(SYSTEM_NAME,1);
  opts->Add(new cOptInt("MinEcmTime",trNOOP("Cryptoworks: min. ECM processing time"),&minEcmTime,0,5000));
  Feature.NeedsKeyFile();
}

bool cSystemLinkCryptoworks::CanHandle(unsigned short SysId)
{
  SysId&=SYSTEM_MASK;
  return SYSTEM_CAN_HANDLE(SysId);
}
