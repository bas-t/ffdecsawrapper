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

#include "system-common.h"
#include "data.h"
#include "parse.h"
#include "helper.h"
#include "misc.h"

#include <openssl/des.h>
#include "openssl-compat.h"

#include "irdeto.h"
#include "log-irdeto.h"

// -- cIrdeto2 -----------------------------------------------------------------

class cIrdeto2 {
private:
  DES_key_schedule ks1, ks2;
  //
  void ScheduleKey(const unsigned char *key);
  void DES3(unsigned char *data, int mode);
protected:
  void Encrypt(unsigned char *data, const unsigned char *seed, const unsigned char *key, int len);
  void Decrypt(unsigned char *data, const unsigned char *seed, const unsigned char *key, int len);
  bool CalculateHash(const unsigned char *key, const unsigned char *iv, const unsigned char *data, int len);
  };

void cIrdeto2::ScheduleKey(const unsigned char *key)
{
  DES_key_sched((DES_cblock *)key,&ks1);
  DES_key_sched((DES_cblock *)(key+8),&ks2);
}

void cIrdeto2::DES3(unsigned char *data, int mode)
{
  int m1, m2;
  if(mode) { m1=DES_DECRYPT; m2=DES_ENCRYPT; }
  else     { m1=DES_ENCRYPT; m2=DES_DECRYPT; }
  DES_ecb_encrypt((DES_cblock *)data,(DES_cblock *)data,&ks1,m1);
  DES_ecb_encrypt((DES_cblock *)data,(DES_cblock *)data,&ks2,m2);
  DES_ecb_encrypt((DES_cblock *)data,(DES_cblock *)data,&ks1,m1);
}

void cIrdeto2::Encrypt(unsigned char *data, const unsigned char *seed, const unsigned char *key, int len)
{
  ScheduleKey(key);
  len&=~7;
  const unsigned char *tmp=seed;
  for(int i=0; i<len; i+=8) {
    xxor(&data[i],8,&data[i],tmp); tmp=&data[i];
    DES3(&data[i],0);
    }
}

void cIrdeto2::Decrypt(unsigned char *data, const unsigned char *seed, const unsigned char *key, int len)
{
  ScheduleKey(key);
  len&=~7;
  unsigned char buf[2][8];
  int n=0;
  memcpy(buf[n],seed,8);
  for(int i=0; i<len; i+=8,data+=8,n^=1) {
    memcpy(buf[1-n],data,8);
    DES3(data,1);
    xxor(data,8,data,buf[n]);
    }
}

bool cIrdeto2::CalculateHash(const unsigned char *key, const unsigned char *iv, const unsigned char *data, int len)
{
  ScheduleKey(key);
  unsigned char cbuff[8];
  memset(cbuff,0,sizeof(cbuff));
  len-=8;
  for(int y=0; y<len; y+=8) {
    if(y<len-8) {
      xxor(cbuff,8,cbuff,&data[y]);
      LDUMP(L_SYS_VERBOSE,cbuff,8,"3DES XOR in:");
      }
    else {
      int l=len-y;
      xxor(cbuff,l,cbuff,&data[y]);
      xxor(cbuff+l,8-l,cbuff+l,iv+8);
      LDUMP(L_SYS_VERBOSE,cbuff,8,"3DES XOR(%d) in:",8-l);
      }
    DES3(cbuff,0);
    LDUMP(L_SYS_VERBOSE,cbuff,8,"3DES out:");
    }
  LDUMP(L_SYS_VERBOSE,cbuff,8,"CryptBuffer:");
  LDUMP(L_SYS_VERBOSE,&data[len],8,"MACBuffer:");
  return memcmp(cbuff,&data[len],8)==0;
}

// -- cSystemIrd2 --------------------------------------------------------------

#define NANOLEN(_a) ((_a) ? ((_a)&0x3F)+2 : 1)

class cSystemIrd2 : public cSystem, private cIrdeto2 {
private:
  void PrepareSeed(unsigned char *seed, const unsigned char *key);
  void NanoDecrypt(unsigned char *data, int i, int len, const unsigned char *key, const unsigned char *iv);
public:
  cSystemIrd2(void);
  virtual bool ProcessECM(const cEcmInfo *ecm, unsigned char *data);
  virtual void ProcessEMM(int pid, int caid, const unsigned char *data);
  };

cSystemIrd2::cSystemIrd2(void)
:cSystem(SYSTEM_NAME2,SYSTEM_PRI2)
{
  hasLogger=true;
}

void cSystemIrd2::PrepareSeed(unsigned char *seed, const unsigned char *key)
{
  unsigned char blank[16];
  memset(blank,0,16);
  Encrypt(seed,blank,key,16);
}

void cSystemIrd2::NanoDecrypt(unsigned char *data, int i, int len, const unsigned char *key, const unsigned char *iv)
{
  while(i<len) {
    int l=NANOLEN(data[i+1]);
    switch(data[i]) {
      case 0x10:
      case 0x50: if(l==0x13 && i<=len-l) Decrypt(&data[i+3],iv,key,16); break;
      case 0x78: if(l==0x14 && i<=len-l) Decrypt(&data[i+4],iv,key,16); break;
      }
    i+=l;
    }
}

