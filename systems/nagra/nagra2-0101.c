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

// -- cAuxSrv ------------------------------------------------------------------

/*
#define HAS_AUXSRV

static int auxPort=7777;
static char auxAddr[80]="localhost";
static char auxPassword[250]="auxserver";

class cAuxSrv : public cMutex {
private:
  cNetSocket so;
  //
  bool Login(void);
public:
  cAuxSrv(void);
  bool Map(int map, unsigned char *data, int len, int outlen);
  };

cAuxSrv::cAuxSrv(void)
:so(DEFAULT_CONNECT_TIMEOUT,7,DEFAULT_IDLE_TIMEOUT)
{}

bool cAuxSrv::Login()
{
  unsigned char buff[256];
  PRINTF(L_SYS_MAP,"auxsrv: connecting to %s:%d",auxAddr,auxPort);
  if(so.Connect(auxAddr,auxPort)) {
    buff[0]=0xA7;
    buff[1]=0x7A;
    buff[2]=0;
    int l=strlen(auxPassword);
    buff[3]=l;
    memcpy(&buff[4],auxPassword,l);
    buff[4+l]=0xFF;
    if(so.Write(buff,l+5)==l+5) return true;
    PRINTF(L_SYS_MAP,"auxsrv: login write failed");
    }
  so.Disconnect();
  return false;
}

bool cAuxSrv::Map(int map, unsigned char *data, int len, int outlen)
{
  if(len>500 || outlen>500) return false;
  cMutexLock lock(this);
  if(!so.Connected() && !Login()) return false;
  PRINTF(L_SYS_MAP,"auxsrv: calling map%02x",map);
  unsigned char buff[512];
  buff[0]=0xA7;
  buff[1]=0x7A;
  buff[2]=((len+1)>>8) & 0xff;
  buff[3]=(len+1)&0xff;
  buff[4]=map;
  memcpy(&buff[5],data,len);
  buff[len+5]=0xFF;
  if(so.Write(buff,len+6)==len+6) {
    if((len=so.Read(buff,sizeof(buff)))>0) {
      if(buff[0]==0x7A && buff[1]==0xA7) {
        if(buff[4]==0x00) {
          int l=buff[2]*256+buff[3];
          if(len>=l+5 && l==outlen+1) {
            if(buff[l+4]==0xFF) {
              memcpy(data,buff+5,outlen);
              return true;
              }
            else PRINTF(L_SYS_MAP,"auxsrv: bad footer in map%02x response",map);
            }
          else PRINTF(L_SYS_MAP,"auxsrv: bad length in map%02x response (got=%d want=%d)",map,l-1,outlen);
          }
        else PRINTF(L_SYS_MAP,"auxsrv: map%02x not successfull (unsupported?)",map);
        }
      else PRINTF(L_SYS_MAP,"auxsrv: bad response to map%02x",map);
      }
    else PRINTF(L_SYS_MAP,"auxsrv: map%02x read failed",map);
    }
  else  PRINTF(L_SYS_MAP,"auxsrv: map%02x write failed",map);
  so.Disconnect();
  return false;
}
*/

// -- cMap0101 ----------------------------------------------------------------

#include "nagra2-map57.c"

class cMap0101 : public cMapCore {
private:
  static const unsigned char primes[];
  unsigned char residues[53];
//  cAuxSrv aux;
  cN2Map57 map57;
protected:
  void DoMap(int f, unsigned char *data=0, int l=0);
public:
  cMap0101(void);
  };

const unsigned char cMap0101::primes[] = {
  0x03,0x05,0x07,0x0B,0x0D,0x11,0x13,0x17,0x1D,0x1F,0x25,0x29,0x2B,0x2F,0x35,0x3B,
  0x3D,0x43,0x47,0x49,0x4F,0x53,0x59,0x61,0x65,0x67,0x6B,0x6D,0x71,0x7F,0x83,0x89,
  0x8B,0x95,0x97,0x9D,0xA3,0xA7,0xAD,0xB3,0xB5,0xBF,0xC1,0xC5,0xC7,0xD3,0xDF,0xE3,
  0xE5,0xE9,0xEF,0xF1,0xFB
  };

cMap0101::cMap0101(void)
{
}

void cMap0101::DoMap(int f, unsigned char *data, int l)
{
  PRINTF(L_SYS_MAP,"0101: calling function %02X",f);
  switch(f) {
    case 0x3b:
      MakeJ();
      BN_zero(R);
      BN_set_bit(R,132); // or 66*wordsize ?
      BN_mod(H,R,D,ctx);
      for(int i=0; i<4; i++) MonMul(H,H,H);
      MonMul(B,A,H);
      break;
    case 0x4d:
      for(int i=0; i<53; i++) residues[i]=BN_mod_word(A,primes[i]);
      break;
    case 0x4e:
      {
      bool isPrime;
      do {
        BN_add_word(A,2);
        isPrime=true;
        for(int i=0; i<53; i++) {
          residues[i]+=2;
          residues[i]%=primes[i];
          if(residues[i]==0) isPrime=false;
          }
        } while(!isPrime);
      break;
      }
    case 0x57:
      map57.Map57(data);
      //aux.Map(0x57,data,l,l);
      break;
    default:
      if(!cMapCore::DoMap(f,data,l))
        PRINTF(L_SYS_MAP,"0101: unsupported call %02x",f);
      break;
    }
}

// -- cN2Prov0101 ----------------------------------------------------------------

