
#include <stdlib.h>
#include <stdio.h>

#include "crypto.h"
#include "data.h"
#include "system-common.h"
#include "systems/nagra/nagra2.h"

#include "compat.h"

void printreg(const char *name, unsigned char * ptr, int maxlen)
{
  int i;
  while(maxlen > 1 && ptr[maxlen-1]==0)
    maxlen--;
  printf("%s(LE): ", name);
  for(i = 0; i < maxlen; i++)
    printf("%02x", ptr[i]);
  printf("\n");
}

int main(int argc, char *argv[])
{
  if(argc<4) {
    printf("usage: %s <plugin-dir> <id> <cmdfile> [LOG]\n",argv[0]);
    return 1;
    }

  InitAll(argv[1]);
  LogAll();
  if(argc<=4 || strcasecmp(argv[4],"LOG"))
    cLogging::SetModuleOption(LCLASS(L_SYS,L_SYS_DISASM),false);
  unsigned char data[4096];
  unsigned int len=ReadRaw(argv[3],data,sizeof(data));
  int id=strtol(argv[2],0,0);
  cN2Prov *emmP=cN2Providers::GetProv(id,N2FLAG_NONE);
  HEXDUMP(L_SYS_EMU,data,len,"Input");
  if(emmP->RunEmu(data,len,0x90,0x90,0x00,0x00,0x460)>=0)
    HEXDUMP(L_SYS_EMU,data,0x460,"Output");

  LogNone();
  static unsigned char dumpreg[4096] = {
    0x8A,0x9B,0xB6,0x0D,0x88,0x17,0x0D,0xA6, 0x11,0xB7,0x48,0x5F,0xD6,0x00,0xBA,0xB7,
    0x44,0x5C,0xD6,0x00,0xBA,0xB7,0x45,0x5C, 0xD6,0x00,0xBA,0x5C,0x89,0xCD,0x38,0x40,
    0x85,0xA3,0x0F,0x26,0xE7,0x84,0xB7,0x0D, 0x86,0x81,0x00,0xF0,0x09,0x01,0x00,0x0A,
    0x01,0x88,0x0B,0x02,0x10,0x0C,0x02,0x98, 0x0D};
  if(emmP->RunEmu(dumpreg,0x100,0x90,0x90,0x00,0x00,0x460)>=0) {
    printreg("J",dumpreg+0xf0, 0x08);
    printreg("A",dumpreg+0x100,0x88);
    printreg("B",dumpreg+0x188,0x88);
    printreg("C",dumpreg+0x210,0x88);
    printreg("D",dumpreg+0x298,0x88);
    }
}
