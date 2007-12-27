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

#ifndef __NAGRA_NAGRA2_H
#define __NAGRA_NAGRA2_H

#include "data.h"
#include "crypto.h"
#include "misc.h"
#include "helper.h"

#include <openssl/des.h>
#include <openssl/sha.h>
#include "openssl-compat.h"

#include "nagra-def.h"
#include "cpu.h"
#include "log-nagra.h"

// ----------------------------------------------------------------

#define MATCH_ID(x,y) ((((x)^(y))&~0x107)==0)

// ----------------------------------------------------------------

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

// ----------------------------------------------------------------

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

// ----------------------------------------------------------------

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
  virtual int RunEmu(unsigned char *data, int len, unsigned short load, unsigned short run, unsigned short stop, unsigned short fetch, int fetch_len) { return -1; }
  bool CanHandle(int Id) { return MATCH_ID(Id,id); }
  bool HasFlags(int Flags) { return (flags&Flags)==Flags; }
  void PrintCaps(int c);
  };

// ----------------------------------------------------------------

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

template<class PROV, int ID, int FLAGS> class cN2ProvLinkReg : public cN2ProvLink {
public:
  cN2ProvLinkReg(void):cN2ProvLink(ID,FLAGS) {}
  virtual cN2Prov *Create(void) { return new PROV(id,flags); }
  };

// ----------------------------------------------------------------

class cN2Providers {
friend class cN2ProvLink;
private:
  static cN2ProvLink *first;
  //
  static void Register(cN2ProvLink *plink);
public:
  static cN2Prov *GetProv(int Id, int Flags);
  };

#endif
