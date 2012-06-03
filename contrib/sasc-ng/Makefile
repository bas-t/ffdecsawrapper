VERSION = 0.0.2
TOOL = sasc-ng
SCVER = sc-src

include config.mak

#DEFINES = -DNO_RINGBUF

CC       ?= gcc
CXX      ?= g++
CXXFLAGS ?= -Wall -D__user= 
CFLAGS   ?= -Wall -D__user= 

ifdef DVB_DIR
  INCLUDES = -I$(DVB_DIR)/include
  DVB_MOD_DIR = DVB_DIR=$(DVB_DIR)
endif

DEFINES += -DRELEASE_VERSION=\"$(VERSION)\" -D__KERNEL_STRICT_NAMES
INCLUDES += -Idvbloopback/module
LBDIR = dvbloopback/src
SCDIR = sc/PLUGINS/src/$(SCVER)
SC_FLAGS = -O2 -fPIC -Wall -Woverloaded-virtual -fno-strict-aliasing

ifdef AUXSERVER_OPTS
  DEFINES += ${AUXSERVER_OPTS}
endif

CXXFLAGS += -g
CFLAGS   += -g
SC_FLAGS += -g

ifdef USE_DLOAD
  SCLIBS = -Lsc/PLUGINS/lib `find sc/PLUGINS/lib/ -name "*.so" \
           -exec basename {} \;|cut -d. -f1|sed -e 's/^lib//'|xargs -n 1 -i echo "-l{}"`
else
  SCLIBS = -Wl,-whole-archive ./sc/PLUGINS/lib/libsc-*.a -Wl,-no-whole-archive \
	./sc/PLUGINS/lib/libvdr-sc.a
endif

OBJ  := forward.o process_req.o msg_passing.o plugin_getsid.o plugin_ringbuf.o\
	plugin_showioctl.o plugin_legacysw.o plugin_dss.o plugin_cam.o \
	plugin_ffdecsa.o plugin_scan.o version.o

OBJ_SC := misc.o dvbdevice.o osdbase.o menuitems.o device.o thread.o \
	tools.o sasccam.o log.o vdrcompat.o libsi.a
ifdef USE_DLOAD
  OBJ_SC += dload.o
endif

OBJS := $(foreach ob,$(OBJ) $(OBJ_SC), objs/$(ob)) FFdecsa/FFdecsa.o
INCLUDES_SC := -I$(SCDIR) -I./sc/include

INCLUDES_SI := -Isc/include/libsi
OBJ_LIBSI := objs/si_descriptor.o objs/si_section.o objs/si_si.o objs/si_util.o

INC_DEPS := $(shell ls $(LBDIR)/*.h) dvbloopback/module/dvbloopback.h
INC_DEPS_LB := $(shell ls dvblb_plugins/*.h)

LIBS = -lpthread -lcrypto -lcrypt

all: $(TOOL) libscanwrap.so

$(TOOL): $(OBJS) | sc-plugin
	$(CXX) $(CFLAGS) -o $(TOOL) $(SCLIBS) $(OBJS) $(LIBS)

libscanwrap.so: dvblb_plugins/scanwrap.c
	$(CC) -fPIC -g -O2 -Wall -I. -nostdlib -shared -o $@ $< -ldl  -lc

clean:
	@-rm -rf $(SCDIR)/*
	@-rm -f ./sc/PLUGINS/lib/*.so* ./sc/PLUGINS/lib/*.a
	@-rm -f ./sc/config.h
	@-rm -f $(OBJS)
	@-rm -f $(OBJ_LIBSI)
	@-rm -f objs/version.cpp
	@-rm -f objs/dload.o
	@-rm -f $(TOOL)
	@-rm -f libscanwrap.so
	@-rm -rf FFdecsa/*

module_clean:
	cd dvbloopback/module && $(MAKE) clean

link-sc-plugin:
	@bash ./makelinks.sh . $(SCDIR)
	@mkdir -p $(SCDIR)/systems-pre $(SCDIR)/po

sc-plugin: link-sc-plugin
	@-rm -f sc/config.h
	@ln -s include/vdr/config.h sc/config.h
	@if [ ! -d sc/PLUGINS/lib ]; then mkdir sc/PLUGINS/lib; fi
ifdef USE_DLOAD
	$(MAKE) -C $(SCDIR) $(SCOPTS) CXX=$(CXX) CXXFLAGS="$(SC_FLAGS)" SASC=1 all
	$(MAKE) link-shared
else
	$(MAKE) -C $(SCDIR) $(SCOPTS) CXX=$(CXX) CXXFLAGS="$(SC_FLAGS)" SASC=1 STATIC=1 all
endif

link-FFdecsa:
	@bash ./makelinks.sh FFdecsa FFdecsa

FFdecsa/FFdecsa.o: link-FFdecsa
	$(MAKE) -C FFdecsa $(FFDECSA_OPTS)

module:
	cd dvbloopback/module && $(MAKE) $(DVB_MOD_DIR)
	@cp -f dvbloopback/module/dvbloopback.ko .

link-shared:
	@cd ./sc/PLUGINS/lib; \
	for i in *.so.*; do \
		link=`echo $$i|cut -d. -f-2`; \
		if [ ! -e $$link ]; then \
			ln -s $$i $$link; \
		fi \
	done

strip-sc:
	@cd ./sc/PLUGINS/lib; \
	for i in *.so.*; do \
		strip $$i; \
	done

strip-sasc:
	@strip sasc-ng

objs/libsi.a: $(OBJ_LIBSI)
	ar ru $@ $(OBJ_LIBSI)

objs/%.o: $(LBDIR)/%.c $(INC_DEPS)
	$(CXX) $(CXXFLAGS) -o $@ -c  $(DEFINES) -I$(LBDIR) $(INCLUDES) $<

objs/%.o: dvblb_plugins/%.c $(INC_DEPS) $(INC_DEPS_LB) | link-FFdecsa
	$(CXX) $(CXXFLAGS) -o $@ -c  $(DEFINES) -I$(LBDIR) $(INCLUDES) $<

objs/%.o: sc/%.cpp | link-sc-plugin
	$(CXX) $(CXXFLAGS) -o $@ -c  $(DEFINES) $(INCLUDES_SC) $(INCLUDES) $<

objs/si_%.o: sc/libsi/%.c
	$(CXX) $(CXXFLAGS) -o $@ -c  $(DEFINES) $(INCLUDES_SI) $<

objs/version.o: objs/version.cpp
	$(CXX) $(CXXFLAGS) -o $@ -c $(DEFINES) $<

objs/version.cpp: FORCE
	@echo 'const char *source_version =' '"'`(hg identify 2>/dev/null || echo -n Unknown) | sed -e 's/ .*//'`'";' > .vers.new ; diff .vers.new $@ > .vers.diff 2>&1 ; if test -s .vers.diff ; then mv -f .vers.new $@ ; fi ; rm -f .vers.new .vers.diff

FORCE:
