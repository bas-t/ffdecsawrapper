
#include <stdlib.h>
#include <stdio.h>

#include "crypto.h"
#include "data.h"
#include "system-common.h"
#include "systems/nagra/nagra2.h"

#include "compat.h"

static const unsigned char key3des[16] = {  };

static cN2Prov *emmP=0;

void Emm(unsigned char *emmdata, int cmdLen, int id)
{
  if(!emmP || (emmP && !emmP->CanHandle(id))) {
    delete emmP;
    emmP=cN2Providers::GetProv(id,N2FLAG_NONE);
    if(emmP) emmP->PrintCaps(L_SYS_EMM);
    }
  if(emmP) emmP->PostDecrypt(false);

  HEXDUMP(L_SYS_RAWEMM,emmdata,cmdLen,"Nagra2 RAWEMM");
  id=(emmdata[8]<<8)+emmdata[9];
  LBSTARTF(L_SYS_EMM);
  bool contFail=false;
  for(int i=8+2+4+4; i<cmdLen-22; ) {
    switch(emmdata[i]) {
      case 0x42: // plain Key update
        if((((emmdata[i+3]|0xF3)+1)&0xFF) != 0) {
          int len=emmdata[i+2];
          int off=emmdata[i+5];
          int ulen=emmdata[i+6];
          if(len>0 && ulen>0 && off+ulen<=len) {
            int ks=emmdata[i+3], kn;
            if(ks==0x06 || ks==0x46) kn=(ks>>6)&1; else kn=MBC(N2_MAGIC,ks);
            unsigned char key[256];
            memset(key,0,sizeof(key));
            bool ok=false;
            if((emmdata[i+1]&0x7F)==0) ok=true;
            else {
              if(emmP && emmP->HasFlags(N2FLAG_POSTAU)) {
                if(emmP->PostProcAU(id,&emmdata[i])) ok=true;
                }
              else PRINTF(L_SYS_EMM,"POSTAU for provider %04x not supported",id);
              }
            if(ok) {
              printf("key %x: off=%x ulen=%x ",kn,off,ulen);
              SDump(&emmdata[i+7],ulen);
              }
            i+=ulen;
            }
          else PRINTF(L_SYS_EMM,"nano42 key size mismatch len=%d off=%d ulen=%d",len,off,ulen);
          }
        else PRINTF(L_SYS_EMM,"nano42 0xf3 status exceeded");
        i+=7;
        break;
      case 0xE0: // DN key update
        if(emmdata[i+1]==0x25) {
          printf("key%02x: ",(emmdata[i+16]&0x40)>>6);
          SDump(&emmdata[i+23],16);
          }
        i+=emmdata[i+1]+2;
        break;
      case 0x83: // change data prov. id
        id=(emmdata[i+1]<<8)|emmdata[i+2];
        i+=3;
        break;
      case 0xB0: case 0xB1: case 0xB2: case 0xB3: // Update with ROM CPU code
      case 0xB4: case 0xB5: case 0xB6: case 0xB7:
      case 0xB8: case 0xB9: case 0xBA: case 0xBB:
      case 0xBC: case 0xBD: case 0xBE: case 0xBF:
        {
        int bx=emmdata[i]&15;
        if(!emmP || !emmP->HasFlags(N2FLAG_Bx)) {
          PRINTF(L_SYS_EMM,"B%X for provider %04x not supported",bx,id);
          i=cmdLen;
          break;
          }
        int r;
        if((r=emmP->ProcessBx(emmdata,cmdLen,i+1))>0)
          i+=r;
        else {
          PRINTF(L_SYS_EMM,"B%X executing failed for %04x",bx,id);
          i=cmdLen;
          }
        break;
        }
      case 0xA4: i+=emmdata[i+1]+2+4; break;	// conditional (always no match assumed for now)
      case 0xA6: i+=15; break;
      case 0xAA: i+=emmdata[i+1]+5; break;
      case 0xAD: i+=emmdata[i+1]+2; break;
      case 0xA2:
      case 0xAE: i+=11;break;
      case 0x12: i+=emmdata[i+1]+2; break;      // create tier
      case 0x20: i+=19; break;                  // modify tier
      case 0x9F: i+=6; break;
      case 0xE3:                                // Eeprom update
        {
        int ex=emmdata[i]&15;
        if(!emmP || !emmP->HasFlags(N2FLAG_Ex)) {
          i+=emmdata[i+4]+5;
          PRINTF(L_SYS_EMM,"E%X for provider %04x not supported",ex,id);
          break;
          }
        int r;
        if((r=emmP->ProcessEx(emmdata,cmdLen,i+1))>0)
          i+=r;
        else {
          PRINTF(L_SYS_EMM,"E%X executing failed for %04x",ex,id);
          i=cmdLen;
          }
        break;
        }
      case 0xE1:
      case 0xE2:
      case 0x00: i=cmdLen; break;		// end of processing
      default:
        if(!contFail) LBPUT("unknown EMM nano");
        LBPUT(" %02x",emmdata[i]);
        contFail=true;
        i++;
        continue;
      }
    LBFLUSH(); contFail=false;
    }
  LBEND();
}

bool Ecm(unsigned char *buff, int cmdLen, int id)
{
  unsigned char cw[16];

  cN2Prov *ecmP=cN2Providers::GetProv(id,N2FLAG_NONE);
  if(ecmP) ecmP->PrintCaps(L_SYS_ECM);
  if(ecmP) ecmP->PostDecrypt(true);

  int l=0, mecmAlgo=0;
  for(int i=(buff[14]&0x10)?16:20; i<cmdLen-10 && l!=3; ) {
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
  if(argc<5) {
    printf("usage: %s <plugin-dir> <id> <ECM/EMM> <raw-file> [<raw-file> [..]]\n",argv[0]);
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

  int id=strtol(argv[2],0,0);
  int dlen=(mode==1) ? 64 : 96;
  unsigned char data[4096];
  for(int pos=4; pos<argc; pos++) {
    int len=ReadRaw(argv[pos],data,sizeof(data));
    if(len%dlen != 0) { printf("bad raw file format\n"); exit(1); }

    for(int alen=0; alen<len; alen+=dlen) {
      if(mode==1) Ecm(&data[alen],dlen,id);
      else if(mode==2) Emm(&data[alen],dlen,id);
      }
    }
}
