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
  virtual bool RomInit(void) { return true; }
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
    if(!AddMapper(new cMapRom(0x8000,buff,0x14000),0x8000,romv>=110?0x8000:0x4000,0x02)) return false;

    snprintf(buff,sizeof(buff),"EEP%02X_%d.bin",(id>>8)&0xFF|0x01,romv);
    // Eeprom00 0x00:0x3000-0x37ff OTP 0x80
    //XXX if(!AddMapper(new cMapRom(0x3000,buff,0x0000),0x3000,0x0800,0x00)) return false;
    if(!AddMapper(new cMapEeprom(0x3000,buff,128,0x0000),0x3000,0x0800,0x00)) return false;
    // Eeprom80 0x80:0x8000-0xbfff
    //XXX if(!AddMapper(new cMapRom(0x8000,buff,0x0800),0x8000,0x4000,0x80)) return false;
    if(!AddMapper(new cMapEeprom(0x8000,buff,  0,0x0800),0x8000,0x4000,0x80)) return false;
    initDone=RomInit();
    }
  return initDone;
}

// -- cMapCore -----------------------------------------------------------------

#define SETSIZE     0x02
#define IMPORT_J    0x03
#define IMPORT_A    0x04
#define IMPORT_B    0x05
#define IMPORT_C    0x06
#define IMPORT_D    0x07
#define IMPORT_LAST 0x08
#define EXPORT_J    0x09
#define EXPORT_A    0x0A
#define EXPORT_B    0x0B
#define EXPORT_C    0x0C
#define EXPORT_D    0x0D
#define EXPORT_LAST 0x0E
#define SWAP_A      0x0F
#define SWAP_B      0x10
#define SWAP_C      0x11
#define SWAP_D      0x12
#define CLEAR_A     0x13
#define CLEAR_B     0x14
#define CLEAR_C     0x15
#define CLEAR_D     0x16
#define COPY_A_B    0x17
#define COPY_B_A    0x18
#define COPY_A_C    0x19
#define COPY_C_A    0x1A
#define COPY_C_D    0x1B
#define COPY_D_C    0x1C

class cMapCore {
private:
  int last;
  cBN *regs[5];
  cBN x, y, s;
protected:
  int wordsize;
  cBN A, B, C, D, J, I;
  cBN Px, Py, Pz,Qx, Qy, Qz; // 0x00,0x20,0x40,0x60,0x80,0x180
  cBN sA0, sC0, sE0, s100, s120, s140, s160;
  cBNctx ctx;
  SHA_CTX sctx;
  // stateless
  void MakeJ0(BIGNUM *j, BIGNUM *d);
  void ModAdd(BIGNUM *r, BIGNUM *a, BIGNUM *b, BIGNUM *d);
  void ModSub(BIGNUM *r, BIGNUM *d, BIGNUM *b);
  void MonMul(BIGNUM *o, BIGNUM *a, BIGNUM *b, BIGNUM *c, BIGNUM *d, BIGNUM *j);
  // statefull
  void MonInit(int bits=0);
  void MonMul(BIGNUM *o, BIGNUM *a, BIGNUM *b) { MonMul(o,a,b,C,D,J); }
  void MonExpNeg(void);
  // ECC
  void DoubleP(int temp);
  void AddP(int temp);
  void ToProjective(int set, BIGNUM *x, BIGNUM *y);
  void ToAffine(void);
  void CurveInit(BIGNUM *a);
  //
  bool DoMap(int f, unsigned char *data=0, int l=0);
public:
  cMapCore(void);
  };

cMapCore::cMapCore(void)
{
  wordsize=4; last=1;
  regs[0]=&J; regs[1]=&A; regs[2]=&B; regs[3]=&C; regs[4]=&D;
}

void cMapCore::ModAdd(BIGNUM *r, BIGNUM *a, BIGNUM *b, BIGNUM *d)
{
  BN_add(r,a,b);
  if(BN_cmp(r,d)>=0) BN_sub(r,r,d);
  BN_mask_bits(r,wordsize<<6);
}