bool cSystemIrd2::ProcessECM(const cEcmInfo *ecm, unsigned char *data)
{
  int len=data[11];
  if(len!=0x28 || SCT_LEN(data)<len+12) {
    if(doLog) PRINTF(L_SYS_ECM,"bad ECM length");
    return false;
    }
  int prov=data[8];
  cPlainKey *pk;
  unsigned char ECM_IV[16];
  if(!(pk=keys.FindKey('I',ecm->caId,KEYSET(prov,TYPE_IV,0),16))) {
    if(doLog) PRINTF(L_SYS_KEY,"missing %04x %02x IV key",ecm->caId,prov);
    return false;
    }
  pk->Get(ECM_IV);

  unsigned char ECM_Seed[16];
  if(!(pk=keys.FindKey('I',ecm->caId,KEYSET(prov,TYPE_SEED,0),16))) {
    if(doLog) PRINTF(L_SYS_KEY,"missing %04x %02x ECM key",ecm->caId,prov);
    return false;
    }
  pk->Get(ECM_Seed);

  cKeySnoop ks(this,'I',ecm->caId,KEYSET(prov,TYPE_OP,data[9]));
  unsigned char key[16];
  if(!(pk=keys.FindKey('I',ecm->caId,KEYSET(prov,TYPE_OP,data[9]),16))) return false;
  pk->Get(key);
  PrepareSeed(ECM_Seed,key);

  data+=12;
  Decrypt(data,ECM_IV,ECM_Seed,len);
  int i=(data[0]&7)+1;
  NanoDecrypt(data,i,len-8,key,ECM_IV);
  if(CalculateHash(ECM_Seed,ECM_IV,data-6,len+6)) {
    HEXDUMP(L_SYS_RAWECM,data-12,len+12,"Irdeto2 RAWECM");
    while(i<len-8) {
      int l=NANOLEN(data[i+1]);
      switch(data[i]) {
        case 0x78:
          memcpy(cw,&data[i+4],16);
          ks.OK(pk);
          return true;
        }
      i+=l;
      }
    }
  else {
    if(doLog) PRINTF(L_SYS_ECM,"hash failed");
    }
  return false;
}

void cSystemIrd2::ProcessEMM(int pid, int caid, const unsigned char *data)
{
  int prov=0; //XXX how to get provider here??

  int len=SCT_LEN(data);
  unsigned char *emm=AUTOMEM(len);

  cPlainKey *pk;
  if(!(pk=keys.FindKey('I',caid,KEYSET(prov,TYPE_IV,0),16))) {
    PRINTF(L_SYS_EMM,"missing %04x %02x IV key",caid,prov);
    return;
    }
  unsigned char EMM_IV[16];
  pk->Get(EMM_IV);

  for(int keyno=0; keyno<2; keyno++) {
    if(!(pk=keys.FindKey('I',caid,KEYSET(prov,TYPE_PMK,keyno),16))) {
      PRINTF(L_SYS_EMM,"missing %04x %02x MK%d key",caid,prov,keyno);
      continue;
      }
    unsigned char PMK[16];
    pk->Get(PMK);

    if(!(pk=keys.FindKey('I',caid,KEYSET(prov,TYPE_SEED,1),16))) {
      PRINTF(L_SYS_EMM,"missing %04x %02x EMM key",caid,prov);
      return;
      }
    unsigned char EMM_Seed[16];
    pk->Get(EMM_Seed);
    PrepareSeed(EMM_Seed,PMK);

    memcpy(emm,data,len);
    Decrypt(&emm[10],EMM_IV,EMM_Seed,len-10);
    NanoDecrypt(emm,16,len-8,PMK,EMM_IV);
    memmove(emm+6,emm+7,len-7); // removing padding byte
    if(CalculateHash(EMM_Seed,EMM_IV,emm+3,len-4)) {
      HEXDUMP(L_SYS_RAWEMM,emm,len,"Irdeto2 RAWEMM");
      for(int i=15; i<len-9;) {
        int l=NANOLEN(emm[i+1]);
        switch(emm[i]) {
          case 0x10:
          case 0x50:
            if(l==0x13 && i+l<=len-9) {
              FoundKey();
              if(keys.NewKey('I',caid,KEYSET(prov,TYPE_OP,emm[i+2]>>2),&emm[i+3],16)) NewKey();
              }
            break;
          }
        i+=l;
        }
      break;
      }
    else
      PRINTF(L_SYS_EMM,"hash failed MK%d",keyno);
    }
}

// -- cSystemLinkIrd -----------------------------------------------------------

class cSystemLinkIrd2 : public cSystemLink {
public:
  cSystemLinkIrd2(void);
  virtual bool CanHandle(unsigned short SysId);
  virtual cSystem *Create(void) { return new cSystemIrd2; }
  };

static cSystemLinkIrd2 staticInit2;

cSystemLinkIrd2::cSystemLinkIrd2(void)
:cSystemLink(SYSTEM_NAME2,SYSTEM_PRI2)
{
  Feature.NeedsKeyFile();
}

bool cSystemLinkIrd2::CanHandle(unsigned short SysId)
{
  return SysId!=SYSTEM_IRDETO && (SysId&SYSTEM_MASK)==SYSTEM_IRDETO;
}