class cN2Prov0101 : public cN2Prov, public cN2Emu, private cMap0101 {
protected:
  virtual bool Algo(int algo, const unsigned char *hd, unsigned char *hw);
  virtual void Stepper(void);
public:
  cN2Prov0101(int Id, int Flags):cN2Prov(Id,Flags) {}
  virtual int ProcessBx(unsigned char *data, int len, int pos);
  };

static cN2ProvLinkReg<cN2Prov0101,0x0101,N2FLAG_MECM|N2FLAG_Bx> staticPL0101;

bool cN2Prov0101::Algo(int algo, const unsigned char *hd, unsigned char *hw)
{
  if(algo==0x40) {
    memcpy(hw,hd,3);
    ExpandInput(hw);
    hw[0x18]|=1; hw[0x40]|=1;
    SetWordSize(2);
    ImportReg(IMPORT_A,hw,3);
    ImportReg(IMPORT_D,hw+24);
    DoMap(0x3b);
    ExportReg(EXPORT_C,hw);
    ImportReg(IMPORT_A,hw+40,3);
    ImportReg(IMPORT_D,hw+64);
    DoMap(0x3b);
    ExportReg(EXPORT_C,hw+40);
    DoMap(0x43);
    DoMap(0x44,hw);
    DoMap(0x44,hw+64);
    hw[0]=hw[64+4]; // ctx.h3&0xFF
    hw[1]=hw[64+5]; //(ctx.h3>>8)&0xFF
    memset(&hw[2],0,128-2);
    return true;
    }
  else if(algo==0x60) { // map 4D/4E/57
    memcpy(hw,hd,5);
    ExpandInput(hw);
    hw[127]|=0x80; hw[0]|=0x01;
    SetWordSize(16);
    ImportReg(IMPORT_A,hw);
    DoMap(0x4d);
    DoMap(0x4e);
    ExportReg(EXPORT_A,hw,16,true);
    ImportReg(IMPORT_A,hw);
    DoMap(0x4e);
    ExportReg(EXPORT_A,hw);
    DoMap(0x57,hw,128);
    return true;
    }

  PRINTF(L_SYS_ECM,"%04X: unknown MECM algo %02x",id,algo);
  return false;
}

int cN2Prov0101::ProcessBx(unsigned char *data, int len, int pos)
{
  if(Init(0x0101,102)) {
    SetMem(0x80,data,len);
    SetPc(0x80+pos);
    SetSp(0x0FFF,0x0FF0);
    ClearBreakpoints();
    AddBreakpoint(0x0000);
    AddBreakpoint(0x9569);
    AddBreakpoint(0xA822); // map handler
    while(!Run(1000)) {
      if(GetPc()==0x9569) {
        GetMem(0x80,data,len);
        return max((int)a,6);
        }
      if(GetPc()==0x0000)
        break;
      if(GetPc()==0xA822) {
        int size=wordsize<<3;
        unsigned char tmp[size];
        unsigned short addr=(Get(0x44)<<8)+Get(0x45);
        switch(a) {
          case SETSIZE:
            SetWordSize(Get(0x48)); break;
          case IMPORT_A ... IMPORT_D:
            GetMem(addr,tmp,size,0); ImportReg(a,tmp); break;
          case EXPORT_A ... EXPORT_D:
            ExportReg(a,tmp); SetMem(addr,tmp,size,0); break;
          case 0x0F:
            {
            unsigned char tmp2[size];
            GetMem(addr,tmp2,size,0);
            ExportReg(EXPORT_A,tmp);
            ImportReg(IMPORT_A,tmp2);
            SetMem(addr,tmp,size,0);
            break;
            }
          default:
            PRINTF(L_SYS_EMM,"%04X: unrecognized map call %02x",id,a);
            return -1;
          }
        PopCr(); PopPc();
        }
      }
    }
  return -1;
}

void cN2Prov0101::Stepper(void)
{
  unsigned short pc=GetPc();
  if(pc>=0x93 && pc<=0xE0) { 	// pc in EMM data
    unsigned char op=Get(pc);
    if((op&0xF0)==0x00) { 	// current opcode BRCLR/BRSET
      int fake=0; 		// 1=branch -1=non-branch
      if(Get(pc+3)==0x81) 	// next opcode == RTS
        fake=1;			// fake branch
      else {
        unsigned char off=Get(pc+2);
        unsigned short target=pc+3+off;
        if(off&0x80) target-=0x100;
        if(Get(target)==0x81) 	// branch target == RTS
          fake=-1;		// fake non-branch
        }
      if(fake) {
        unsigned short ea=Get(pc+1);
        unsigned char val=Get(dr,ea);
        int bit=1<<((op&0xF)>>1);
        // set/clr bit according to fake-mode and opcode
        if((fake>0 && (op&0x01)) || (fake<0 && !(op&0x01)))  {
          if(val&bit) loglb->Printf("*");
          val&=~bit;
          }
        else {
          if(!(val&bit)) loglb->Printf("*");
          val|=bit;
          }
        Set(dr,ea,val);
        }
      }
    }
}

// -- cN2Prov0901 --------------------------------------------------------------

class cN2Prov0901 : public cN2Prov0101 {
public:
  cN2Prov0901(int Id, int Flags):cN2Prov0101(Id,Flags) {}
  virtual int ProcessBx(unsigned char *data, int len, int pos);
  };

static cN2ProvLinkReg<cN2Prov0901,0x0901,N2FLAG_MECM|N2FLAG_Bx> staticPL0901;

int cN2Prov0901::ProcessBx(unsigned char *data, int len, int pos)
{
  if(Init(0x0801,102)) {
    return cN2Prov0101::ProcessBx(data,len,pos);
    }
  return -1;
}
