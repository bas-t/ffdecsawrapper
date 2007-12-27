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

// -- cN2Prov1101 ----------------------------------------------------------------

class cN2Prov1101 : public cN2Prov {
protected:
  virtual bool NeedsCwSwap(void) { return true; }
public:
  cN2Prov1101(int Id, int Flags):cN2Prov(Id,Flags) {}
  };

static cN2ProvLinkReg<cN2Prov1101,0x1101,N2FLAG_INV> staticPL1101;
