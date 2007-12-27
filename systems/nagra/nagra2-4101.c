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

// -- cN2Prov4101 ----------------------------------------------------------------

class cN2Prov4101 : public cN2Prov {
public:
  cN2Prov4101(int Id, int Flags):cN2Prov(Id,Flags) {}
  virtual bool PostProcAU(int id, unsigned char *data);
  };

static cN2ProvLinkReg<cN2Prov4101,0x4101,N2FLAG_POSTAU> staticPL4101;

bool cN2Prov4101::PostProcAU(int id, unsigned char *data)
{
  if(data[1]==0x01) {
    cPlainKey *pk;
    if(!(pk=keys.FindKey('N',id,MBC(N2_MAGIC,0x30),16))) {
      PRINTF(L_SYS_EMM,"missing %04x NN 30 3DES key (16 bytes)",id);
      return false;
      }
    unsigned char dkey[16];
    pk->Get(dkey);
    DES_key_schedule ks1, ks2;
    DES_key_sched((DES_cblock *)&dkey[0],&ks1);
    DES_key_sched((DES_cblock *)&dkey[8],&ks2);
    DES_ecb2_encrypt(DES_CAST(&data[7]),DES_CAST(&data[7]),&ks1,&ks2,DES_DECRYPT);
    DES_ecb2_encrypt(DES_CAST(&data[7+8]),DES_CAST(&data[7+8]),&ks1,&ks2,DES_DECRYPT);
    }
  return true;
}
