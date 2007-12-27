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

#include "system.h"
#include "misc.h"
#include "opts.h"
#include "network.h"
#include "crypto.h"
#include "helper.h"

#include <openssl/des.h>
#include <openssl/sha.h>
#include "openssl-compat.h"

#include "nagra.h"
#include "cpu.h"
#include "log-nagra.h"

#define SYSTEM_NAME          "Nagra2"
#define SYSTEM_PRI           -10

// -- cN2Emu -------------------------------------------------------------------

class cN2Emu : protected c6805 {
private:
  bool initDone;
protected:
  bool Init(int id, int romv);
  virtual void Stepper(void) {}
public:
  cN2Emu(void);
  virtual ~cN2Emu() {}
  };

cN2Emu::cN2Emu(void)
{
  initDone=false;
}

bool cN2Emu::Init(int id, int romv)
{
  if(!initDone) {
    ResetMapper();
    char buff[256];
    snprintf(buff,sizeof(buff),"ROM%d.bin",romv);
    // UROM  0x00:0x4000-0x7fff
    if(!AddMapper(new cMapRom(0x4000,buff,0x00000),0x4000,0x4000,0x00)) return false;
    // ROM00 0x00:0x8000-0xffff
    if(!AddMapper(new cMapRom(0x8000,buff,0x04000),0x8000,0x8000,0x00)) return false;
    // ROM01 0x01:0x8000-0xffff
    if(!AddMapper(new cMapRom(0x8000,buff,0x0C000),0x8000,0x8000,0x01)) return false;
    // ROM02 0x02:0x8000-0xbfff
    if(!AddMapper(new cMapRom(0x8000,buff,0x14000),0x8000,0x4000,0x02)) return false;

    snprintf(buff,sizeof(buff),"EEP%02X_%d.bin",(id>>8)&0xFF,romv);
    // Eeprom00 0x00:0x3000-0x37ff OTP 0x80
    if(!AddMapper(new cMapRom(0x3000,buff,0x0000),0x3000,0x0800,0x00)) return false;
    //XXX if(!AddMapper(new cMapEeprom(0x3000,buff,128,0x0000),0x3000,0x0800,0x00)) return false;
    // Eeprom80 0x80:0x8000-0xbfff
    if(!AddMapper(new cMapRom(0x8000,buff,0x0800),0x8000,0x4000,0x80)) return false;
    //XXX if(!AddMapper(new cMapEeprom(0x8000,buff,  0,0x0800),0x8000,0x4000,0x80)) return false;
    initDone=true;
    }
  return true;
}

// -- cMapCore -----------------------------------------------------------------

#define SETSIZE  0x02
#define IMPORT_J 0x03
#define IMPORT_A 0x04
#define IMPORT_B 0x05
#define IMPORT_C 0x06
#define IMPORT_D 0x07
#define EXPORT_A 0x0A
#define EXPORT_B 0x0B
#define EXPORT_C 0x0C
#define EXPORT_D 0x0D

class cMapCore {
private:
  cBN x, y, s, j;
  SHA_CTX sctx;
protected:
  cBN A, B, C, D, J;
  cBN H, R;
  cBNctx ctx;
  int wordsize;
  //
  void ImportReg(unsigned char reg, const unsigned char *data, int l=0);
  void ExportReg(unsigned char reg, unsigned char *data, int l=0, bool BE=false);
  void SetWordSize(int l) { wordsize=l; }
  void MakeJ(void);
  void MonMul(BIGNUM *o, BIGNUM *i1, BIGNUM *i2);
  bool DoMap(int f, unsigned char *data=0, int l=0);
public:
  cMapCore(void);
  };

cMapCore::cMapCore(void)
{
  wordsize=4;
}

