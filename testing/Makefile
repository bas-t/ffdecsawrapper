
### The directory environment:

VDRDIR = ../../../..

### The C++ compiler and options:

CXX      ?= g++
CXXFLAGS ?= -g -march=pentium3 -O2 -Wall -Woverloaded-virtual

### Includes and Defines

INCLUDES = -I.. -I$(VDRDIR)/include
DEFINES  = -DAPIVERSNUM=$(APIVERSNUM) -DAPIVERSION='"$(APIVERSION)"' -D_GNU_SOURCE

### Allow user defined options to overwrite defaults:

-include $(VDRDIR)/Make.config
-include ../Make.config

### The version number of VDR (taken from VDR's "config.h"):

APIVERSION = $(shell sed -ne '/define APIVERSION/ s/^.*"\(.*\)".*$$/\1/p' $(VDRDIR)/include/vdr/config.h)
APIVERSNUM = $(shell sed -ne '/define APIVERSNUM/ s/^.[a-zA-Z ]*\([0-9]*\) .*$$/\1/p' $(VDRDIR)/include/vdr/config.h)

### VDR version dependant

# test VDR version
BYVERS = $(strip $(shell if test $(APIVERSNUM) -ge 010703; then echo "*"; fi))
# test if PlayTsVideo() exists (e.g. TSplay patch)
BYTSPL = $(strip $(shell grep -l 'PlayTsVideo' $(VDRDIR)/include/vdr/device.h))

ifneq ($(BYVERS)$(BYTSPL),)
  DEFINES += -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE
endif

### The object files (add further files here):

OBJS = misc.o log.o override.o data.o crypto.o parse.o system.o system-common.o smartcard.o network.o filter.o version.o
SHAREDOBJS = compat.o $(VDRDIR)/tools.o $(VDRDIR)/thread.o
LIBS = -lpthread -lrt -ljpeg -lcrypto
DYNLIBS = -ldl $(VDRDIR)/libsi/libsi.a

NOBJS  = $(patsubst %.o,../%.o,$(OBJS))

### Implicit rules:

$(VDRDIR)/%.o: $(VDRDIR)/%.c
	make -C $(VDRDIR) $(@F)

../%.o: ../%.c
	make -C .. $(@F)

%.o: %.c
	$(CXX) $(CXXFLAGS) -c $(DEFINES) $(INCLUDES) $<

### Targets:

all: testECM testEMM testN1Emu testN2Emu testN2RunEmu testTPS testExtAU testMutex

testECM.o: testECM.c compat.h
testECM: testECM.o $(SHAREDOBJS) $(NOBJS)
	$(CXX) $(CXXFLAGS) -rdynamic $^ $(LIBS) $(DYNLIBS) -o $@

testEMM.o: testEMM.c compat.h
testEMM: testEMM.o $(SHAREDOBJS) $(NOBJS)
	$(CXX) $(CXXFLAGS) -rdynamic $^ $(LIBS) $(DYNLIBS) -o $@

testN1Emu.o: testN1Emu.c ../systems/nagra/nagra.c ../systems/nagra/nagra1.c ../systems/nagra/cpu.c ../systems/nagra/log-nagra.h
testN1Emu: testN1Emu.o $(SHAREDOBJS) $(NOBJS)
	$(CXX) $(CXXFLAGS) $^ $(LIBS) -o $@

testN2Emu.o: testN2Emu.c compat.h
testN2Emu: testN2Emu.o $(SHAREDOBJS) $(NOBJS)
	$(CXX) $(CXXFLAGS) $^ $(LIBS) -L../systems/nagra -lsc-nagra -o $@
	@echo "don't forget: export LD_LIBRARY_PATH=../systems/nagra"

testN2RunEmu.o: testN2RunEmu.c
testN2RunEmu: testN2RunEmu.o $(SHAREDOBJS) $(NOBJS)
	$(CXX) $(CXXFLAGS) $^ $(LIBS) -L../systems/nagra -lsc-nagra -o $@
	@echo "don't forget: export LD_LIBRARY_PATH=../systems/nagra"

testTPS.o: testTPS.c ../systems/viaccess/tps.c ../systems/viaccess/st20.c ../systems/viaccess/viaccess.c
testTPS: testTPS.o $(SHAREDOBJS) $(NOBJS)
	$(CXX) $(CXXFLAGS) $^ $(LIBS) -o $@

testExtAU.o: testExtAU.c compat.h
testExtAU: testExtAU.o $(SHAREDOBJS) $(NOBJS)
	$(CXX) $(CXXFLAGS) -rdynamic $^ $(LIBS) $(DYNLIBS) -o $@

testINIT.o: testINIT.c compat.h
testINIT: testINIT.o $(SHAREDOBJS) $(NOBJS)
	$(CXX) $(CXXFLAGS) -rdynamic $^ $(LIBS) $(DYNLIBS) -o $@

filterhelper: filterhelper.o
	$(CXX) $(CXXFLAGS) $^ -o $@

testMutex: testMutex.o
	$(CXX) $(CXXFLAGS) $^ -lpthread -o $@

clean:
	@-rm -f *.o core* *~
	@-rm -f testECM testEMM testN1Emu testN2Emu testN2RunEmu testTPS testExtAU testINIT
	@-rm -f filterhelper testMutex
	@-rm -f dump.txt
