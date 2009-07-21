
#include <stdlib.h>
#include <stdio.h>

#include "crypto.h"
#include "data.h"
#include "system-common.h"
#include "systems/nagra/nagra2.h"

#include "compat.h"

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
}