void cMapCore::ImportReg(unsigned char reg, const unsigned char *in, int l)
{
  l=(l?l:wordsize)<<3;
  switch(reg) {
    case IMPORT_J: J.GetLE(in,8); break;
    case IMPORT_A: A.GetLE(in,l); break;
    case IMPORT_B: B.GetLE(in,l); break;
    case IMPORT_C: C.GetLE(in,l); break;
    case IMPORT_D: D.GetLE(in,l); break;
    default: PRINTF(L_GEN_DEBUG,"internal: nagramap import register not supported"); return;
    }
}

void cMapCore::ExportReg(unsigned char reg, unsigned char *out, int l, bool BE)
{
  l=(l?l:wordsize)<<3;
  cBN *ptr;
  switch(reg) {
    case EXPORT_A: ptr=&A; break;
    case EXPORT_B: ptr=&B; break;
    case EXPORT_C: ptr=&C; break;
    case EXPORT_D: ptr=&D; break;
    default: PRINTF(L_GEN_DEBUG,"internal: nagramap export register not supported"); return;
    }
  if(!BE) ptr->PutLE(out,l);
  else ptr->Put(out,l);
}

void cMapCore::MakeJ(void)
{
#if OPENSSL_VERSION_NUMBER < 0x0090700fL
#error BN_mod_inverse is probably buggy in your openssl version
#endif
  BN_zero(x);
  BN_sub(J,x,D);
  BN_set_bit(J,0);
  BN_set_bit(x,64);
  BN_mod_inverse(J,J,x,ctx);
}

void cMapCore::MonMul(BIGNUM *o, BIGNUM *i1, BIGNUM *i2)
{
  int words=(BN_num_bytes(i1)+7)>>3;
  BN_zero(s);
  for(int i=0; i<words; i++) {	
    BN_rshift(x,i1,i<<6);
    BN_mask_bits(x,64);
    BN_mul(x,x,i2,ctx);
    BN_add(s,s,x);

    BN_copy(x,s);
    BN_mask_bits(x,64);
    BN_mul(x,x,J,ctx);
    if(i==(words-1)) {
      BN_lshift(y,x,64);
      BN_add(y,y,x);
      // Low
      BN_rshift(C,y,2);
      BN_add(C,C,s);
      BN_rshift(C,C,52);
      BN_mask_bits(C,12);
      }

    BN_mask_bits(x,64);
    BN_mul(x,x,D,ctx);
    BN_add(s,s,x);
    if(i==(words-1)) {
      // High
      BN_lshift(y,s,12);
      BN_add(C,C,y);
      BN_mask_bits(C,wordsize<<6);
      }

    BN_rshift(s,s,64);
    if(BN_cmp(s,D)==1) {
      BN_copy(x,s);
      BN_sub(s,x,D);
      }
    }
  BN_copy(o,s);
}

bool cMapCore::DoMap(int f, unsigned char *data, int l)
{
  switch(f) {
    case 0x43: // init SHA1
      SHA1_Init(&sctx);
      break;
    case 0x44: // add 64 bytes to SHA1 buffer
      RotateBytes(data,64);
      SHA1_Update(&sctx,data,64);
      BYTE4_LE(data   ,sctx.h4);
      BYTE4_LE(data+4 ,sctx.h3);
      BYTE4_LE(data+8 ,sctx.h2);
      BYTE4_LE(data+12,sctx.h1);
      BYTE4_LE(data+16,sctx.h0);
      break;
    case 0x45: // add wordsize bytes to SHA1 buffer and finalize SHA result
      if(wordsize) {
        if(wordsize>1) RotateBytes(data,wordsize);
        SHA1_Update(&sctx,data,wordsize);
        }
      memset(data,0,64);
      SHA1_Final(data+64,&sctx);
      break;
    default:
      return false;
    }
  return true;
}

// -- cN2Prov ------------------------------------------------------------------

