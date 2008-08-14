
#include <stdlib.h>
#include <stdio.h>

#include "data.h"
#include "system.h"
#include "compat.h"

#include <libsi/section.h>
SI::TOT __dummy;

int main(int argc, char *argv[])
{
  if(argc<4) {
    printf("usage: %s <lib-dir> <plugin-dir> <caid> <rawemm-file>\n",argv[0]);
    return 1;
    }
  DllsLoad(argv[1]);
  InitAll(argv[2]);
  LogAll();
  unsigned char emm[4096];
  int len=ReadRaw(argv[4],emm,sizeof(emm));
    
  int caid=strtol(argv[3],0,0);
  printf("using caid %04x\n",caid);
  cSystem *sys=0;
  int lastPri=0;
  while((sys=cSystems::FindBySysId(caid,false,lastPri))) {
    sys->DoLog(true); sys->CardNum(0);
    lastPri=sys->Pri();
    if(sys->HasLogger()) {
      for(int i=0; i<len;) {
        int s=SCT_LEN(&emm[i]);
        sys->ProcessEMM(0x123,caid,&emm[i]);
        i+=s;
        }
      }
    delete sys;
    }
}
