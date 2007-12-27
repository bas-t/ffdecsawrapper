
#include <stdlib.h>
#include <stdio.h>

#define TESTER
#include "crypto.h"
#include "data.h"
#include "system-common.h"
#include "systems/nagra/cpu.c"
#include "systems/nagra/nagra2.c"
#include "systems/nagra/nagra.c"

#include "compat.h"

static const unsigned char key3des[16] = {  };

void Emm(unsigned char *emmdata, int cmdLen, int id)
{
  cN2Prov *emmP=cN2Providers::GetProv(id,N2FLAG_NONE);
  if(emmP) printf("provider %04x capabilities%s%s%s%s\n",id,
                    emmP->HasFlags(N2FLAG_MECM)    ?" MECM":"",
                    emmP->HasFlags(N2FLAG_Bx)      ?" Bx":"",
                    emmP->HasFlags(N2FLAG_POSTAU)  ?" POSTPROCAU":"",
                    emmP->HasFlags(N2FLAG_INV)     ?" INVCW":"");

  for(int i=8+2+4+4; i<cmdLen-22; ) {
printf("%02x: nano %02x\n",i,emmdata[i]);
    switch(emmdata[i]) {
      case 0x42: // plain Key update
        if(emmdata[i+2]==0x10 && (emmdata[i+3]&0xBF)==0x06 &&
           (emmdata[i+4]&0xF8)==0x08 && emmdata[i+5]==0x00 && emmdata[i+6]==0x10) {
          if(!emmP || emmP->PostProcAU(id,&emmdata[i])) {
            printf("key%02x: ",(emmdata[i+3]&0x40)>>6);
            SDump(&emmdata[i+7],16);
            }
          }
        i+=23;
        break;
      case 0xE0: // DN key update
        if(emmdata[i+1]==0x25) {
          printf("key%02x: ",(emmdata[i+16]&0x40)>>6);
          SDump(&emmdata[i+23],16);
          }
        i+=39;
        break;
      case 0x83: // change data prov. id
        id=(emmdata[i+1]<<8)|emmdata[i+2];
        printf("keyid: %04x\n",id);
        i+=3;
        break;
      case 0xA4: // conditional (always no match assumed for now)
        i+=emmdata[i+1]+2+4;
        break;
      case 0xA6:
        i+=emmdata[i+1]+1;
        break;
      case 0x13:
      case 0x14:
      case 0x15:
        i+=4;
        break;
      case 0xB0 ... 0xBF: // Update with ROM CPU code
        {
        int bx=emmdata[i]&15;
        if(!emmP || !emmP->HasFlags(N2FLAG_Bx)) {
          printf("B%X for provider %04x not supported\n",bx,id);
          i=cmdLen;
          break;
          }
        int r;
        if((r=emmP->ProcessBx(emmdata,cmdLen,i+1))>0)
          i+=r;
        else {
          printf("B%X executing failed for %04x\n",bx,id);
          i=cmdLen;
          }
        break;
        }
      case 0xE3: // Eeprom update
        i+=emmdata[i+4]+4;
        break;
      case 0xE1:
      case 0xE2:
      case 0x00: // end of processing
        i=cmdLen;
        break;
      default:
        i++;
        continue;
      }
    }
}

bool Ecm(unsigned char *buff, int cmdLen, int id)
{
  unsigned char cw[16];

  cN2Prov *ecmP=cN2Providers::GetProv(id,N2FLAG_NONE);
  if(ecmP) ecmP->PrintCaps(L_SYS_ECM);

  int l=0, mecmAlgo=0;
  for(int i=16; i<cmdLen-10 && l!=3; ) {
printf("%02x: nano %02x\n",i,buff[i]);
    switch(buff[i]) {
      case 0x10:
      case 0x11:
        if(buff[i+1]==0x09) {
          int s=(~buff[i])&1;
          mecmAlgo=buff[i+2]&0x60;
          memcpy(cw+(s<<3),&buff[i+3],8);
          i+=11; l|=(s+1);
          }
        else {
          printf("bad length %d in CW nano %02x\n",buff[i+1],buff[i]);
          i++;
          }
        break;
      case 0x00:
        i+=2; break;
      case 0x13 ... 0x15:
        i+=4; break;
      case 0x30 ... 0x36:
      case 0xB0:
        i+=buff[i+1]+2;
        break;
      default:
        i++;
        continue;
      }
    }
  if(l!=3) return false;
  if(mecmAlgo>0) {
    if(ecmP && ecmP->HasFlags(N2FLAG_MECM)) {
static unsigned char odata[15] = { 0x80,0x30,0x47,0x07,0x45,0xec,0xc1,0xee,0xc5,0x53,0x91,0x2f,0x70,0x34,0x34 };
      if(!ecmP->MECM(buff[15],mecmAlgo,odata,cw)) return false;
      }
    else { printf("MECM for provider %04x not supported\n",id); return false; }
    }
  if(ecmP) ecmP->SwapCW(cw);
  printf("resulting CW: "); SDump(cw,16);
  return true;
}

int main(int argc, char *argv[])
{
  if(argc<4) {
    printf("usage: %s <plugin-dir> <id> <ECM/EMM> <raw-file>\n",argv[0]);
    return 1;
    }

  int mode;
  if(!strcasecmp(argv[3],"ECM")) mode=1;
  else if(!strcasecmp(argv[3],"EMM")) mode=2;
  else {
    printf("bad mode '%s'\n",argv[3]);
    return 1;
    }
  
  InitAll(argv[1]);
  LogAll();
  cLogging::SetModuleOption(LCLASS(L_SYS,L_SYS_DISASM),false);
  unsigned char data[256];
  int len=ReadRaw(argv[4],data,sizeof(data));
  if((mode==1 && len!=64) || (mode==2 && len!=96)) {
    printf("bad raw file format\n");
    return 1;
    }

  int id=strtol(argv[2],0,0);
  if(mode==1) Ecm(data,len,id);
  else if(mode==2) Emm(data,len,id);
}
