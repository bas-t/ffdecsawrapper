
#include <stdlib.h>
#include <stdio.h>

#define TESTER
#include "data.h"
#include "scsetup.h"
#include "compat.h"

#include <libsi/section.h>
SI::TOT __dummy;

void cPlainKeys::TestExternalUpdate(void)
{
  Lock();
  Start();
  Unlock();
  while(Active()) {
   printf("."); fflush(stdout);
   cCondWait::SleepMs(600);
   }
}

int main(int argc, char *argv[])
{
  if(argc<3) {
    printf("usage: %s <lib-dir> <plugin-dir> <extau>\n",argv[0]);
    return 1;
    }
  DllsLoad(argv[1]);
  InitAll(argv[2]);
  LogAll();

  ScSetup.AutoUpdate=1;
  externalAU=argv[3];
  keys.TestExternalUpdate();
}