class cN2Prov {
private:
  unsigned seed[5], cwkey[8];
  bool keyValid;
  cIDEA idea;
protected:
  int id, flags;
  //
  virtual bool Algo(int algo, const unsigned char *hd, unsigned char *hw) { return false; }
  virtual bool NeedsCwSwap(void) { return false; }
  void ExpandInput(unsigned char *hw);
public:
  cN2Prov(int Id, int Flags);
  virtual ~cN2Prov() {}
  bool MECM(unsigned char in15, int algo, unsigned char *cws);
  void SwapCW(unsigned char *cw);
  virtual int ProcessBx(unsigned char *data, int len, int pos) { return -1; }
  virtual bool PostProcAU(int id, unsigned char *data) { return true; }
  bool CanHandle(int Id) { return ((Id^id)&~0x107)==0; }
  bool HasFlags(int Flags) { return (flags&Flags)==Flags; }
  };

cN2Prov::cN2Prov(int Id, int Flags)
{
  keyValid=false; id=Id; flags=Flags;
}

void cN2Prov::ExpandInput(unsigned char *hw)
{
  hw[0]^=(0xDE +(0xDE<<1)) & 0xFF;
  hw[1]^=(hw[0]+(0xDE<<1)) & 0xFF;
  for(int i=2; i<128; i++) hw[i]^=hw[i-2]+hw[i-1];
  IdeaKS ks;
  idea.SetEncKey((unsigned char *)"NagraVision S.A.",&ks);
  unsigned char buf[8];
  memset(buf,0,8);
  for(int i=0; i<128; i+=8) {
    xxor(buf,8,buf,&hw[i]);
    idea.Encrypt(buf,8,buf,&ks,0);
    xxor(buf,8,buf,&hw[i]);
    memcpy(&hw[i],buf,8);
    }
}

bool cN2Prov::MECM(unsigned char in15, int algo, unsigned char *cw)
{
  unsigned char hd[5], hw[128+64], buf[20];
  hd[0]=in15&0x7F;
  hd[1]=cw[14];
  hd[2]=cw[15];
  hd[3]=cw[6];
  hd[4]=cw[7];

  if(keyValid && !memcmp(seed,hd,5)) {	// key cached
    memcpy(buf,cwkey,8);
    }
  else {				// key not cached
    memset(hw,0,sizeof(hw));
    if(!Algo(algo,hd,hw)) return false;
    memcpy(&hw[128],hw,64);
    RotateBytes(&hw[64],128);
    SHA1(&hw[64],128,buf);
    RotateBytes(buf,20);

    memcpy(seed,hd,5);
    memcpy(cwkey,buf,8);
    keyValid=true;
    }  

  memcpy(&buf[8],buf,8);
  IdeaKS ks;
  idea.SetEncKey(buf,&ks);
  memcpy(&buf[0],&cw[8],6);
  memcpy(&buf[6],&cw[0],6);
  idea.Encrypt(&buf[4],8,&buf[4],&ks,0);
  idea.Encrypt(buf,8,buf,&ks,0);

  memcpy(&cw[ 0],&buf[6],3);
  memcpy(&cw[ 4],&buf[9],3);
  memcpy(&cw[ 8],&buf[0],3);
  memcpy(&cw[12],&buf[3],3);
  for(int i=0; i<16; i+=4) cw[i+3]=cw[i]+cw[i+1]+cw[i+2];
  return true;
}

void cN2Prov::SwapCW(unsigned char *cw)
{
  if(NeedsCwSwap()) {
    unsigned char tt[8];
    memcpy(&tt[0],&cw[0],8);
    memcpy(&cw[0],&cw[8],8);
    memcpy(&cw[8],&tt[0],8);
    }
}

// -- cN2ProvLink & cN2Providers -----------------------------------------------

#define N2FLAG_NONE     0
#define N2FLAG_MECM     1
#define N2FLAG_Bx       2
#define N2FLAG_POSTAU   4
#define N2FLAG_INV      128

class cN2Providers;

class cN2ProvLink {
friend class cN2Providers;
private:
  cN2ProvLink *next;
protected:
  int id, flags;
  //
  virtual cN2Prov *Create(void)=0;
  bool CanHandle(int Id) { return ((Id^id)&~0x107)==0; }
  bool HasFlags(int Flags) { return (flags&Flags)==Flags; }
public:
  cN2ProvLink(int Id, int Flags);
  virtual ~cN2ProvLink() {}
  };

