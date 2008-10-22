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

#include <openssl/md5.h>

#include "system-common.h"
#include "data.h"
#include "helper.h"

#include "nds.h"
#include "log-nds.h"

#define SYSTEM_NAME          "Fake-NDS"
#define SYSTEM_PRI           -12

// -- cSystemFakeNDS -----------------------------------------------------------

class cSystemFakeNDS : public cSystem {
private:
  cFileMap *fm;
  bool mapMD5ok;
  //
  bool MapFlash(const char *name, int len, const unsigned char *appmd);
public:
  cSystemFakeNDS(void);
  ~cSystemFakeNDS();
  virtual bool ProcessECM(const cEcmInfo *ecm, unsigned char *data);
  };

cSystemFakeNDS::cSystemFakeNDS(void)
:cSystem(SYSTEM_NAME,SYSTEM_PRI)
{
  fm=0; mapMD5ok=false;
  memset(cw,0,sizeof(cw));
}

cSystemFakeNDS::~cSystemFakeNDS()
{
  if(fm) fm->Unmap();
}

bool cSystemFakeNDS::MapFlash(const char *name, int len, const unsigned char *appmd)
{
  if(fm && !fm->IsFileMap(name,false)) { fm->Unmap(); fm=0; }
  if(!fm) fm=filemaps.GetFileMap(name,FILEMAP_DOMAIN,false);
  if(!fm) return false;
  if(!fm->Addr()) {
    if(!fm->Map()) return false;
    unsigned char md[16];
    MD5(fm->Addr(),fm->Size(),md);
    if(fm->Size()<len || memcmp(md,appmd,16)) {
      mapMD5ok=false;
      if(doLog) PRINTF(L_SYS_ECM,"MD5 checksum failed on flash bin");
      return false;
      }
    mapMD5ok=true;
    }
  return mapMD5ok;
}

bool cSystemFakeNDS::ProcessECM(const cEcmInfo *ecm, unsigned char *data)
{
  int len=SCT_LEN(data);
  if(len<7 || data[3]!=0x00 || data[4]!=0x00 || data[5]!=0x01) {
    if(doLog) PRINTF(L_SYS_ECM,"ECM format check failed");
    return false;
    }
  int iLen=data[6];
  if(iLen+6>len || iLen<0x1C) {
    if(doLog) PRINTF(L_SYS_ECM,"IRD block length check failed");
    return false;
    }

  int block1, block2, block3, block4, cwoff;
  if(iLen==0x1C && data[6+11]==0x0F && data[6+12]==0x40) { // PW
    // check cw in IRD block
    if(!(data[6+9]&1) || !(data[6+10]&16)) {
      if(doLog) PRINTF(L_SYS_ECM,"no CW in IRD block");
      return false;
      }
    static const unsigned char app10039[] = { 0xff,0x25,0x38,0x51,0xec,0xcb,0xc8,0xfa,0xab,0x97,0x59,0x15,0x4d,0x14,0x05,0xfa };
    if(!MapFlash("PR-HD1000_060000.bin",0x2603c0,app10039)) return false;
    block1=1;
    block2=23;
    block3=0x22D344;
    block4=0x25c620;
    cwoff=14;
    }
  else if(iLen==0x21 && data[6+14]==0x0F && data[6+15]==0x40) { // VIASAT
    static const unsigned char apppace[] = { 0x23,0x21,0xc3,0xe0,0xc1,0xce,0xed,0xb4,0x4b,0xd6,0xa4,0xc7,0x44,0x61,0xda,0x0e };
    if(!MapFlash("VIA-Pace4_060000.bin",0x2BA600,apppace)) return false;
    block1=1;
    block2=26;
    block3=0x28cb08;
    block4=0x2b5318;
    cwoff=17;
    }
  else {
    if(doLog) PRINTF(L_SYS_ECM,"failed to detect provider");
    return false;
    }

  unsigned char hash[94], md[16];
  memcpy(&hash[0],&data[6+block1],10);
  memcpy(&hash[10],&data[6+block2],4);
  memcpy(&hash[14],fm->Addr()+block3,64);
  memcpy(&hash[78],fm->Addr()+block4,16);
  MD5(hash,94,md);
  xxor(cw+((data[0]&1)<<3),8,&data[6+cwoff],&md[8]);
  return true;
}

// -- cSystemLinkFakeNDS -------------------------------------------------------

class cSystemLinkFakeNDS : public cSystemLink {
public:
  cSystemLinkFakeNDS(void);
  virtual bool CanHandle(unsigned short SysId);
  virtual cSystem *Create(void) { return new cSystemFakeNDS; }
  };

static cSystemLinkFakeNDS staticInit;

cSystemLinkFakeNDS::cSystemLinkFakeNDS(void)
:cSystemLink(SYSTEM_NAME,SYSTEM_PRI)
{}

bool cSystemLinkFakeNDS::CanHandle(unsigned short SysId)
{
  return (SysId&SYSTEM_MASK)==SYSTEM_NDS;
}
