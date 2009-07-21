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

#define HAS_AUXSRV

#ifdef HAS_AUXSRV
extern int auxEnabled;
extern int auxPort;
extern char auxAddr[80];
extern char auxPassword[250];
#endif

// ----------------------------------------------------------------

class cMapReg  {
protected:
  enum { DEF_WORDSIZE = 4, DEF_MAXWORDSIZE = 17 };
  cBN fullreg, reg, tmp;
  int wordsize, maxwordsize, *defwordsize;
  int DefWordSize() const { return defwordsize ? *defwordsize : DEF_WORDSIZE; }
  int OpWordSize(int wsize) const { int sz = wsize>0 ? wsize : DefWordSize(); return sz>maxwordsize ? maxwordsize : sz; }
  void ClearReg(int wsize);
  void ClearFullReg(int wsize);
  void SetTmp(BIGNUM* val, int wsize);
protected:
  void SetMaxWordSize(int max) { maxwordsize = max; }
  void SetDefWordSize(int* _defwordsize) { defwordsize = _defwordsize; }
public:
  cMapReg(int* defwordsize=NULL, int maxwordsize=DEF_MAXWORDSIZE);
  operator BIGNUM* () { return Value(); }
  BIGNUM *operator->() { return Value(); }
  BIGNUM* Value(int wsize=0, bool mask=false);
  int OpWordSize() const { return wordsize; }

  void Commit(int wsize=-1, int resync=-1);
  void Reload(int wsize=0);
  void GetLE(const unsigned char *in, int n=0);
  void PutLE(unsigned char *out, int n=0);
  void Set(BIGNUM* val, int wsize);
  void Clear(int wsize);
};



// ----------------------------------------------------------------

#define WS_START(x) { int __oldws=wordsize; wordsize=(x);
#define WS_END()    wordsize=__oldws; }

class cMapMath {
private:
  cBN x, y, s;
protected:
  int wordsize;
  cMapReg A, B, C, D, J, I;
  cBNctx ctx;
  SHA_CTX sctx;
  // stateless
#if 0
  void ClearReg(BIGNUM* reg, int size=0);
  void SetReg(BIGNUM* reg, BIGNUM* in, int size=0);
  void SetReg(BIGNUM* reg, unsigned int val, int size=0);
  void SetRestoreReg(BIGNUM* reg, BIGNUM* restorereg, int size=0);
  void LoadReg(BIGNUM* out, BIGNUM* reg, int size=0);
  void LoadSaveReg(BIGNUM* reg, BIGNUM* savereg, int size=0);
#endif

