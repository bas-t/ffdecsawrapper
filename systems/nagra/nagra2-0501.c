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

// -- cMap0501 -----------------------------------------------------------------

class cMap0501 : public cMapCore {
private:
  int mId;
protected:
  void DoMap(int f, unsigned char *data=0, int l=0);
public:
  cMap0501(int Id);
  };

cMap0501::cMap0501(int Id)
{
  mId=Id|0x100;
}

void cMap0501::DoMap(int f, unsigned char *data, int l)
{
  PRINTF(L_SYS_MAP,"%04x: calling function %02X",mId,f);
  switch(f) {
    case 0x37:
      l=(l?l:wordsize)<<3;
      B.GetLE(data,l);
      MonMul(B,B,A);
      break;
    case 0x3a:
      MonInit();
      MonMul(B,A,B);
      MonMul(B,A,B);
      break;
    default:
      if(!cMapCore::DoMap(f,data,l))
        PRINTF(L_SYS_MAP,"%04x: unsupported call %02x",mId,f);
      break;
    }
}

// -- cN2Prov0501 --------------------------------------------------------------

class cN2Prov0501 : public cN2Prov, private cMap0501, public cN2Emu {
protected:
  virtual bool Algo(int algo, unsigned char *hd, const unsigned char *ed, unsigned char *hw);
  virtual bool NeedsCwSwap(void) { return true; }
public:
  cN2Prov0501(int Id, int Flags);
  virtual int ProcessBx(unsigned char *data, int len, int pos);
  };

static cN2ProvLinkReg<cN2Prov0501,0x0501,(N2FLAG_MECM|N2FLAG_INV|N2FLAG_Bx)> staticPL0501;

cN2Prov0501::cN2Prov0501(int Id, int Flags)
:cN2Prov(Id,Flags)
,cMap0501(Id)
{}

bool cN2Prov0501::Algo(int algo, unsigned char *hd, const unsigned char *ed, unsigned char *hw)
{
  if(algo==0x60) {
    hw[0]=hd[0];
    hw[1]=hd[1];
    hw[2]=hd[2]&0xF8;
    ExpandInput(hw);
    hw[63]|=0x80;
    hw[95]=hw[127]=hw[95]&0x7F;
    DoMap(SETSIZE,0,4);
    DoMap(IMPORT_J,hw+0x18);
    DoMap(IMPORT_D,hw+0x20);
    DoMap(IMPORT_A,hw+0x60);
    DoMap(0x37,hw+0x40);
    DoMap(EXPORT_C,hw);
    DoMap(0x3a);
    DoMap(EXPORT_C,hw+0x20);
    DoMap(0x43);
    DoMap(0x44,hw);
    hw[0]&=7;
    DoMap(EXPORT_B,hw+3);
    memset(hw+3+0x20,0,128-(3+0x20));
    return true;
    }

  PRINTF(L_SYS_ECM,"%04X: unknown MECM algo %02x",id,algo);
  return false;
}

int cN2Prov0501::ProcessBx(unsigned char *data, int len, int pos)
{
  if(Init(id,120)) {
    SetMem(0x80,data,len);
    SetPc(0x80+pos);
    SetSp(0x0FFF,0x0FE0);
    Set(0x0001,0xFF);
    Set(0x000E,0xFF);
    Set(0x0000,0x04);
    ClearBreakpoints();
    AddBreakpoint(0x821f);
    while(!Run(5000)) {
      switch(GetPc()) {
        case 0x821f:
          GetMem(0x80,data,len);
          return a;
        }
      }
    }
  return -1;
}

// -- cN2Prov0511 ----------------------------------------------------------------

static cN2ProvLinkReg<cN2Prov0501,0x0511,(N2FLAG_MECM|N2FLAG_INV)> staticPL0511;

// -- cN2Prov1101 ----------------------------------------------------------------

static cN2ProvLinkReg<cN2Prov0501,0x1101,(N2FLAG_MECM|N2FLAG_INV)> staticPL1101;

// -- cN2Prov3101 ----------------------------------------------------------------

static cN2ProvLinkReg<cN2Prov0501,0x3101,(N2FLAG_MECM|N2FLAG_INV)> staticPL3101;