class cN2Providers {
friend class cN2ProvLink;
private:
  static cN2ProvLink *first;
  //
  static void Register(cN2ProvLink *plink);
public:
  static cN2Prov *GetProv(int Id, int Flags);
  };

template<class PROV, int ID, int FLAGS> class cN2ProvLinkReg : public cN2ProvLink {
public:
  cN2ProvLinkReg(void):cN2ProvLink(ID,FLAGS) {}
  virtual cN2Prov *Create(void) { return new PROV(id,flags); }
  };

cN2ProvLink *cN2Providers::first=0;

void cN2Providers::Register(cN2ProvLink *plink)
{
  PRINTF(L_CORE_DYN,"n2providers: registering prov %04X with flags %d",plink->id,plink->flags);
  plink->next=first;
  first=plink;
}

cN2Prov *cN2Providers::GetProv(int Id, int Flags)
{
  cN2ProvLink *pl=first;
  while(pl) {
    if(pl->CanHandle(Id) && pl->HasFlags(Flags)) return pl->Create();
    pl=pl->next;
    }
  return 0;
}

cN2ProvLink::cN2ProvLink(int Id, int Flags)
{
  id=Id; flags=Flags;
  cN2Providers::Register(this);
}

#include "nagra2-prov.c"

#ifndef TESTER

// -- cNagra2 ------------------------------------------------------------------

class cNagra2 : public cNagra {
private:
  bool Signature(const unsigned char *vkey, const unsigned char *sig, const unsigned char *msg, int len);
protected:
  cIDEA idea;
  //
  virtual void CreatePQ(const unsigned char *key, BIGNUM *p, BIGNUM *q);
  bool DecryptECM(const unsigned char *in, unsigned char *out, const unsigned char *key, int len, const unsigned char *vkey, BIGNUM *m);
  bool DecryptEMM(const unsigned char *in, unsigned char *out, const unsigned char *key, int len, const unsigned char *vkey, BIGNUM *m);
  };

void cNagra2::CreatePQ(const unsigned char *key, BIGNUM *p, BIGNUM *q)
{
  // Calculate P and Q from PK
  IdeaKS ks;
  idea.SetEncKey(key,&ks);
  // expand IDEA-G key
  unsigned char idata[96];
  for(int i=11; i>=0; i--) {
    unsigned char *d=&idata[i*8];
    memcpy(d,&key[13],8);
    *d^=i;
    idea.Decrypt(d,8,&ks,0);
    xxor(d,8,d,&key[13]);
    *d^=i;
    }
  // Calculate P
  idata[0] |= 0x80;
  idata[47] |= 1;
  BN_bin2bn(idata,48,p);
  BN_add_word(p,(key[21] << 5 ) | ((key[22] & 0xf0) >> 3));
  // Calculate Q
  idata[48] |= 0x80;
  idata[95] |= 1;
  BN_bin2bn(idata+48,48,q);
  BN_add_word(q,(key[22] &0xf << 9 ) | (key[23]<<1));
}

bool cNagra2::Signature(const unsigned char *vkey, const unsigned char *sig, const unsigned char *msg, int len)
{
  unsigned char buff[16];
  memcpy(buff,vkey,sizeof(buff));
  for(int i=0; i<len; i+=8) {
    IdeaKS ks;
    idea.SetEncKey(buff,&ks);
    memcpy(buff,buff+8,8);
    idea.Encrypt(msg+i,8,buff+8,&ks,0);
    xxor(&buff[8],8,&buff[8],msg+i);
    }
  buff[8]&=0x7F;
  return (memcmp(sig,buff+8,8)==0);
}

