
### The directory environment:

VDRDIR = ../../../..

### Allow user defined options to overwrite defaults:

-include $(VDRDIR)/Make.config

### The C++ compiler and options:

CXX      ?= g++
CXXFLAGS ?= -g -march=pentium3 -O2 -Wall -Woverloaded-virtual

### Includes and Defines

SCAPIVERS = $(shell sed -ne '/define SCAPIVERS/ s/^.[a-zA-Z ]*\([0-9]*\).*$$/\1/p' ../sc.c)
APIVERSION = $(shell sed -ne '/define APIVERSION/ s/^.*"\(.*\)".*$$/\1/p' $(VDRDIR)/include/vdr/config.h)
APIVERSNUM = $(shell sed -ne '/define APIVERSNUM/ s/^.[a-zA-Z ]*\([0-9]*\) .*$$/\1/p' $(VDRDIR)/include/vdr/config.h)

INCLUDES = -I.. -I$(VDRDIR)/include
DEFINES  = -DAPIVERSNUM=$(APIVERSNUM) -DAPIVERSION='"$(APIVERSION)"' -DSCAPIVERS=$(SCAPIVERS) -D_GNU_SOURCE

OBJS = misc.o log.o data.o crypto.o parse.o system.o system-common.o smartcard.o network.o filter.o
SHAREDOBJS = compat.o $(VDRDIR)/tools.o $(VDRDIR)/thread.o
LIBS = -lpthread -ljpeg -ldl -lcrypto

NOBJS  = $(patsubst %.o,../%.o,$(OBJS))

### Implicit rules:

$(VDRDIR)/%.o: $(VDRDIR)/%.c
	make -C $(VDRDIR) $(@F)

../%.o: ../%.c
	make -C .. $(@F)

%.o: %.c
	$(CXX) $(CXXFLAGS) -c $(DEFINES) $(INCLUDES) $<

### Targets:

all: testECM testEMM testN1Emu testN2Emu

testECM.o: testECM.c compat.h
testECM: testECM.o $(SHAREDOBJS) $(NOBJS)
	$(CXX) $(CXXFLAGS) -rdynamic $^ $(LIBS) -o $@

testEMM.o: testEMM.c compat.h
testEMM: testEMM.o $(SHAREDOBJS) $(NOBJS)
	$(CXX) $(CXXFLAGS) -rdynamic $^ $(LIBS) -o $@

testN1Emu.o: testN1Emu.c ../systems/nagra/nagra.c ../systems/nagra/nagra1.c ../systems/nagra/cpu.c ../systems/nagra/log-nagra.h
testN1Emu: testN1Emu.o $(SHAREDOBJS) $(NOBJS)
	$(CXX) $(CXXFLAGS) $^ $(LIBS) -o $@

testN2Emu.o: testN2Emu.c ../systems/nagra/nagra2*.c ../systems/nagra/cpu.c
testN2Emu: testN2Emu.o $(SHAREDOBJS) $(NOBJS)
	$(CXX) $(CXXFLAGS) $^ $(LIBS) -o $@

filterhelper: filterhelper.o
	$(CXX) $(CXXFLAGS) $^ -o $@
clean:
	@-rm -f *.o core* *~
	@-rm -f testECM testEMM testN1Emu testN2Emu
	@-rm -f filterhelper
	@-rm -f dump.txt
