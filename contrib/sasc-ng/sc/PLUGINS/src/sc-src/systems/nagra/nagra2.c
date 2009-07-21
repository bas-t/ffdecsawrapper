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

#include "system.h"
#include "opts.h"
#include "log-core.h"
#include "debug.h"
#include "cpu.h"
#include "stdio.h"
#include "math.h"

#include "nagra.h"
#include "nagra2.h"

#define SYSTEM_NAME          "Nagra2"
#define SYSTEM_PRI           -10

// -- cN2Timer -----------------------------------------------------------------

cN2Timer::cN2Timer(): cpu(0)
{
  cycles=intrCycles=0; ctrl=0; val=0; divisor=0; invDivisor=0; timerBugged = false; latch=0xFF;
  delayInterrupt = 0;
}

double cN2Timer::GetDivisor()
{
	int prescalar = ((ctrl&0x38)>>3)+1;
	double divisor = pow(4.0, prescalar);
	double multiplier;

	// multipliers for 0 and 2 aren't correct yet
	switch((ctrl&0xc0)>>6) {
	case 0:	multiplier = 10.3f; break;
	case 1:	multiplier = 1.0f; break;
	case 2:	multiplier = 6.375f; break;
	default:multiplier = 0;
	}

	return divisor * multiplier;
}

void cN2Timer::Update()
{
	int rlatch = latch;

	if(divisor) {
		if(timerBugged && cycles>=0x100 * divisor) {
			timerBugged = false;
			cycles -= 0x101 * divisor;
		}
		if(timerBugged) rlatch = 0x100;

		unsigned int v = cycles * invDivisor;
		if(ctrl&tmCONTINUOUS) {
			val = rlatch - (v % (rlatch+1));
			if(InterruptSet() && val==0) {
				intrCycles = cycles + (unsigned int)((val?val:latch)*divisor);
			}
		} else {
			// one-shot
			if(v>=rlatch) {
				val = 0;
				if(Running() && ((cycles - 2) * invDivisor)>=rlatch)
					Stop();
			} else
				val = rlatch - v;
		}
	} else
		val = 0;
}

bool cN2Timer::AddCycles(unsigned int count)
{
	bool irq = false;
	if(Running()) {
		cycles += count;
		if(!delayInterrupt && InterruptSet() && cycles>=intrCycles) {
			delayInterrupt = intrCycles-cycles;
			if(delayInterrupt < 0)
				delayInterrupt = count;
			else
				delayInterrupt += count;
			intrCycles = 0;
			if((ctrl&tmCONTINUOUS)) Update();
		}
	}

	if(delayInterrupt) {
		delayInterrupt -= count;
		if(delayInterrupt < 0)
			delayInterrupt = 0;
		if(!delayInterrupt) {
			Update();
			irq = true;
		}
	}
	return irq;
}

unsigned int cN2Timer::Cycles()
{
	if(Running()) Update();
	PRINTF(L_SYS_EMU,"n2timer read %u @ %02x:%04x: %u %02x %02x = %02x", id, cpu?cpu->GetCr():0, cpu?cpu->GetPc():0, cycles, ctrl&0xff, latch&0xff, val);
	return val;
}

unsigned char cN2Timer::Ctrl()
{
	if(Running()) Update();
	return ctrl;
}

void cN2Timer::Latch(unsigned char val)
{
  if(!Running()) {
    latch=val; if(latch==0) latch=0x100;
    cycles=0;
    ctrl|=tmLATCHED;
	timerBugged = false;
  }
}

void cN2Timer::Ctrl(unsigned char val)
{
  if(Running()) {
    ctrl=(ctrl&~tmRUNNING) | (val&tmRUNNING);
    if(!Running()) {
      Stop();
	  --cycles;
	  Update();
      PRINTF(L_SYS_EMU,"n2timer %u: stopped cycles=%d (%02x) ctrl=%02x latch=%02x",id,cycles,val,ctrl,latch&0xff);
    }
  }
  else {
    ctrl=(ctrl&~tmMASK) | (val&tmMASK);
	divisor = GetDivisor();
	if(divisor) invDivisor = 1.0 / divisor;
    if(Running()) {
		if(!(ctrl&tmLATCHED) && divisor>0) {
			if(this->val==0) {
				timerBugged = true;
				cycles = (unsigned int)divisor;
			} else {
				cycles = (unsigned int)(((timerBugged?0x100:latch)-this->val+1) * divisor);
			}
		}
		if((ctrl&tmINTERRUPT)) {
			intrCycles = cycles + (unsigned int)((timerBugged?0x101:this->val?this->val:latch)*divisor);
			if(id==2) PRINTF(L_SYS_MAP,"Timer %d set for %u cycles (%02x %02x)", id, intrCycles-cycles, Latch(), ctrl&0xff);
		}
		ctrl &= ~tmLATCHED;
	}
  }
}