bool cNagra2::DecryptECM(const unsigned char *in, unsigned char *out, const unsigned char *key, int len, const unsigned char *vkey, BIGNUM *m)
{
  int sign=in[0] & 0x80;
  if(rsa.RSA(out,in+1,64,pubExp,m)<=0) {
    PRINTF(L_SYS_CRYPTO,"first RSA failed (ECM)");
    return false;
    }
  out[63]|=sign; // sign adjustment
  if(len>64) memcpy(out+64,in+65,len-64);

  if(in[0]&0x04) {
    unsigned char tmp[8];
    DES_key_schedule ks1, ks2; 
    RotateBytes(tmp,&key[0],8);
    DES_key_sched((DES_cblock *)tmp,&ks1);
    RotateBytes(tmp,&key[8],8);
    DES_key_sched((DES_cblock *)tmp,&ks2);
    memset(tmp,0,sizeof(tmp));
    for(int i=7; i>=0; i--) RotateBytes(out+8*i,8);
    DES_ede2_cbc_encrypt(out,out,len,&ks1,&ks2,(DES_cblock *)tmp,DES_DECRYPT);
    for(int i=7; i>=0; i--) RotateBytes(out+8*i,8);
    } 
  else idea.Decrypt(out,len,key,0); 

  RotateBytes(out,64);
  if(rsa.RSA(out,out,64,pubExp,m,false)<=0) {
    PRINTF(L_SYS_CRYPTO,"second RSA failed (ECM)");
    return false;
    }
  if(vkey && !Signature(vkey,out,out+8,len-8)) {
    PRINTF(L_SYS_CRYPTO,"signature failed (ECM)");
    return false;
    }
  return true;
}

bool cNagra2::DecryptEMM(const unsigned char *in, unsigned char *out, const unsigned char *key, int len, const unsigned char *vkey, BIGNUM *m)
{
  int sign=in[0]&0x80;
  if(rsa.RSA(out,in+1,96,pubExp,m)<=0) {
    PRINTF(L_SYS_CRYPTO,"first RSA failed (EMM)");
    return false;
    }
  out[95]|=sign; // sign adjustment
  cBN exp;
  if(in[0]&0x08) {
    // standard IDEA decrypt
    if(len>96) memcpy(out+96,in+97,len-96);
    idea.Decrypt(out,len,key,0);
    BN_set_word(exp,3);
    }
  else {
    // private RSA key expansion
    CreateRSAPair(key,0,exp,m);
    }
  RotateBytes(out,96);
  if(rsa.RSA(out,out,96,exp,m,false)<=0) {
    PRINTF(L_SYS_CRYPTO,"second RSA failed (EMM)");
    return false;
    }
  if(vkey && !Signature(vkey,out,out+8,len-8)) {
    PRINTF(L_SYS_CRYPTO,"signature failed (EMM)");
    return false;
    }
  return true;
}

// -- cSystemNagra2 ------------------------------------------------------------

class cSystemNagra2 : public cSystem, protected cNagra2 {
private:
  int lastEcmId, lastEmmId;
  cN2Prov *ecmP, *emmP;
public:
  cSystemNagra2(void);
  ~cSystemNagra2();
  virtual bool ProcessECM(const cEcmInfo *ecm, unsigned char *data);
  virtual void ProcessEMM(int pid, int caid, unsigned char *buffer);
  };

cSystemNagra2::cSystemNagra2(void)
:cSystem(SYSTEM_NAME,SYSTEM_PRI)
{
  hasLogger=true;
  lastEcmId=lastEmmId=0; ecmP=emmP=0;
}

cSystemNagra2::~cSystemNagra2()
{
  delete ecmP;
  delete emmP;
}

