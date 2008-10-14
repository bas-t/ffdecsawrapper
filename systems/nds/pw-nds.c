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

#define SYSTEM_NAME          "PW-NDS"
#define SYSTEM_PRI           -12

// -- cSystemPwNDS ---------------------------------------------------------------

class cSystemPwNDS : public cSystem {
private:
  cFileMap *fm;
public:
  cSystemPwNDS(void);
  ~cSystemPwNDS();
  virtual bool ProcessECM(const cEcmInfo *ecm, unsigned char *data);
  };

cSystemPwNDS::cSystemPwNDS(void)
:cSystem(SYSTEM_NAME,SYSTEM_PRI)
{
  fm=0;
  memset(cw,0,sizeof(cw));
}

cSystemPwNDS::~cSystemPwNDS()
{
  if(fm) fm->Unmap();
}

bool cSystemPwNDS::ProcessECM(const cEcmInfo *ecm, unsigned char *data)
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

  // check cw in IRD block
  if(!(data[6+9]&1) || !(data[6+10]&16)) {
    if(doLog) PRINTF(L_SYS_ECM,"no CW in IRD block");
    return false;
    }

  if(!fm) fm=filemaps.GetFileMap("PR-HD1000_060000.bin",FILEMAP_DOMAIN,false);
  if(!fm || (!fm->Addr() && !fm->Map())) return false;
  unsigned char *m=fm->Addr();

  unsigned char md[16];
  MD5(m,fm->Size(),md);
  const unsigned char app10039[] = { 0xff,0x25,0x38,0x51,0xec,0xcb,0xc8,0xfa,0xab,0x97,0x59,0x15,0x4d,0x14,0x05,0xfa };
  if(fm->Size()<0x2603c0 || memcmp(md,app10039,16)) {
    if(doLog) PRINTF(L_SYS_ECM,"MD5 checksum failed on flash bin");
    return false;
    }

  unsigned char hash[94];
  memcpy(&hash[0],&data[6+1],10);
  memcpy(&hash[10],&data[6+23],4);
  memcpy(&hash[14],m+0x22D344,64);
  memcpy(&hash[78],m+0x25c620,16);
  MD5(hash,94,md);
  xxor(cw+((data[0]&1)<<3),8,&data[6+14],&md[8]);
  return true;
}

// -- cSystemLinkPwNDS ---------------------------------------------------------

class cSystemLinkPwNDS : public cSystemLink {
public:
  cSystemLinkPwNDS(void);
  virtual bool CanHandle(unsigned short SysId);
  virtual cSystem *Create(void) { return new cSystemPwNDS; }
  };

static cSystemLinkPwNDS staticInit;

cSystemLinkPwNDS::cSystemLinkPwNDS(void)
:cSystemLink(SYSTEM_NAME,SYSTEM_PRI)
{}

bool cSystemLinkPwNDS::CanHandle(unsigned short SysId)
{
  return (SysId&SYSTEM_MASK)==SYSTEM_NDS;
}