void cMapCore::ModSub(BIGNUM *r, BIGNUM *d, BIGNUM *b)
{
  cBN p;
  BN_set_bit(p,wordsize<<6);
  BN_mod_sub(r,d,b,p,ctx);
  BN_mask_bits(r,wordsize<<6);
}

void cMapCore::MakeJ0(BIGNUM *j, BIGNUM *d)
{
#if OPENSSL_VERSION_NUMBER < 0x0090700fL
#error BN_mod_inverse is probably buggy in your openssl version
#endif
  BN_zero(x);
  BN_sub(j,x,d);
  BN_set_bit(j,0);
  BN_set_bit(x,64);
  BN_mod_inverse(j,j,x,ctx);
}

void cMapCore::MonMul(BIGNUM *o, BIGNUM *a, BIGNUM *b, BIGNUM *c, BIGNUM *d, BIGNUM *j)
{
  int words=(BN_num_bytes(d)+7)>>3;
  BN_zero(s);
  for(int i=0; i<words;) {
    BN_rshift(x,a,(i++)<<6);
    BN_mask_bits(x,64);
    BN_mul(x,x,b,ctx);
    BN_add(s,s,x);

    BN_copy(x,s);
    BN_mask_bits(x,64);
    BN_mul(x,x,j,ctx);
    if(i==words) {
      BN_lshift(y,x,64);
      BN_add(y,y,x);
      // Low
      BN_rshift(c,y,2);
      BN_add(c,c,s);
      BN_rshift(c,c,52);
      BN_mask_bits(c,12);
      }

    BN_mask_bits(x,64);
    BN_mul(x,x,d,ctx);
    BN_add(s,s,x);
    if(i==words) {
      // High
      BN_lshift(y,s,12);
      BN_add(c,c,y);
      BN_mask_bits(c,wordsize<<6);
      }

    BN_rshift(s,s,64);
    if(BN_cmp(s,d)==1) {
      BN_copy(x,s);
      BN_sub(s,x,d);
      }
    }
  BN_copy(o,s);
}

void cMapCore::MonInit(int bits)
{
  // Calculate J0 & H montgomery elements in J and B
  MakeJ0(J,D);
  BN_zero(I);
  BN_set_bit(I,bits ? bits : 68*wordsize);
  BN_mod(B,I,D,ctx);
  for(int i=0; i<4; i++) MonMul(B,B,B);
}

void cMapCore::MonExpNeg(void)
{
  if(BN_is_zero(D)) { BN_set_word(A,1); return; }
  cBN e;
  BN_copy(e,D);
  BN_mask_bits(e,8);		// check LSB
  unsigned int n=BN_get_word(e);
  BN_copy(e,D);
  if(n) BN_sub_word(e,0x02);	// N -2
  else  BN_add_word(e,0xFE);	// N + 254 ('carryless' -2)
  BN_copy(A,B);
  for(int i=BN_num_bits(e)-2; i>-1; i--) {
    MonMul(B,B,B);
    if(BN_is_bit_set(e,i)) MonMul(B,A,B);
    }
  BN_set_word(A,1);
  MonMul(B,A,B);
}

void cMapCore::DoubleP(int temp)
{
  ModAdd(B,Py,Py,D);
  MonMul(sC0,Pz,B);
  MonMul(B,B,B);
  MonMul(B,Px,B);
  ModSub(sA0,D,B);
  MonMul(B,Px,Px);
  BN_copy(A,B);
  ModAdd(B,B,B,D);
  ModAdd(A,B,A,D);
  MonMul(B,Pz,Pz);
  MonMul(B,B,B);
  MonMul(B,s160,B);
  ModAdd(B,B,A,D);
  BN_copy(A,B);
  MonMul(B,B,B);
  BN_copy(C,sA0);
  ModAdd(B,C,B,D);
  ModAdd(B,C,B,D);
  if(temp==0) BN_copy(Px,B); else BN_copy(sA0,B);
  ModAdd(B,C,B,D);
  MonMul(B,A,B);
  BN_copy(A,B);
  MonMul(B,Py,Py);
  ModAdd(B,B,B,D);
  MonMul(B,B,B);
  ModAdd(B,B,B,D);
  ModAdd(B,B,A,D);
  ModSub(B,D,B);
  if(temp==0) { BN_copy(Py,B); BN_copy(Pz,sC0); }
  else BN_copy(sA0,sC0);
}