bool cSystemNagra2::ProcessECM(const cEcmInfo *ecm, unsigned char *data)
{
  int cmdLen=data[4]-5;
  int id=(data[5]*256)+data[6];
  cTimeMs minTime;

  if(id==0x4101) StartLog(ecm,0x1881); // D+ AU
    
  if(cmdLen<64 || SCT_LEN(data)<cmdLen+10) {
    if(doLog) PRINTF(L_SYS_ECM,"bad ECM message msgLen=%d sctLen=%d",cmdLen,SCT_LEN(data));
    return false;
    }

  int keyNr=(data[7]&0x10)>>4;
  cKeySnoop ks(this,'N',id,keyNr);
  cPlainKey *pk;
  cBN m1;
  unsigned char ideaKey[16], vKey[16];
  bool hasVerifyKey=false;
  if(!(pk=keys.FindKey('N',id,MBC('M','1'),-1)))  {
    if(doLog) PRINTF(L_SYS_KEY,"missing %04x M1 key",id);
    return false;
    }
  pk->Get(m1);
  if((pk=keys.FindKey('N',id,'V',sizeof(vKey)))) {
    pk->Get(vKey);
    hasVerifyKey=true;
    }
  else if(doLog && id!=lastEcmId) PRINTF(L_SYS_KEY,"missing %04x V key (non-fatal)",id);
  if(!(pk=keys.FindKey('N',id,keyNr,sizeof(ideaKey)))) return false;
  pk->Get(ideaKey);

  unsigned char buff[256];
  if(!DecryptECM(data+9,buff,ideaKey,cmdLen,hasVerifyKey?vKey:0,m1)) {
    if(doLog) PRINTF(L_SYS_ECM,"decrypt of ECM failed (%04x)",id);
    return false;
    }

  if((!ecmP && id!=lastEcmId) || (ecmP && !ecmP->CanHandle(id))) {
    delete ecmP;
    ecmP=cN2Providers::GetProv(id,N2FLAG_NONE);
    if(ecmP) PRINTF(L_SYS_ECM,"provider %04x capabilities%s%s%s%s",id,
                      ecmP->HasFlags(N2FLAG_MECM)    ?" MECM":"",
                      ecmP->HasFlags(N2FLAG_Bx)      ?" Bx":"",
                      ecmP->HasFlags(N2FLAG_POSTAU)  ?" POSTPROCAU":"",
                      ecmP->HasFlags(N2FLAG_INV)     ?" INVCW":"");
    }
  lastEcmId=id;

  int l=0, mecmAlgo=0;
  LBSTARTF(L_SYS_ECM);
  bool contFail=false;
  for(int i=16; i<cmdLen-10 && l!=3; ) {
    switch(buff[i]) {
      case 0x10:
      case 0x11:
        if(buff[i+1]==0x09) {
          int s=(~buff[i])&1;
          mecmAlgo=buff[i+2]&0x60;
          memcpy(cw+(s<<3),&buff[i+3],8);
          i+=11; l|=(s+1);
          }
        else {
          PRINTF(L_SYS_ECM,"bad length %d in CW nano %02x",buff[i+1],buff[i]);
          i++;
          }
        break;
      case 0x00:
        i+=2; break;
      case 0x13 ... 0x17:
        i+=4; break;
      case 0x30 ... 0x36:
      case 0xB0:
        i+=buff[i+1]+2;
        break;
      default:
        if(!contFail) LBPUT("unknown ECM nano");
        LBPUT(" %02x",buff[i]);
        contFail=true;
        i++;
        continue;
      }
    LBFLUSH(); contFail=false;
    }
  LBEND();
  if(l!=3) return false;
  if(mecmAlgo>0) {
    if(ecmP && ecmP->HasFlags(N2FLAG_MECM)) {
      if(!ecmP->MECM(buff[15],mecmAlgo,cw)) return false;
      }
    else { PRINTF(L_SYS_ECM,"MECM for provider %04x not supported",id); return false; }
    }
  if(ecmP) ecmP->SwapCW(cw);
  ks.OK(pk);

  int i=minEcmTime-minTime.Elapsed();
  if(i>0) cCondWait::SleepMs(i);
  return true;
}