void cN2Timer::Stop(void)
{
  ctrl&=~(tmRUNNING|tmLATCHED);
  if(delayInterrupt>=3)
	  delayInterrupt = 0;
}

// -- cMapMemHW ----------------------------------------------------------------

//#define CRC_POLY 0x8408 // ccitt poly


cMapMemHW::cMapMemHW(c6805& _cpu)
:cMapMem(HW_OFFSET,HW_REGS),cpu(_cpu)
{
  CRCvalue=0xffff; CRCit=CRCCALC_DELAY+1; CRCpos=0; CRCtime=0;
  GenCRC16Table();
  for(int i=0; i<MAX_TIMERS; ++i)
	  timer[i].SetId((i+1)%3, &cpu);
  PRINTF(L_SYS_EMU,"mapmemhw: new HW map off=%04x size=%04x",offset,size);

  Set(HW_IO, 0xfe);				// IO
  Set(HW_CRC_CONTROL, CRC_DISABLED); // CRC disabled
  Set(HW_TIMER0_LATCH, 0x00);	// initialize timer0 latch
  Set(HW_TIMER1_LATCH, 0x00);	// initialize timer1 latch
  Set(HW_TIMER2_LATCH, 0x00);	// initialize timer2 latch
  Set(HW_TIMER0_CONTROL, 0x00);	// disable timer0
  Set(HW_TIMER1_CONTROL, 0x00);	// disable timer1
  Set(HW_TIMER2_CONTROL, 0x00);	// disable timer2
}

void cMapMemHW::GenCRC16Table()
{
	for(int i=0; i<256; ++i) {
		unsigned short c = i;
		for(int j=0; j<8; ++j)
			c = (c>>1) ^ ((c&1) ? CRC_POLY : 0);
		CRC16table[i] = c;
	}
}

unsigned short cMapMemHW::CRCcalculate(unsigned short crc, unsigned char val, int bits)
{
	if(bits>=8) return (crc>>8)^CRC16table[(crc^val)&0xff];
	if(bits>0) crc ^= (val&(0xff>>(8-bits)));
	for(int i=0; i<bits; ++i)
		crc = (crc>>1) ^ ((crc&1) ? CRC_POLY : 0);
	return crc;
}

void cMapMemHW::CRCupdate(unsigned int cycles)
{
	if(!(mem[HW_CRC_CONTROL-offset]&CRC_DISABLED) && CRCit<=CRCCALC_DELAY) {
		int elapsed = cycles-CRCtime;
		CRCtime = cycles;
		int it = CRCit;
		unsigned char val = mem[HW_CRC_DATA-offset];
		if((CRCit+=elapsed)>CRCCALC_DELAY)	// > instead of >= because of CRC_BUSY lockout
			CRCit = CRCCALC_DELAY+1;
		if(it==0 && CRCit>=CRCCALC_DELAY)
			CRCvalue = (CRCvalue>>8)^CRC16table[(CRCvalue^val)&0xff];
		else
			for(int i=it; i<CRCit && i<CRCCALC_DELAY; ++i)
				CRCvalue = (CRCvalue>>1) ^ (((CRCvalue^(val>>i))&1) ? CRC_POLY : 0);
	}
}

int cMapMemHW::AddCycles(unsigned int num)
{
  int mask=0;
  for(int i=0; i<MAX_TIMERS; i++)
	  if(timer[i].AddCycles(num)) mask |= 1<<timer[i].Id();
  return mask;
}

unsigned char cMapMemHW::Get(unsigned short ea)
{
  if(ea<offset || ea>=offset+size) return 0;
  ea-=offset;
  switch(ea) {
	case HW_SECURITY:
	  return (mem[ea]&0x70)|0x0f;
    case HW_TIMER0_CONTROL:
    case HW_TIMER1_CONTROL:
    case HW_TIMER2_CONTROL:
      return timer[TIMER_NUM(ea)].Ctrl();
    case HW_TIMER0_DATA:
    case HW_TIMER1_DATA:
    case HW_TIMER2_DATA:
      return timer[TIMER_NUM(ea)].Cycles();
    case HW_TIMER0_LATCH:
    case HW_TIMER1_LATCH:
    case HW_TIMER2_LATCH:
      return timer[TIMER_NUM(ea)].Latch();
    case HW_CRC_CONTROL:
      {
      unsigned char r=mem[ea];
	  CRCupdate(cpu.Cycles());
	  if(CRCit<CRCCALC_DELAY) r |= CRC_BUSY;
	  else r &= ~CRC_BUSY;
      return r|0x80;
      }
    case HW_CRC_DATA:
      {
	  CRCupdate(cpu.Cycles());
	  unsigned char r=(CRCvalue^0xffff)>>((CRCpos&1)<<3);
      CRCpos^=1;
      return r;
      }
    default:
      return mem[ea];
    }
}

