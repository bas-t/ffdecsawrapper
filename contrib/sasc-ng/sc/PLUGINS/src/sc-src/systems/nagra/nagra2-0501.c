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

#include "nagra2.h"

// -- cMap0501 -----------------------------------------------------------------

class cMap0501 : public cMapCore {
private:
protected:
  virtual bool Map(int f, unsigned char *data, int l);
public:
  cMap0501(void);
  };

cMap0501::cMap0501(void)
{
}

bool cMap0501::Map(int f, unsigned char *data, int l)
{
  switch(f) {
    case 0x37:
      l=(l?l:wordsize)<<3;
      H.GetLE(data,l);
      MonMul(B,H,A);
      break;
    case 0x3a:
      MakeJ();
      BN_zero(R);
      BN_set_bit(R,68*wordsize);
      BN_mod(H,R,D,ctx);
      for(int i=0; i<4; i++) MonMul(H,H,H);
      MonMul(B,A,H);
      MonMul(B,A,B);
      break;
    default:
	  return false;
    }
  return true;
}

// -- cN2Prov0501 --------------------------------------------------------------

class cN2Prov0501 : public cN2Prov, private cMap0501 {
protected:
  virtual int Algo(int algo, const unsigned char *hd, const unsigned char *ed, unsigned char *hw, const unsigned char *cw);
  virtual bool NeedsCwSwap(void) { return true; }
public:
  cN2Prov0501(int Id, int Flags):cN2Prov(Id,Flags) { seedSize=3; SetMapIdent(Id); }
  };

static cN2ProvLinkReg<cN2Prov0501,0x0501,N2FLAG_MECM|N2FLAG_INV> staticPL0501;

int cN2Prov0501::Algo(int algo, const unsigned char *hd, const unsigned char *ed, unsigned char *hw, const unsigned char *cw)
{
  if(algo==0x60) {
    hw[0]=hd[0];
    hw[1]=hd[1];
    hw[2]=hd[2]&0xF8;
    ExpandInput(hw);
    hw[63]|=0x80;
    hw[95]=hw[127]=hw[95]&0x7F;
    SetWordSize(4);
    ImportReg(IMPORT_J,hw+0x18);
    ImportReg(IMPORT_D,hw+0x20);
    ImportReg(IMPORT_A,hw+0x60);
    DoMap(0x37,hw+0x40);
    ExportReg(EXPORT_C,hw);
    DoMap(0x3a);
    ExportReg(EXPORT_C,hw+0x20);
    DoMap(0x43);
    DoMap(0x44,hw);
    hw[0]&=7;
    ExportReg(EXPORT_B,hw+3);
    memset(hw+3+0x20,0,128-(3+0x20));
    return MARV_SUCCESS;
    }

  PRINTF(L_SYS_MAP,"0501: unknown MECM algo %02x",algo);
  return MARV_ERROR;
}

