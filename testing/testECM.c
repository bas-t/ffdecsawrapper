
#include <stdlib.h>
#include <stdio.h>

#include "data.h"
#include "system.h"
#include "compat.h"
#include <vdr/sources.h>

int main(int argc, char *argv[])
{
  if(argc<6) {
    printf("usage: %s <lib-dir> <plugin-dir> <caid> <provid> <source> <ecm-dump>\n",argv[0]);
    return 1;
    }
  DllsLoad(argv[1]);
  InitAll(argv[2]);
  LogAll();
  cLogging::SetModuleOption(LCLASS(7,0x20<<2),false); // Nagra L_SYS_DISASM
  cLogging::SetModuleOption(LCLASS(7,0x20<<4),false); // Nagra L_SYS_CPUSTATS
  unsigned char ecm[4096];
  ReadRaw(argv[6],ecm,sizeof(ecm));
    
  int caid=strtol(argv[3],0,0);
  int provid=strtol(argv[4],0,0);
  printf("using caid %04x provid %04x\n",caid,provid);
  cEcmInfo ecmD("dummy",0x123,caid,provid);
  ecmD.SetSource(10,cSource::FromString(argv[5]),120);
  cSystem *sys=0;
  int lastPri=0;
  while((sys=cSystems::FindBySysId(caid,false,lastPri))) {
    lastPri=sys->Pri();
    printf("processing with module '%s'\n",sys->Name());
    bool res=sys->ProcessECM(&ecmD,ecm);
    if(res) {
      printf("resulting CW: ");
      SDump(sys->CW(),16);
      }
    delete sys;
    if(res) break;
    }
}
