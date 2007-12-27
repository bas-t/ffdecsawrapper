
#include <stdlib.h>
#include <stdio.h>

#include "data.h"
#include "system.h"
#include "compat.h"

int main(int argc, char *argv[])
{
  if(argc<5) {
    printf("usage: %s <lib-dir> <plugin-dir> <caid> <provid> <ecm-dump>\n",argv[0]);
    return 1;
    }
  DllsLoad(argv[1]);
  InitAll(argv[2]);
  unsigned char ecm[4096];
  ReadRaw(argv[5],ecm,sizeof(ecm));
    
  int caid=strtol(argv[3],0,0);
  int provid=strtol(argv[4],0,0);
  printf("using caid %04x provid %04x\n",caid,provid);
  cEcmInfo ecmD("dummy",0x123,caid,provid);
  cSystem *sys=0;
  int lastPri=0;
  while((sys=cSystems::FindBySysId(caid,false,lastPri))) {
    lastPri=sys->Pri();
    printf("processing with module '%s'\n",sys->Name());
    bool res=sys->ProcessECM(&ecmD,ecm);
    delete sys;
    if(res) break;
    }
}
