
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
    printf("usage: %s <plugin-dir> <id> <cmd>\n",argv[0]);
    return 1;
    }

  InitAll(argv[1]);
  LogAll();
  cLogging::SetModuleOption(LCLASS(L_SYS,L_SYS_DISASM),false);
  unsigned char data[1024];
  unsigned int len = 0;
  char *p = argv[3];
  char last = -1;
  while(p < argv[3]+strlen(argv[3]) && len < sizeof(data)) {
    if(*p == ' ' || *p == '\t') {
      if(last>=0) { printf("failed to read data\n"); return 1; }
      p++;
      continue;
      }
    unsigned char b;
    if(*p >= '0' && *p <= '9')      b = *p - '0';
    else if(*p >= 'a' && *p <= 'f') b = *p - 'a' + 10;
    else if(*p >= 'A' && *p <= 'F') b = *p - 'A' + 10;
    else { printf("failed to read data\n"); return 1; }
    if(last<0) last = b;
    else {
      data[len++] = (last << 4) | b;
      last = -1;
      }
    p++;
    }
  int id=strtol(argv[2],0,0);
  cN2Prov *emmP=cN2Providers::GetProv(id,N2FLAG_NONE);
  HEXDUMP(0, data, len, "Input");
  if(emmP->RunEmu(data, len, 0x90, 0x90, 0x00, 0x00, 0x300)>=0)
    HEXDUMP(0, data, 0x300, "Output");
}