void cMapMemHW::Set(unsigned short ea, unsigned char val)
{
  if(ea<offset || ea>=offset+size) return;
  ea-=offset;
  switch(ea) {
    case HW_TIMER0_CONTROL:
    case HW_TIMER1_CONTROL:
    case HW_TIMER2_CONTROL:
      timer[TIMER_NUM(ea)].Ctrl(val);
      break;
    case HW_TIMER0_LATCH:
    case HW_TIMER1_LATCH:
    case HW_TIMER2_LATCH:
      timer[TIMER_NUM(ea)].Latch(val);
      break;
    case HW_CRC_CONTROL:
	  if((mem[ea]&CRC_DISABLED)) {
		  if(!(val&CRC_DISABLED)) {
	        CRCvalue=0xffff; CRCpos=0; CRCtime=cpu.Cycles();
			if(CRCit<=CRCCALC_DELAY) {
				++CRCtime;
				++CRCit;
			}
		  }
	  } else if((val&CRC_DISABLED)) {
		  CRCupdate(cpu.Cycles());
	  }
      mem[ea]=val;
      break;
    case HW_CRC_DATA:
	  CRCupdate(cpu.Cycles());
      if(CRCit>CRCCALC_DELAY) {
         if(!(mem[HW_CRC_CONTROL-offset]&CRC_DISABLED)) {
			mem[ea] = val;
			CRCit = 0;
			CRCtime=cpu.Cycles();
         }
	  }
      break;
    default:
      mem[ea]=val;
      break;
    }
}

// -- cN2Emu -------------------------------------------------------------------

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

// -- cMapReg ------------------------------------------------------------------

cMapReg::cMapReg(int *_defwordsize, int _maxwordsize): wordsize(DEF_WORDSIZE),maxwordsize(_maxwordsize),
	defwordsize(_defwordsize)
{
}

BIGNUM* cMapReg::Value(int wsize, bool mask)
{
	wsize = OpWordSize(wsize);
	if(wordsize!=wsize) {
		Commit();
		Reload();
	} else if(mask) {
		BN_mask_bits(reg, wsize*64);
	}
	return reg;
}

void cMapReg::ClearReg(int wsize)
{
	BN_rshift(reg,reg,wsize*64);
	BN_lshift(reg,reg,wsize*64);
}

void cMapReg::ClearFullReg(int wsize)
{
	BN_rshift(fullreg,fullreg,wsize*64);
	BN_lshift(fullreg,fullreg,wsize*64);
}

void cMapReg::SetTmp(BIGNUM* val, int wsize)
{
	if(val->neg) {
		BN_clear(tmp);
		BN_set_bit(tmp,wsize*64);
		BN_add(tmp,tmp,val);
	} else
		BN_copy(tmp,val);
	BN_mask_bits(tmp,wsize*64);
}

void cMapReg::Commit(int wsize, int resync)
{
	if(resync<0 && wsize<0) resync = 1;
	wsize = OpWordSize(wsize>=0?wsize:wordsize);
	ClearFullReg(wsize);
	SetTmp(reg,wsize);
	BN_add(fullreg, fullreg, tmp);
	if(resync) {
		if(wordsize==wsize) BN_mask_bits(reg, wsize*64);
		else wordsize = wsize;
	}
}

void cMapReg::Reload(int wsize)
{
	wsize = OpWordSize(wsize>=0?wsize:wordsize);
	wordsize = wsize;
	BN_copy(reg, fullreg);
	BN_mask_bits(reg, 64*wsize);
}

void cMapReg::GetLE(const unsigned char *in, int n)
{
	int wsize = OpWordSize(n<=0?n:(n+7)/8);
	if(wordsize>wsize) Commit();
	reg.GetLE(in, 8*wsize);
	Commit(wsize);
}

void cMapReg::PutLE(unsigned char* out, int n)
{
	int wsize = OpWordSize(n<=0?n:(n+7)/8);
	Commit();
	fullreg.PutLE(out, 8*wsize);
}

