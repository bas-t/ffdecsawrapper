
#include <stdlib.h>
#include <stdio.h>

#define TESTER
#include "crypto.h"
#include "data.h"
#include "system-common.h"
#include "compat.h"

#include "systems/viaccess/viaccess.h"
#include "systems/viaccess/viaccess.c"

// ----------------------------------------------------------------

class cTransponderTime;

class cSatTimeHook : public cLogHook {
private:
public:
  cSatTimeHook(cTransponderTime *Ttime);
  ~cSatTimeHook();
  virtual void Process(int pid, unsigned char *data);
  };

cSatTimeHook::cSatTimeHook(cTransponderTime *Ttime)
:cLogHook(HOOK_SATTIME,"sattime")
{}

cSatTimeHook::~cSatTimeHook()
{}

void cSatTimeHook::Process(int pid, unsigned char *data)
{}

// ----------------------------------------------------------------

class cTpsAuHook : public cLogHook {
public:
  cTpsAuHook(void);
  virtual ~cTpsAuHook();
  virtual void Process(int pid, unsigned char *data);
  void DummyProcess(unsigned char *data, int size);
  };

#include "systems/viaccess/tps.c"
#include "systems/viaccess/st20.c"

cTpsAuHook::cTpsAuHook(void)
:cLogHook(HOOK_TPSAU,"tpsau")
{}

cTpsAuHook::~cTpsAuHook()
{}

void cTpsAuHook::Process(int pid, unsigned char *data)
{
}

void cTpsAuHook::DummyProcess(unsigned char *data, int size)
{
  tpskeys.Load(false);
  if(data && size) {
    cOpenTVModule mod(2,data,size);
    tpskeys.ProcessAu(&mod);
    }
  else {
    tpskeys.LoadBin();
    }
  tpskeys.Purge(time(0));
  tpskeys.Save();
}

// ----------------------------------------------------------------

int main(int argc, char *argv[])
{
  if(argc<3) {
    printf("usage: %s <plugin-dir> <TPSBIN|OTV> <decomp-bin>\n",argv[0]);
    return 1;
    }

  InitAll(argv[1]);
  LogAll();
  cLogging::SetModuleOption(LCLASS(L_SYS,L_SYS_DISASM),false);
  if(!strcasecmp(argv[2],"OTV")) {
    FILE *f=fopen(argv[3],"r");
    if(f) {
      fseek(f,0,SEEK_END);
      int size=ftell(f);
      fseek(f,0,SEEK_SET);
      unsigned char *data=(unsigned char *)malloc(size);
      if(fread(data,1,size,f)<=0) ;
      fclose(f);
      printf("read %d bytes from %s\n",size,argv[3]);

      cTpsAuHook hook;
      hook.DummyProcess(data,size);
      }
    }
  else if(!strcasecmp(argv[2],"TPSBIN")) {
    cTpsAuHook hook;
    hook.DummyProcess(0,0);
    }
  return 0;
}
