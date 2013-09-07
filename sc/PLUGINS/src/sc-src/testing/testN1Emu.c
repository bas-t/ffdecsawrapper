
#include <stdlib.h>
#include <stdio.h>

#define TESTER
#include "crypto.h"
#include "data.h"
#include "system-common.h"
#include "systems/nagra/cpu.c"
#include "systems/nagra/nagra.c"
#include "systems/nagra/nagra1.c"

#include "compat.h"

class cTestNagra : public cNagra1 {
public:
  virtual void Process(int id, unsigned char *emmdata);
  };

void cTestNagra::Process(int id, unsigned char *emmdata)
{
  cEmu *emu=0;
  cBN n1, e1;

  unsigned int i=0;
  bool gotKeys=false;
  unsigned char key0[8], key1[8];
  int keyId=(emmdata[i+1]<<8)+emmdata[i+2];
  switch(emmdata[i]) { // Check filter type
    case 0x00: // One card
      i+=7; break;
    case 0x01 ... 0x20: // Group of cards
      i+=emmdata[i]+0x7; break;
    case 0x3e:
      i+=6; break;
    case 0x3d: // All cards with same system ID
    case 0x3f:
      i+=3; break;
    }
  int nrKeys=0;
  while(i<sizeof(emmdata)) {
    if((emmdata[i]&0xF0)==0xF0) { // Update with CPU code
      const int romNr=emmdata[i]&0x0F;
      if(!emu || !emu->Matches(romNr,id)) {
        delete emu; emu=0;
        printf("Testing with ROM %d, 0xID %04x\n",romNr,id);
        switch(romNr) {
          case 3:  emu=new cEmuRom3;  break;
          case 7:  emu=new cEmuRom7;  break;
          case 10: emu=new cEmuRom10; break;
          case 11: emu=new cEmuRom11; break;
          default: printf("unsupported ROM"); return;
          }
        if(!emu || !emu->Init(romNr,id)) {
          delete emu; emu=0;
          printf("initialization failed for ROM %d\n",romNr);
          return;
          }
        }
      unsigned char ki[2];
      if((gotKeys=emu->GetOpKeys(emmdata,ki,key0,key1))) {
        printf("keyid: "); SDump(ki,2);
        printf("key0: "); SDump(key0,8);
        printf("key1: "); SDump(key1,8);
        keyId=(ki[0]<<8)+ki[1];
        printf("got keys for %04X (ROM %d)\n",keyId,romNr);
        }
      unsigned char select[3], pkset[3][15];
      select[0]=(keyId>>8)|0x01; // always high id for ECM RSA keys
      select[1]=keyId&0xFF;
      select[2]=0; // type 0
HexDump(select,3);
      if(emu->GetPkKeys(&select[0],&pkset[0][0])) {
        int pkKeyId=((select[0]<<8)+select[1]);
        printf("got PK keys for %04X (ROM %d)\n",pkKeyId,romNr);
HexDump(pkset[0],sizeof(pkset[0]));
HexDump(pkset[1],sizeof(pkset[1]));
HexDump(pkset[2],sizeof(pkset[2]));
        for(int i=0; i<3; i++) {
          CreateRSAPair(pkset[i],0,e1,n1);
          printf("keyE1 set=%d ",i); bprint(e1);
          printf("keyN1 set=%d ",i); bprint(n1);
          }
        }
      break; // don't process other nanos
      }
   else if(emmdata[i]==0x60) { // NULL nano
      i+=2;
      }
    else if(emmdata[i]==0x00) {
      i++;
      }
    else if(emmdata[i]==0x81) {
      i++;
      }
    else if(emmdata[i]==0x83) {
      keyId=(emmdata[i+1]<<8)+emmdata[i+2];
      i+=3;
      }
    else if(emmdata[i]==0x42) { // plain Key
      if(emmdata[i+1]==0x05) memcpy(key0,&emmdata[i+2],sizeof(key0));
      else                   memcpy(key1,&emmdata[i+2],sizeof(key1));
      i+=10;
      if(++nrKeys==2) {
        gotKeys=true;
        printf("got keys for %04X (plain)\n",keyId);
        break;
        }
      }
    else {
      printf("ignored nano %02x\n",emmdata[i]);
      break;
      }
    }

  delete emu;
}


int main(int argc, char *argv[])
{
  if(argc<3) {
    printf("usage: %s <plugin-dir> <id> <rawemm-file>\n",argv[0]);
    return 1;
    }

  InitAll(argv[1]);
  unsigned char emmdata[64];
  int emmlen=ReadRaw(argv[3],emmdata,sizeof(emmdata));
  if(emmlen!=64) {
    printf("bad rawemm file format\n");
    return 1;
    }
  const int id=strtol(argv[2],0,0);
  cTestNagra test;
  test.Process(id,emmdata);
  return 0;
}