void cMapCore::AddP(int temp)
{
  MonMul(B,Pz,Pz);
  MonMul(sA0,Pz,B);
  MonMul(B,Qx,B);
  BN_copy(A,B);
  ModSub(B,D,B);
  ModAdd(B,Px,B,D);
  BN_copy(sC0,B);
  MonMul(I,Pz,B);
  if(temp==0) BN_copy(Pz,I); else BN_copy(sA0,I);
  MonMul(B,B,B);
  BN_copy(sE0,B);
  ModAdd(A,Px,A,D);
  MonMul(A,A,B);
  BN_copy(s100,A);
  MonMul(B,sA0,Qy);
  BN_copy(sA0,B);
  ModSub(B,D,B);
  ModAdd(B,Py,B,D);
  BN_copy(s120,B);
  MonMul(B,B,B);
  BN_swap(A,B);
  ModSub(B,D,B);
  ModAdd(B,A,B,D);
  if(temp==0) BN_copy(Px,B); else BN_copy(sA0,B);
  ModAdd(B,B,B,D);
  ModSub(B,D,B);
  ModAdd(B,B,s100,D);
  ModAdd(A,sA0,Py,D);
  MonMul(sA0,s120,B);
  MonMul(B,sE0,sC0);
  MonMul(B,A,B);
  ModSub(B,D,B);
  ModAdd(B,B,sA0,D);
  MonMul(B,s140,B);
  if(temp==0) BN_copy(Py,B); else BN_copy(sA0,B);
}

void cMapCore::ToProjective(int set, BIGNUM *x, BIGNUM *y)
{
  if(set==0) {
    BN_set_word(I,1); MonMul(Pz,I,B);
    BN_copy(Qz,Pz);
    MonMul(Px,x,B);
    MonMul(Py,y,B);
    }
  else {
    MonMul(Qx,x,B);
    MonMul(Qy,y,B);
    }
}

void cMapCore::ToAffine(void)
{
  BN_set_word(I,1);
  MonMul(B,Pz,Pz); MonMul(B,Pz,B); MonMul(B,I,B);
  MonExpNeg();
  BN_set_word(I,1);
  MonMul(A,Py,B); MonMul(Py,I,A);
  MonMul(B,Pz,B); MonMul(B,Px,B); MonMul(Px,I,B);
}

void cMapCore::CurveInit(BIGNUM *a)
{
  BN_zero(Px); BN_zero(Py); BN_zero(Pz); BN_zero(Qx); BN_zero(Qy); BN_zero(Qz);
  BN_zero(sA0); BN_zero(sC0); BN_zero(sE0); BN_zero(s100);
  BN_zero(s120); BN_zero(s140); BN_zero(s160);
  MonInit();
  BN_copy(A,B);
  BN_copy(I,D);
  BN_add_word(I,1);
  BN_rshift(I,I,1);
  MonMul(s140,I,B);
  MonMul(s160,B,a);
}