  void MakeJ0(BIGNUM *j, BIGNUM *d, BIGNUM *c=NULL, int bits=64);
  void MakeJ() { MakeJ0(J, D, C); }
  bool ModAdd(BIGNUM *r, BIGNUM *a, BIGNUM *b, BIGNUM *d);
  bool ModSub(BIGNUM *r, BIGNUM *d, BIGNUM *b);
  void MonMul0(BIGNUM *o, BIGNUM *a, BIGNUM *b, BIGNUM *c, BIGNUM *d, BIGNUM *j, int words=0);
  void MonMulFin(BIGNUM *o, BIGNUM *d);
  void MonMul(BIGNUM *o, BIGNUM *a, BIGNUM *b, BIGNUM *c, BIGNUM *d, BIGNUM *j, int words=0);
  // statefull
  void MonMul(BIGNUM *o, BIGNUM *a, BIGNUM *b, int words=0) { MonMul(o, a, b, C, D, J, words); }
public:
  cMapMath(void);
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

class cMapCore : public cMapMath {
private:
  int mapid, last;
  cBN x, y, s, j;
  cBN e;
  cMapReg *regs[5];
  c6805* cpu;
  bool interruptible, interrupted;
  //
  bool MapGeneric(int f, unsigned char *data, int l);
protected:
  unsigned int cycles;
  cBN Px, Py, Pz,Qx, Qy, Qz; // 0x00,0x20,0x40,0x60,0x80,0x180
  cBN sA0, sC0, sE0, s100, s120, s140, s160;
  cBN H, R;
  void ImportReg(unsigned char reg, const unsigned char *data, int l=0);
  void ExportReg(unsigned char reg, unsigned char *data, int l=0);
  void SetWordSize(int l) { wordsize=l; }
  // statefull
  void MonInit(int bits=0);
  void MonExp(BIGNUM *scalar);
  void MonExpNeg(void);
  // ECC
  void DoubleP(int temp);
  void AddP(int temp);
  void ToProjective(int set, BIGNUM *x, BIGNUM *y);
  void ToAffine(void);
  void CurveInit(BIGNUM *a);
  //
  int GetOpSize(int l);
  void DoMap(int f, unsigned char *data=0, int l=0);
  virtual bool Map(int f, unsigned char *data, int l) { return false; }
  void SetMapIdent(int Mapid) { mapid=Mapid; }
  bool Interruptible(void) { return interruptible; }
  bool Interrupted(void) { return interrupted; }
  virtual void AddMapCycles(unsigned int num) {}
  unsigned int MapCycles(void) { return cycles; }
  virtual unsigned int CpuCycles(void) { return 0; }
public:
  cMapCore(void);
  virtual ~cMapCore() {}
  };

// ----------------------------------------------------------------

class cN2Timer {
private:
  int ctrl, latch, id, delayInterrupt;
  unsigned char val;
  unsigned int cycles, intrCycles;
  double divisor, invDivisor;
  bool timerBugged;
  enum { tmCONTINUOUS=0x01, tmRUNNING=0x02, tmINTERRUPT=0x04, tmMASK=0xFF, tmLATCHED=0x100 };
  //
  void Stop(void);
  double GetDivisor();
  void Update();
  c6805* cpu;
public:
  cN2Timer();
  int Id() const { return id; }
  void SetId(unsigned char num, c6805* _cpu=NULL) { id = num; cpu = _cpu; }
  bool AddCycles(unsigned int count);
  unsigned int Cycles();
  unsigned char Ctrl();
  void Ctrl(unsigned char c);
  unsigned char Latch(void) { return latch&0xFF; }
  void Latch(unsigned char val);
  bool Running(void) { return ctrl&tmRUNNING; }
  bool InterruptSet(void) { return ctrl&tmINTERRUPT; }
};

// ----------------------------------------------------------------
#define HW_REGS   0x20
#define HW_OFFSET 0x0000

#define MAX_TIMERS 3
#define TIMER_NUM(x) (((x)>>2)&3) // timer order doesn't match HW order

// $00 = I/O Reg
//		0 = stop bit
// $01 = Security
// $06 = UART Clock control
// $08 = timer 0 value
// $09 = timer 0 latch
// $0a = timer 0 config
// $0d = PIC IMR
//		3 = timer 0
// $0e = CRC control
//		0 = busy
//		1 = disabled
// $0f = CRC data
// $10 = timer 1 value
// $11 = timer 1 latch
// $12 = timer 1 control
// $14 = timer 2 value
// $15 = timer 2 latch
// $16 = timer 2 control
//		0 = one-shot/periodic
//		1 = enabled
//		3-5 = prescalar
//		6-7 = multiplier

class cMapMemHW : public cMapMem {
public:
  // memory mapped HW
  enum {
    HW_IO=0x00, HW_SECURITY,
    HW_TIMER0_DATA=0x08, HW_TIMER0_LATCH, HW_TIMER0_CONTROL,
    HW_CRC_CONTROL=0x0e, HW_CRC_DATA,
    HW_TIMER1_DATA=0x10, HW_TIMER1_LATCH, HW_TIMER1_CONTROL,
    HW_TIMER2_DATA=0x14, HW_TIMER2_LATCH, HW_TIMER2_CONTROL
    };
private:
  c6805& cpu;
  // timer hardware
  cN2Timer timer[MAX_TIMERS];
  // CRC hardware
  enum { CRCCALC_DELAY=8, CRC_BUSY=1, CRC_DISABLED=2, CRC_POLY=0x8408 /* ccitt*/};
  unsigned short CRCvalue;
  unsigned char CRCpos:1;
  unsigned int CRCtime, CRCit;
  unsigned short CRC16table[256];
  void GenCRC16Table();
  unsigned short CRCcalculate(unsigned short crc, unsigned char val, int bits);
  void CRCupdate(unsigned int cycles);
  //
public:
  cMapMemHW(c6805& cpu);
  virtual unsigned char Get(unsigned short ea);
  virtual void Set(unsigned short ea, unsigned char val);
  int AddCycles(unsigned int num);
};

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
#define N2FLAG_NONE     0
#define N2FLAG_MECM     1
#define N2FLAG_Bx       2
#define N2FLAG_POSTAU   4
#define N2FLAG_Ex       8
#define N2FLAG_INV      128

// MECM Algo return values
enum {
  MARV_SYNCING = -3, MARV_DESYNCED, MARV_NOT_PROCESSED,
  MARV_ERROR = 0, MARV_SUCCESS, MARV_MAYBE_SUCCESS
};

class cN2Prov {
private:
  unsigned char seed[32], cwkey[8];
  bool keyValid;
protected:
  int id, flags, seedSize;
  unsigned char lastcw[17];    // last byte used to indicate confirmed cw
  unsigned char prevcw[16], lasthd[32];
  cIDEA idea;
  //
  virtual int Algo(int algo, const unsigned char *hd, const unsigned char *ed, unsigned char *hw, const unsigned char* cw) { return false; }
  virtual void DynamicHD(unsigned char *hd, const unsigned char *ed) {}
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
  virtual int RunEmu(unsigned char *data, int len, unsigned short load, unsigned short run, unsigned short stop, unsigned short fetch, int fetch_len, int maxinstr=100000) { return -1; }
  bool CanHandle(int Id) { return MATCH_ID(Id,id); }
  bool HasFlags(int Flags) { return (flags&Flags)==Flags; }
  void PrintCaps(int c);
  virtual bool BFReset() { return false;}
  virtual bool BFNext() { return false; }
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
