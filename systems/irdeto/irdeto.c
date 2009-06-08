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
#include <ctype.h>

#include "system-common.h"

#include "irdeto.h"
#include "log-irdeto.h"
#include "version.h"

SCAPIVERSTAG();

static const struct LogModule lm_sys = {
  (LMOD_ENABLE|L_SYS_ALL)&LOPT_MASK,
  (LMOD_ENABLE|L_SYS_DEFDEF)&LOPT_MASK,
  "irdeto",
  { L_SYS_DEFNAMES,"rawemm","rawecm" }
  };
ADD_MODULE(L_SYS,lm_sys)

// -- cPlainKeyIrd -------------------------------------------------------------

#define I1_KEYLEN 8
#define I2_KEYLEN 16

class cPlainKeyIrd : public cHexKey {
private:
  bool IsI2Key(void);
protected:
  virtual int IdSize(void);
  virtual cString PrintKeyNr(void);
public:
  cPlainKeyIrd(bool Super);
  virtual bool Parse(const char *line);
  virtual bool SetKey(void *Key, int Keylen);
  virtual bool SetBinKey(unsigned char *Mem, int Keylen);
  };

static cPlainKeyTypeReg<cPlainKeyIrd,'I'> KeyReg;

cPlainKeyIrd::cPlainKeyIrd(bool Super)
:cHexKey(Super)
{}

bool cPlainKeyIrd::SetKey(void *Key, int Keylen)
{
  if(!IsI2Key()) SetSupersede(false);
  return cHexKey::SetKey(Key,Keylen);
}

bool cPlainKeyIrd::SetBinKey(unsigned char *Mem, int Keylen)
{
  if(!IsI2Key()) SetSupersede(false);
  return cHexKey::SetBinKey(Mem,Keylen);
}

int cPlainKeyIrd::IdSize(void)
{
  return IsI2Key() ? 4 : 2;
}

bool cPlainKeyIrd::IsI2Key(void)
{
  return TYPE(keynr)!=TYPE_I1;
}

bool cPlainKeyIrd::Parse(const char *line)
{
  unsigned char sid[2], skey[I2_KEYLEN];
  int klen;
  if(GetChar(line,&type,1) && (klen=GetHex(line,sid,2,false))) {
     type=toupper(type); id=Bin2Int(sid,klen);
     line=skipspace(line);
     bool ok=false;
     if(klen==2) {
       klen=I2_KEYLEN;
       unsigned char prov;
       if(GetHex(line,&prov,1)) {
         line=skipspace(line);
         int typ, id;
         if(!strncasecmp(line,"IV",2)) {       typ=TYPE_IV; id=0; line+=2; ok=true; }
         else if(!strncasecmp(line,"ECM",3)) { typ=TYPE_SEED; id=0; line+=3; ok=true; }
         else if(!strncasecmp(line,"EMM",3)) { typ=TYPE_SEED; id=1; line+=3; ok=true; }
         else if(!strncasecmp(line,"MK",2)) {  typ=TYPE_PMK; id=line[2]-'0'; line+=3; ok=true; }
         else { typ=TYPE_OP; ok=GetHex(line,sid,1); id=sid[0]; }
         keynr=KEYSET(prov,typ,id);
         }
       }
     else {
       klen=I1_KEYLEN;
       ok=GetHex(line,sid,1);
       keynr=KEYSET(0,TYPE_I1,sid[0]);
       }
     line=skipspace(line);
     if(ok && GetHex(line,skey,klen)) {
       SetBinKey(skey,klen);
       return true;
       }
    }
  return false;
}

cString cPlainKeyIrd::PrintKeyNr(void)
{
  char tmp[16];
  if(IsI2Key()) {
    snprintf(tmp,sizeof(tmp),"%02X ",PROV(keynr));
    switch(TYPE(keynr)) {
      case TYPE_OP:   snprintf(tmp+3,sizeof(tmp)-3,"%02X",ID(keynr)); break;
      case TYPE_IV:   snprintf(tmp+3,sizeof(tmp)-3,"IV"); break;
      case TYPE_SEED: snprintf(tmp+3,sizeof(tmp)-3,"%s",ID(keynr)?"EMM":"ECM"); break;
      case TYPE_PMK:  snprintf(tmp+3,sizeof(tmp)-3,"MK%d",ID(keynr)); break;
      }
    }
  else
    snprintf(tmp,sizeof(tmp),"%02X",ID(keynr));
  return tmp;
}