bool cMapCore::DoMap(int f, unsigned char *data, int l)
{
  int dl=(l?l:wordsize)<<3;
  switch(f) {
    case SETSIZE:
      wordsize=l; break;

    case IMPORT_J:    
    case IMPORT_A:    
    case IMPORT_B:    
    case IMPORT_C:    
    case IMPORT_D:
      last=f-IMPORT_J;
      // fall through
    case IMPORT_LAST:
      regs[last]->GetLE(data,last>0?dl:8);
      break;

    case EXPORT_J:
    case EXPORT_A:
    case EXPORT_B:
    case EXPORT_C:
    case EXPORT_D:
      last=f-EXPORT_J;
      // fall through
    case EXPORT_LAST:
      regs[last]->PutLE(data,last>0?dl:8);
      break;

    case SWAP_A:
    case SWAP_B:
    case SWAP_C:
    case SWAP_D:
      last=f-SWAP_A+1;
      x.GetLE(data,dl);
      regs[last]->PutLE(data,dl);
      BN_copy(*regs[last],x);
      break;

    case CLEAR_A:
    case CLEAR_B:
    case CLEAR_C:
    case CLEAR_D:
      last=f-CLEAR_A+1; BN_zero(*regs[last]);
      break;

    case COPY_A_B:
      last=2; BN_copy(B,A); break;
    case COPY_B_A:
      last=1; BN_copy(A,B); break;
    case COPY_A_C:
      last=3; BN_copy(C,A); break;
    case COPY_C_A:
      last=1; BN_copy(A,C); break;
    case COPY_C_D:
      last=4; BN_copy(D,C); break;
    case COPY_D_C:
      last=3; BN_copy(C,D); break;

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

#define N2FLAG_NONE     0
#define N2FLAG_MECM     1
#define N2FLAG_Bx       2
#define N2FLAG_POSTAU   4
#define N2FLAG_Ex       8
#define N2FLAG_INV      128

class cN2Prov {
private:
  unsigned char seed[32], cwkey[8];
  bool keyValid;
protected:
  int id, flags, seedSize;
  cIDEA idea;
  //
  virtual bool Algo(int algo, unsigned char *hd, const unsigned char *ed, unsigned char *hw) { return false; }
  virtual bool NeedsCwSwap(void) { return false; }
  void ExpandInput(unsigned char *hw);
public:
  cN2Prov(int Id, int Flags);
  virtual ~cN2Prov() {}
  bool MECM(unsigned char in15, int algo, const unsigned char *ed, unsigned char *cw);
  void SwapCW(unsigned char *cw);
  virtual int ProcessBx(unsigned char *data, int len, int pos) { return -1; }
  virtual int ProcessEx(unsigned char *data, int len, int pos) { return -1; }
  virtual bool PostProcAU(int id, unsigned char *data) { return true; }
  bool CanHandle(int Id) { return MATCH_ID(Id,id); }
  bool HasFlags(int Flags) { return (flags&Flags)==Flags; }
  void PrintCaps(int c);
  };

cN2Prov::cN2Prov(int Id, int Flags)
{
  keyValid=false; id=Id|0x100; flags=Flags; seedSize=5;
}

void cN2Prov::PrintCaps(int c)
{
  PRINTF(c,"provider %04x capabilities%s%s%s%s%s",id,
           HasFlags(N2FLAG_MECM)    ?" MECM":"",
           HasFlags(N2FLAG_Bx)      ?" Bx":"",
           HasFlags(N2FLAG_Ex)      ?" Ex":"",
           HasFlags(N2FLAG_POSTAU)  ?" POSTPROCAU":"",
           HasFlags(N2FLAG_INV)     ?" INVCW":"");
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

bool cN2Prov::MECM(unsigned char in15, int algo, const unsigned char *ed, unsigned char *cw)
{
  unsigned char hd[32], hw[128+64], buf[20];
  hd[0]=in15&0x7F;
  hd[1]=cw[14];
  hd[2]=cw[15];
  hd[3]=cw[6];
  hd[4]=cw[7];

  if(keyValid && !memcmp(seed,hd,seedSize)) {	// key cached
    memcpy(buf,cwkey,8);
    }
  else {				// key not cached
    memset(hw,0,sizeof(hw));
    if(!Algo(algo,hd,ed,hw)) return false;
    memcpy(&hw[128],hw,64);
    RotateBytes(&hw[64],128);
    SHA1(&hw[64],128,buf);
    RotateBytes(buf,20);

    memcpy(seed,hd,seedSize);
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

class cN2Providers;

class cN2ProvLink {
friend class cN2Providers;
private:
  cN2ProvLink *next;
protected:
  int id, flags;
  //
  virtual cN2Prov *Create(void)=0;
  bool CanHandle(int Id) { return MATCH_ID(Id,id); }
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

static int dropEMMs=1;

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
#define NA_SOURCE_START 0x8267 // cSource::FromString("S61.5W");
#define NA_SOURCE_END   0x85c8 // cSource::FromString("S148W");
  unsigned char odata[15];
  memcpy(odata,data,sizeof(odata));
  if(ecm->source>=NA_SOURCE_START && ecm->source<=NA_SOURCE_END) {
    if(ecm->caId==0x1234) data[5]=0x09;		// NA rev 248 morph
    else data[5]=0x01;				// I _HATE_ this provider
    data[6]&=0x1F;				// specific stuff :(
    data[7]=data[7]&0x10|0x86;
    data[8]=0;
    data[9]=data[9]&0x80|0x08;
    }

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
    if(ecmP) ecmP->PrintCaps(L_SYS_ECM);
    }
  lastEcmId=id;

  HEXDUMP(L_SYS_RAWECM,buff,cmdLen,"Nagra2 RAWECM");
  int l=0, mecmAlgo=0;
  LBSTARTF(L_SYS_ECM);
  bool contFail=false;
  for(int i=(buff[14]&0x10)?16:20; i<cmdLen-10 && l!=3; ) {
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
      case 0x30:
      case 0x31:
      case 0x32:
      case 0x33:
      case 0x34:
      case 0x35:
      case 0x36:
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
      if(!ecmP->MECM(buff[15],mecmAlgo,odata,cw)) return false;
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

  if(buffer[0]==0x83 && dropEMMs) return; // skip EMM-S if disabled
  if(cmdLen<96 || SCT_LEN(buffer)<cmdLen+15) {
    PRINTF(L_SYS_EMM,"bad EMM message msgLen=%d sctLen=%d",cmdLen,SCT_LEN(buffer));
    return;
    }

  int keyset=(buffer[12]&0x03);
  int sel=(buffer[12]&0x10)<<2;
  int rsasel=(MATCH_ID(id,0x4101) || MATCH_ID(id,0x7101)) ? 0:sel; // D+ hack
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
    if(emmP) emmP->PrintCaps(L_SYS_EMM);
    }
  lastEmmId=id;

  HEXDUMP(L_SYS_RAWEMM,emmdata,cmdLen,"Nagra2 RAWEMM");
  id=(emmdata[8]<<8)+emmdata[9];
  LBSTARTF(L_SYS_EMM);
  bool contFail=false;
  for(int i=8+2+4+4; i<cmdLen-22; ) {
    switch(emmdata[i]) {
      case 0x42: // plain Key update
        if((((emmdata[i+3]|0xF3)+1)&0xFF) != 0) {
          int len=emmdata[i+2];
          int off=emmdata[i+5];
          int ulen=emmdata[i+6];
          if(len>0 && ulen>0 && off+ulen<=len) {
            int ks=emmdata[i+3], kn;
            if(ks==0x06 || ks==0x46) kn=(ks>>6)&1; else kn=MBC(N2_MAGIC,ks);
            unsigned char key[256];
            memset(key,0,sizeof(key));
            if((pk=keys.FindKey('N',id,kn,len))) {
              if(cPlainKeyNagra::IsBNKey(kn)) { pk->Get(n); n.PutLE(key,len); }
              else pk->Get(key);
              }
            bool ok=false;
            if((emmdata[i+1]&0x7F)==0) ok=true;
            else {
              if(emmP && emmP->HasFlags(N2FLAG_POSTAU)) {
                if(emmP->PostProcAU(id,&emmdata[i])) ok=true;
                }
              else PRINTF(L_SYS_EMM,"POSTAU for provider %04x not supported",id);
              }
            if(ok) {
              memcpy(&key[off],&emmdata[i+7],ulen);
              FoundKey();
              if(cPlainKeyNagra::IsBNKey(kn)) {
                n.GetLE(key,len);
                if(keys.NewKey('N',id,kn,n,len)) NewKey();
                }
              else {
                if(keys.NewKey('N',id,kn,key,len)) NewKey();
                }
              cLoaders::SaveCache();
              }
            i+=ulen;
            }
          else PRINTF(L_SYS_EMM,"nano42 key size mismatch len=%d off=%d ulen=%d",len,off,ulen);
          }
        else PRINTF(L_SYS_EMM,"nano42 0xf3 status exceeded");
        i+=7;
        break;
      case 0xE0: // DN key update
        if(emmdata[i+1]==0x25) {
          FoundKey();
          if(keys.NewKey('N',id,(emmdata[i+16]&0x40)>>6,&emmdata[i+23],16)) NewKey();
          cLoaders::SaveCache();
          }
        i+=emmdata[i+1]+2;
        break;
      case 0x83: // change data prov. id
        id=(emmdata[i+1]<<8)|emmdata[i+2];
        i+=3;
        break;
      case 0xB0: case 0xB1: case 0xB2: case 0xB3: // Update with ROM CPU code
      case 0xB4: case 0xB5: case 0xB6: case 0xB7:
      case 0xB8: case 0xB9: case 0xBA: case 0xBB:
      case 0xBC: case 0xBD: case 0xBE: case 0xBF:
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
      case 0xA4: i+=emmdata[i+1]+2+4; break;	// conditional (always no match assumed for now)
      case 0xA6: i+=15; break;
      case 0xAA: i+=emmdata[i+1]+5; break;
      case 0xAD: i+=emmdata[i+1]+2; break;
      case 0xA2:
      case 0xAE: i+=11;break;
      case 0x12: i+=emmdata[i+1]+2; break;      // create tier
      case 0x20: i+=19; break;                  // modify tier
      case 0x9F: i+=6; break;
      case 0xE3:                                // Eeprom update
        {
        int ex=emmdata[i]&15;
        if(!emmP || !emmP->HasFlags(N2FLAG_Ex)) {
          i+=emmdata[i+4]+5;
          PRINTF(L_SYS_EMM,"E%X for provider %04x not supported",ex,id);
          break;
          }
        int r;
        if((r=emmP->ProcessEx(emmdata,cmdLen,i+1))>0)
          i+=r;
        else {
          PRINTF(L_SYS_EMM,"E%X executing failed for %04x",ex,id);
          i=cmdLen;
          }
        break;
        }
      case 0xE1:
      case 0xE2:
      case 0x00: i=cmdLen; break;		// end of processing
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
  opts=new cOpts(SYSTEM_NAME,5);
  opts->Add(new cOptBool("DropEMMS",trNOOP("Nagra2: drop EMM-S packets"),&dropEMMs));
#ifdef HAS_AUXSRV
  static const char allowed_chars[] = "0123456789abcdefghijklmnopqrstuvwxyz-.";
  opts->Add(new cOptBool("AuxServerEnable",trNOOP("Nagra2: Enable AUXserver"),&auxEnabled));
  opts->Add(new cOptStr("AuxServerAddr",trNOOP("Nagra2: AUXserver hostname"),auxAddr,sizeof(auxAddr),allowed_chars));
  opts->Add(new cOptInt("AuxServerPort",trNOOP("Nagra2: AUXserver port"),&auxPort,0,65535));
  opts->Add(new cOptStr("AuxServerPass",trNOOP("Nagra2: AUXserver password"),auxPassword,sizeof(auxPassword),allowed_chars));
#endif
  Feature.NeedsKeyFile();
}

bool cSystemLinkNagra2::CanHandle(unsigned short SysId)
{
  return ((SysId&SYSTEM_MASK)==SYSTEM_NAGRA && (SysId&0xFF)>0) ||
          SysId==SYSTEM_NAGRA_BEV;
}

#endif //TESTER