void cMapReg::Set(BIGNUM* val, int wsize)
{
	wsize = OpWordSize(wsize);
	if(wordsize!=wsize) Commit();
	ClearReg(wsize);
	SetTmp(val,wsize);
	if(wordsize!=wsize) {
		ClearFullReg(wsize);
		BN_add(fullreg, fullreg, tmp);
	}
	BN_add(reg, reg, tmp);
}

void cMapReg::Clear(int wsize)
{
	wsize = OpWordSize(wsize);
	if(wordsize!=wsize) {
		Commit();
		ClearFullReg(wsize);
	}
	ClearReg(wsize);
}

// -- cMapMath -----------------------------------------------------------------

cMapMath::cMapMath(): A(&wordsize),B(&wordsize),C(&wordsize),D(&wordsize),J(NULL,1),I(&wordsize)
{
	wordsize = 4;
}
bool cMapMath::ModAdd(BIGNUM *r, BIGNUM *a, BIGNUM *b, BIGNUM *d)
{
	bool ret = false;
    BN_add(r,a,b);
	if(BN_cmp(r,d)>=0) {
		BN_sub(r,r,d);
		ret = true;
	}
    BN_mask_bits(r,wordsize<<6);
	return ret;
}

bool cMapMath::ModSub(BIGNUM *r, BIGNUM *d, BIGNUM *b)
{
    cBN p;
	bool ret = BN_cmp(d, b)<0;
    BN_set_bit(p,wordsize<<6);
    BN_mod_sub(r,d,b,p,ctx);
    BN_mask_bits(r,wordsize<<6);
	return ret;
}

void cMapMath::MakeJ0(BIGNUM *j, BIGNUM *d, BIGNUM* c, int bits)
{
#if OPENSSL_VERSION_NUMBER < 0x0090700fL
#error BN_mod_inverse is probably buggy in your openssl version
#endif
    BN_zero(x);
    BN_sub(j,x,d);
	j->neg = 1;
    BN_set_bit(j,0);
    BN_set_bit(x,bits);
	BN_mod_inverse(j,j,x,ctx);
	if(c) {
		BN_copy(c,d);
		BN_mask_bits(c,bits);
		BN_mul(c,j,c,ctx);
		BN_rshift(c,c,bits);
		BN_mask_bits(c,bits);
	}
}