void cSystemNagra2::ProcessEMM(int pid, int caid, unsigned char *buffer)
{
  int cmdLen=buffer[9]-5;
  int id=buffer[10]*256+buffer[11];

  if(cmdLen<96 || SCT_LEN(buffer)<cmdLen+15) {
    PRINTF(L_SYS_EMM,"bad EMM message msgLen=%d sctLen=%d",cmdLen,SCT_LEN(buffer));
    return;
    }

  int keyset=(buffer[12]&0x03);
  int sel=(buffer[12]&0x10)<<2;
  int rsasel=(id==0x4101 || id==0x4001) ? 0:sel; // D+ hack
  int sigsel=(buffer[13]&0x80)>>1;
  cPlainKey *pk;
  cBN n;
  unsigned char ideaKey[24], vKey[16];
  bool hasVerifyKey=false;
  if(!(pk=keys.FindKey('N',id,MBC(N2_MAGIC,keyset+0x10+rsasel),96)))  {
    PRINTF(L_SYS_EMM,"missing %04x NN %.02X RSA key (96 bytes)",id,keyset+0x10+rsasel);
    return;
    }
  pk->Get(n);
  if((pk=keys.FindKey('N',id,MBC(N2_MAGIC,0x03+sigsel),sizeof(vKey)))) {
    pk->Get(vKey);
    hasVerifyKey=true;
    }
  else if(id!=lastEmmId) PRINTF(L_SYS_EMM,"missing %04x NN %.02X signature key (non-fatal)",id,0x03+sigsel);
  if(!(pk=keys.FindKey('N',id,MBC(N2_MAGIC,keyset),24))) {
    if(!(pk=keys.FindKey('N',id,MBC(N2_MAGIC,keyset+sel),16))) {
      PRINTF(L_SYS_EMM,"missing %04x NN %.02x IDEA key (24 or 16 bytes)",id,keyset+sel);
      return;
      }
    memset(ideaKey+16,0,8);
    }
  pk->Get(ideaKey);

  unsigned char emmdata[256];
  if(!DecryptEMM(buffer+14,emmdata,ideaKey,cmdLen,hasVerifyKey?vKey:0,n)) {
    PRINTF(L_SYS_EMM,"decrypt of EMM failed (%04x)",id);
    return;
    }
  if((!emmP && id!=lastEmmId) || (emmP && !emmP->CanHandle(id))) {
    delete emmP;
    emmP=cN2Providers::GetProv(id,N2FLAG_NONE);
    if(emmP) PRINTF(L_SYS_EMM,"provider %04x capabilities%s%s%s%s",id,
                      emmP->HasFlags(N2FLAG_MECM)    ?" MECM":"",
                      emmP->HasFlags(N2FLAG_Bx)      ?" Bx":"",
                      emmP->HasFlags(N2FLAG_POSTAU)  ?" POSTPROCAU":"",
                      emmP->HasFlags(N2FLAG_INV)     ?" INVCW":"");
    }
  lastEmmId=id;

  HEXDUMP(L_SYS_RAWEMM,emmdata,cmdLen,"Nagra2 RAWEMM");
  id=(emmdata[8]<<8)+emmdata[9];
  LBSTARTF(L_SYS_EMM);
  bool contFail=false;
  for(int i=8+2+4+4; i<cmdLen-22; ) {
    switch(emmdata[i]) {
      case 0x42: // plain Key update
        if(emmdata[i+2]==0x10 && (emmdata[i+3]&0xBF)==0x06 &&
           (emmdata[i+4]&0xF8)==0x08 && emmdata[i+5]==0x00 && emmdata[i+6]==0x10) {
          if(!emmP || emmP->PostProcAU(id,&emmdata[i])) {
            FoundKey();
            if(keys.NewKey('N',id,(emmdata[i+3]&0x40)>>6,&emmdata[i+7],16)) NewKey();
            cLoaders::SaveCache();
            }
          }
        i+=23;
        break;
      case 0xE0: // DN key update
        if(emmdata[i+1]==0x25) {
          FoundKey();
          if(keys.NewKey('N',id,(emmdata[i+16]&0x40)>>6,&emmdata[i+23],16)) NewKey();
          cLoaders::SaveCache();
          }
        i+=39;
        break;
      case 0x83: // change data prov. id
        id=(emmdata[i+1]<<8)|emmdata[i+2];
        i+=3;
        break;
      case 0xA4: // conditional (always no match assumed for now)
        i+=emmdata[i+1]+2+4;
        break;
      case 0xA6:
        i+=15;
        break;
      case 0xAE:
        i+=11;
        break;
      case 0x13 ... 0x17: // Date
        i+=4;
        break;
      case 0xB0 ... 0xBF: // Update with ROM CPU code
        {
        int bx=emmdata[i]&15;
        if(!emmP || !emmP->HasFlags(N2FLAG_Bx)) {
          PRINTF(L_SYS_EMM,"B%X for provider %04x not supported",bx,id);
          i=cmdLen;
          break;
          }
        int r;
        if((r=emmP->ProcessBx(emmdata,cmdLen,i+1))>0)
          i+=r;
        else {
          PRINTF(L_SYS_EMM,"B%X executing failed for %04x",bx,id);
          i=cmdLen;
          }
        break;
        }
      case 0xE3: // Eeprom update
        i+=emmdata[i+4]+4;
        break;
      case 0xE1:
      case 0xE2:
      case 0x00: // end of processing
        i=cmdLen;
        break;
      default:
        if(!contFail) LBPUT("unknown EMM nano");
        LBPUT(" %02x",emmdata[i]);
        contFail=true;
        i++;
        continue;
      }
    LBFLUSH(); contFail=false;
    }
  LBEND();
}

