
#include <stdlib.h>
#include <stdio.h>

#include "data.h"
#include "system.h"
#include "log.h"
#include "compat.h"
#include <vdr/sources.h>

#include <libsi/section.h>
SI::TOT __dummy;

int main(int argc, char *argv[])
{
  if(argc<2) {
    printf("usage: %s <lib-dir> <plugin-dir>\n",argv[0]);
    return 1;
    }
  DllsLoad(argv[1]);
  InitAll(argv[2]);
  LogAll();
  cLogging::SetModuleOption(LCLASS(7,0x20<<2),false); // Nagra L_SYS_DISASM
  cLogging::SetModuleOption(LCLASS(7,0x20<<4),false); // Nagra L_SYS_CPUSTATS
  cLogging::SetModuleOption(LCLASS(16,0x20<<5),false); // Viacsess L_SYS_DISASM

  getchar();
  fflush(stdout);
  return 0;
}