void cMapMath::MonMul0(BIGNUM *o, BIGNUM *a, BIGNUM *b, BIGNUM *c, BIGNUM *d, BIGNUM *j, int words)
{
	if(words<=0) words = wordsize;
    BN_zero(s);
	for(int i=0; --words>=0; ) {
        BN_rshift(x,a,(i++)<<6);
        BN_mask_bits(x,64);
        BN_mul(x,x,b,ctx);
        BN_add(s,s,x);

        BN_copy(x,s);
        BN_mask_bits(x,64);
        BN_mul(x,x,j,ctx);
        if(!words) {
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
        if(!words) {
            // High
            BN_lshift(y,s,12);
            BN_add(c,c,y);
            BN_mask_bits(c,wordsize<<6);
        }

        BN_rshift(s,s,64);
        if(words && BN_cmp(s,d)>=0) {
            BN_copy(x,s);
            BN_sub(s,x,d);
        }
    }
    BN_copy(o,s);
}

void cMapMath::MonMulFin(BIGNUM *o, BIGNUM *d)
{
	if(BN_cmp(o,d)>=0)
		BN_sub(o,o,d);
}

void cMapMath::MonMul(BIGNUM *o, BIGNUM *a, BIGNUM *b, BIGNUM *c, BIGNUM *d, BIGNUM *j, int words)
{
	MonMul0(o, a, b, c, d, j, words);
	MonMulFin(o, d);
}


// -- cMapCore -----------------------------------------------------------------

cMapCore::cMapCore(void)
{
  last=1; mapid=0;
  regs[0]=&J; regs[1]=&A; regs[2]=&B; regs[3]=&C; regs[4]=&D;
  interruptible=false;
}

void cMapCore::ImportReg(unsigned char reg, const unsigned char *in, int l)
{
  int dl=(l>0?l:wordsize)<<3;
  cBN tmp;
  switch(reg) {
    case IMPORT_J:
    case IMPORT_A:
    case IMPORT_B:
    case IMPORT_C:
    case IMPORT_D:
		last = reg-IMPORT_J;
		// fall through
	case IMPORT_LAST:
		regs[last]->GetLE(in+(last>0?0:dl-8),last>0?dl:8);
		break;
    default: PRINTF(L_SYS_MAP,"internal: nagramap import register not supported"); return;
    }
}

void cMapCore::ExportReg(unsigned char reg, unsigned char *out, int l)
{
  int dl=(l>0?l:wordsize)<<3;
  switch(reg) {
	case EXPORT_J:
    case EXPORT_A:
    case EXPORT_B:
    case EXPORT_C:
    case EXPORT_D:
		last = reg-EXPORT_J;
		// fall through
	case EXPORT_LAST:
		regs[last]->PutLE(out,last>0?dl:8);
		if(reg==EXPORT_LAST && last==0)
			for(int i=8; i<dl; i+=8) memcpy(out+i,out,8);
		break;
    default: PRINTF(L_SYS_MAP,"internal: nagramap export register not supported"); return;
    }
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

void cMapCore::MonExp(BIGNUM *scalar)
{
  if(BN_is_zero(D)) { BN_one(A); return; }
  BN_copy(A,B);
  for(int i=BN_num_bits(scalar)-2; i>-1; i--) {
    MonMul(B,B,B);
    if(BN_is_bit_set(scalar,i)) MonMul(B,A,B);
    }
  BN_one(A);
  MonMul(B,A,B);
}

void cMapCore::MonExpNeg(void)
{
  if(BN_is_zero(D)) { BN_set_word(A,1); return; }
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
  if(BN_is_bit_set(D,0)) {
    int i;
    for(i=BN_num_bits(D)-2; i>0; i--) if(BN_is_bit_set(D,i)) break;
    if(i<=0) {
      MonMul(B,B,B);
      MonMul(B,A,B);
      }
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
  BN_mask_bits(I,wordsize<<6);
  BN_rshift(I,I,1);
  MonMul(s140,I,B);
  MonMul(s160,B,a);
}

int cMapCore::GetOpSize(int l)
{
  return l!=0 ? l : wordsize;
}

void cMapCore::DoMap(int f, unsigned char *data, int l)
{
  PRINTF(L_SYS_MAP,"%04x: calling function %02X",mapid,f);
  cycles=0;
  unsigned int startcycles=CpuCycles();
  interrupted=false; interruptible=true;
  try {
    if(!Map(f,data,l) && !MapGeneric(f,data,l))
      PRINTF(L_SYS_MAP,"%04x: unsupported call %02x",mapid,f);
    if(cycles) {
      unsigned int elapsed=CpuCycles()-startcycles;
      if(cycles>elapsed) AddMapCycles(cycles-elapsed);
      }
    } catch(int) { interrupted=true; }
  interruptible=false;
  cycles=CpuCycles()-startcycles;
}

bool cMapCore::MapGeneric(int f, unsigned char *data, int l)
{
  const int l1=GetOpSize(l);
  const int dl=l1<<3;
  switch(f) {
    case SETSIZE:
      cycles=(l>17 ? 459 : (l ? 475 : 454))-6;
      if(l>=1 && l<=17) wordsize=l;
      break;

    case IMPORT_J:
      cycles=890-6;
      last=0;
      regs[0]->GetLE(data,8);
      break;
    case IMPORT_A:
    case IMPORT_B:
    case IMPORT_C:
    case IMPORT_D:
      if(l>17) { l=17; cycles+=5; }
      else if(l<=0) { l=wordsize; cycles+=4; }
      cycles+=771+160*l-6;
      last=f-IMPORT_J;
      regs[last]->GetLE(data,l<<3);
      break;
    case IMPORT_LAST:
      if(l>16) { l=1; cycles+=5; }
      else if(l<=0) l=1;
      cycles=656+160*l-6;
      regs[last]->GetLE(data,(last==0?1:l)<<3);
      break;

    case EXPORT_J:
      cycles=897-6;
      last=0;
      regs[0]->PutLE(data,8);
      break;
    case EXPORT_A:
    case EXPORT_B:
    case EXPORT_C:
    case EXPORT_D:
      if(l>17) { l=17; cycles+=5; }
      else if(l<=0) { l=wordsize; cycles+=4; }
      cycles=778+160*l-6;
      last=f-EXPORT_J;
      regs[last]->PutLE(data,l<<3);
      break;
    case EXPORT_LAST:
      if(l>16) { l=1; cycles+=5; }
      else if(l<=0) l=1;
      cycles=668+160*l-6;
      regs[last]->PutLE(data,(last==0?1:l)<<3);
      break;

    case SWAP_A:
    case SWAP_B:
    case SWAP_C:
    case SWAP_D:
      cycles=776+248*l1-6;
      last=f-SWAP_A+1;
      e.GetLE(data,dl);
      regs[last]->PutLE(data,dl);
      regs[last]->Set(e,l1);
      break;

    case CLEAR_A:
    case CLEAR_B:
    case CLEAR_C:
    case CLEAR_D:
      cycles=462+(8*l1+3)/5*5-6;
      last=f-CLEAR_A+1;
      regs[last]->Clear(l1);
      break;

    case COPY_A_B:
      last=2; B.Set(A,l1); cycles=462+(8*l1+3)/5*5-6; break;
    case COPY_B_A:
      last=1; A.Set(B,l1); cycles=462+(8*l1+3)/5*5-6; break;
    case COPY_A_C:
      last=3; C.Set(A,l1); cycles=462+(8*l1+3)/5*5-6; break;
    case COPY_C_A:
      last=1; A.Set(C,l1); cycles=462+(8*l1+3)/5*5-6; break;
    case COPY_C_D:
      last=4; D.Set(C,l1); cycles=462+(8*l1+3)/5*5-6; break;
    case COPY_D_C:
      last=3; C.Set(D,l1); cycles=462+(8*l1+3)/5*5-6; break;

    case 0x39:
    case 0x3a:
      AddMapCycles(512);
      WS_START(1);
      MakeJ0(J,D);
      AddMapCycles(256);
      WS_END();
      AddMapCycles(340);
      if(!BN_is_zero(D)) {
        BN_zero(I);
        BN_set_bit(I,68*wordsize);
        BN_mod(B,I,D,ctx);
        }
      AddMapCycles(320);
      for(int i=0; i<4; i++) MonMul(B,B,B);

      if(f==0x39) I.GetLE(data,wordsize<<3);
      MonMul(B,(f==0x39?I:A),B);
      MonMul(B,A,B);
      break;
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
      if(dl) {
        if(dl>1) RotateBytes(data,dl);
        SHA1_Update(&sctx,data,dl);
        }
      SHA1_Final(data,&sctx);
      RotateBytes(data,20);
      break;
    default:
      return false;
    }
  return true;
}

// -- cN2Prov ------------------------------------------------------------------

cN2Prov::cN2Prov(int Id, int Flags)
{
//  keyValid=false; id=Id|0x100; flags=Flags; seedSize=5;
	id=Id|0x100; flags=Flags;
  memset(lastcw, 0, sizeof(lastcw));
  memset(prevcw, 0, sizeof(prevcw));
  memset(lasthd, 0, sizeof(lasthd));
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
  int rv;
  hd[0]=in15&0x7F;
  hd[1]=cw[14];
  hd[2]=cw[15];
  hd[3]=cw[6];
  hd[4]=cw[7];

  DynamicHD(hd,ed);
  hd[seedSize] = algo;

  if(keyValid && !memcmp(seed,hd,seedSize+1)) {	// key cached
    memcpy(buf,cwkey,8);
    rv = MARV_SUCCESS;
  }
  else {				// key not cached
    memset(hw,0,sizeof(hw));
    if((rv=Algo(algo,hd,ed,hw,cw))==MARV_ERROR) return false;
    memcpy(&hw[128],hw,64);
    RotateBytes(&hw[64],128);
    SHA1(&hw[64],128,buf);
    RotateBytes(buf,20);

    if(rv==MARV_SUCCESS) {
      memcpy(seed,hd,seedSize+1);
      memcpy(cwkey,buf,8);
      keyValid=true;
    }
  }

  memcpy(prevcw, cw, sizeof(prevcw));
  memcpy(lasthd, hd, seedSize+1);

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

  if(rv==MARV_DESYNCED)
    lastcw[16] = 0;
  else if(rv>=MARV_SUCCESS) {
    if(rv==MARV_SUCCESS) lastcw[16] = 1;
    memcpy(lastcw, cw, 16);
  }

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

// -- cN2Providers -------------------------------------------------------------

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

// -- cN2ProvLink --------------------------------------------------------------

cN2ProvLink::cN2ProvLink(int Id, int Flags)
{
  id=Id; flags=Flags;
  cN2Providers::Register(this);
}

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
  BN_add_word(q,((key[22]&0xf)<<9) | (key[23]<<1));
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
    switch(ecm->source) { 
      case 0x8267: // S61.5W 
      case 0x82d0: // S72.0W 
      case 0x8302: // S77.0W 
      case 0x844c: // S110.0W 
      case 0x849c: // S118.0W 
      case 0x84a6: // S119.0W 
      case 0x841a: // S105.0W 
      case 0x84ba: // S121.0W 
      case 0x850a: // S129.0W 
      case 0x85c8: // S148.0W 
        data[5]=0x01; 
        break; 
      case 0x8334: // S82.0W 
      case 0x838e: // S91.0W 
        data[5]=0x09; 
        break; 
      case 0x83ca: // S97.0W 
        data[5]=0xc1; 
        break; 
      default: 
        if (ecm->caId==0x1234) 
          data[5]=0x09; 
        else 
          data[5]=0x01; 
    } 
 
    data[6]&=0x1F;      
    data[7]=data[7]&0x10|0x86;
    data[8]=0;
    data[9]=data[9]&0x80|0x08;
  } 
  int cmdLen=data[4]-5;
  int id=(data[5]*256)+data[6];
  cTimeMs minTime;
  int provider=id;
  if(id==0x4101) StartLog(ecm,0x1881); // D+ AU
  if(id==0x0505 || id==0x0503 || id==0x0511) id=0x0501; // PremStar  ugly again :(
  if (id >= 0x0101 && id <= 0x01FF) {
	provider = 0x0101;
  } else if (id >= 0x0901 && id <= 0x09FF) {
	provider = 0x0901;
  } else {
	provider = id;
  }

  if(cmdLen<64 || SCT_LEN(data)<cmdLen+10) {
    if(doLog) PRINTF(L_SYS_ECM,"bad ECM message msgLen=%d sctLen=%d",cmdLen,SCT_LEN(data));
    return false;
    }

  int keyNr=(data[7]&0x10)>>4;
  cKeySnoop ks(this,'N',provider,keyNr);
  cPlainKey *pk;

  cBN m1;
  unsigned char ideaKey[16], vKey[16];
  bool hasVerifyKey=false;
  if(!(pk=keys.FindKey('N',provider,MBC('M','1'),-1)))  {
    if(doLog) PRINTF(L_SYS_KEY,"missing %04x M1 key",id);
    return false;
    }
  pk->Get(m1);
  if((pk=keys.FindKeyNoTrig('N',provider,'V',sizeof(vKey)))) {
    pk->Get(vKey);
    hasVerifyKey=true;
    }
  else if(doLog && id!=lastEcmId) PRINTF(L_SYS_KEY,"missing %04x V key (non-fatal)",id);
  if(!(pk=keys.FindKey('N',provider,keyNr,sizeof(ideaKey)))) return false;
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
   char txt[150];

  if(buffer[0]==0x83 && dropEMMs) return; // skip EMM-S if disabled
   if(cmdLen<96 || SCT_LEN(buffer)<cmdLen+15) {
	PRINTF(L_SYS_EMM,"Bad EMM message msgLen=%d sctLen=%d",cmdLen,SCT_LEN(buffer));
	return;
    }
   char time_str[20];
   char text[100];
   int keyset=(buffer[12]&0x03);
   int sel=(buffer[12]&0x10)<<2;
   int sigsel=(buffer[13]&0x80)>>1;
	int CamID = 0;
	int provider=id;
	if (id >= 0x0101 && id <= 0x010F) {
		provider = 0x0101;
	} else if (id >= 0x0901 && id <= 0x090F) {
		provider = 0x0901;
	} else {
		provider = id;
	}
  if((!emmP && id!=lastEmmId) || (emmP && !emmP->CanHandle(id))) {
    delete emmP;
    emmP=cN2Providers::GetProv(id,N2FLAG_NONE);
//    if(emmP) emmP->PrintCaps(L_SYS_EMM);
    }
  lastEmmId=id;

  //HEXDUMP(L_SYS_RAWEMM,buffer+14,cmdLen,"Nagra2 Encrypted EMM");
  //id=(emmdata[8]<<8)+emmdata[9];
  //LBSTARTF(L_SYS_EMM);
	int keyPTR = N2_MAGIC;
	if (buffer[0] == 0x83) {
  		keyPTR = 0;
		for (int it=5; it>2; it--) {
			CamID = MBC(CamID,buffer[it]);
		}
  		//keyPTR=CID(N2_EMM_S,buffer[5],buffer[4],buffer[3]);
	}	


  cPlainKey *pk;
  cBN n;
  unsigned char ideaKey[24], vKey[16];
  bool hasVerifyKey=false;
	if(!(pk=keys.FindKey('N',provider,MBC(keyPTR,keyset+0x10+sel),96,0)))  
		{
		PRINTF(L_SYS_EMM,"You are missing %04x NN %.02X RSA key (96 bytes)",id,keyset+0x10+sel);
		return;
    }
  pk->Get(n);
	if (buffer[0] != 0x83) {
	    if((pk=keys.FindKey('N',provider,MBC(keyPTR,0x03+sigsel),sizeof(vKey)))) {
			pk->Get(vKey);
			hasVerifyKey=true;
		}
		else if(id!=lastEmmId) {
			PRINTF(L_SYS_EMM,"You are missing %04x NN %.02X signature key (non-fatal)",id,0x03+sigsel);
		}
	}
	if(!(pk=keys.FindKey('N',provider,MBC(keyPTR,keyset),24,0))) {
	    if(!(pk=keys.FindKey('N',provider,MBC(keyPTR,keyset+sel),16))) {
			PRINTF(L_SYS_EMM,"You are missing %04x NN %.02x IDEA key (24 or 16 bytes)",id,keyset);
			return;
		}
		memset(ideaKey+16,0,8);
	}
  pk->Get(ideaKey);

  unsigned char emmdata[256];
	if(!DecryptEMM(buffer+14,emmdata,ideaKey,cmdLen,hasVerifyKey?vKey:0,n)) {
		sprintf(txt,"[%s] nagra2: decrypt of EMM failed (%04x)", time_str, id);
		if (buffer[0] == 0x82) {
			PRINTF(L_SYS_EMM,"Bad EMM-G Packet (Idea Hash failed! -> RSA decryption failed!): %04x", id);
		} else if (buffer[0] == 0x83) {
			PRINTF(L_SYS_EMM,"EMM-S {%06X%02X} (Idea Hash failed! -> RSA decryption failed!)",CamID,buffer[6]);
		}
		return;
	} else {
		if (buffer[0] == 0x82) {
			PRINTF(L_SYS_EMM,"Decrypt of EMM-G Succeeded (%04x)", id);
			char str2[51];	// 4AFFB6B0C6C52F7C0801144F8C9F18717787E3
			sprintf(&str2[0],"%02X ",emmdata[18]);
			for(int it=1; it<20; it++) sprintf(&str2[(it*2)+1],"%02X",emmdata[it+18]);
			PRINTF(L_SYS_EMM,"Decrypt of EMM-S Succeeded (%04x)", id);
		}
	}
	lastEmmId=id;
  if((!emmP && id!=lastEmmId) || (emmP && !emmP->CanHandle(id))) {
    delete emmP;
    emmP=cN2Providers::GetProv(id,N2FLAG_NONE);
    if(emmP) emmP->PrintCaps(L_SYS_EMM);
    }
  lastEmmId=id;

  HEXDUMP(L_SYS_RAWEMM,buffer+14,cmdLen,"Nagra2 Encrypted EMM");
  id=(emmdata[8]<<8)+emmdata[9];
  LBSTARTF(L_SYS_EMM);
  bool contFail=false;

	if (buffer[0] == 0x82) {
		id=(emmdata[8]<<8)+emmdata[9];
	}

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
            if((pk=keys.FindKeyNoTrig('N',id,kn,len))) {
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
					for(int it=7 ; it<23 ; it++) sprintf(&text[(it-7)*2],"%02X",emmdata[i+it]);
					PRINTF(L_SYS_EMM,"Key %02X: %s  (%04X)",(emmdata[i+3]&0x40)>>6,text,id);
              if(cPlainKeyNagra::IsBNKey(kn)) {
                n.GetLE(key,len);
                if(keys.NewKey('N',id,kn,n,len)) NewKey();
                }
              else {
		  if(keys.NewKey('N',id,kn,key,len)) {
			NewKey();
	  	  }
                }
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
          PRINTF(L_SYS_EMM,"Key packet for %04x",id);
          if(keys.NewKey('N',id,(emmdata[i+16]&0x40)>>6,&emmdata[i+23],16)) NewKey();
          }
        i+=emmdata[i+1]+2;
        break;
      case 0x83: // change data prov. id
        id=(emmdata[i+1]<<8)|emmdata[i+2];
        i+=3;
        break;
      		//CamID target update
	  case 0xA0:
		    PRINTF(L_SYS_EMM, "EMM-S CAM Id Update");
	        i+=5;
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
		if((r=emmP->ProcessBx(emmdata,cmdLen,i+1))>0 || (r==0 && emmdata[i]!=0xb1)){
          PRINTF(L_SYS_EMM,"B%X executing succeeded for %04x",bx,id);
          i+=r;
		  } 
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
      case 0x12: i+=emmdata[i+1]+2; PRINTF(L_SYS_EMM,"Create tier packet"); break; // create tier			
      case 0x20: i+=19; PRINTF(L_SYS_EMM,"Modify tier packet"); break;   // modify tier
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
		if((r=emmP->ProcessEx(emmdata,cmdLen,i+1))>0){
          PRINTF(L_SYS_EMM,"E%X for provider %04x succeeded",ex,id);
          i+=r;
		  } 
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