// -- cSystemLinkNagra2 --------------------------------------------------------

static const tI18nPhrase Phrases2[] = {
  { "Nagra2: AUXserver hostname",
    "Nagra2: AUXserver Hostname",
    "",
    "",
    "",
    "",
    "",
    "",
    "Nagra2: AUX-palvelimen osoite",
    "",
    "",
    "",
    "",
  },
  { "Nagra2: AUXserver port",
    "Nagra2: AUXserver Port",
    "",
    "",
    "",
    "",
    "",
    "",
    "Nagra2: AUX-palvelimen portti",
    "",
    "",
    "",
    "",
  },
  { "Nagra2: AUXserver password",
    "Nagra2: AUXserver Passwort",
    "",
    "",
    "",
    "",
    "",
    "",
    "Nagra2: AUX-palvelimen salasana",
    "",
    "",
    "",
    "",
  },
  { NULL }
  };

class cSystemLinkNagra2 : public cSystemLink {
public:
  cSystemLinkNagra2(void);
  virtual bool CanHandle(unsigned short SysId);
  virtual cSystem *Create(void) { return new cSystemNagra2; }
  };

static cSystemLinkNagra2 staticInitN2;

cSystemLinkNagra2::cSystemLinkNagra2(void)
:cSystemLink(SYSTEM_NAME,SYSTEM_PRI)
{
#ifdef HAS_AUXSRV
  static const char allowed_chars[] = "0123456789abcdefghijklmnopqrstuvwxyz-.";
  opts=new cOpts(SYSTEM_NAME,3);
  opts->Add(new cOptStr("AuxServerAddr","Nagra2: AUXserver hostname",auxAddr,sizeof(auxAddr),allowed_chars));
  opts->Add(new cOptInt("AuxServerPort","Nagra2: AUXserver port",&auxPort,0,65535));
  opts->Add(new cOptStr("AuxServerPass","Nagra2: AUXserver password",auxPassword,sizeof(auxPassword),allowed_chars));
  Feature.AddPhrases(Phrases2);
#endif
  Feature.NeedsKeyFile();
}

bool cSystemLinkNagra2::CanHandle(unsigned short SysId)
{
  return ((SysId&SYSTEM_MASK)==SYSTEM_NAGRA && (SysId&0xFF)>0) ||
          SysId==SYSTEM_NAGRA_BEV;
}

#endif //TESTER
