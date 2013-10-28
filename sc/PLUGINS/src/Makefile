#  This program is free software: you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, either version 3 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program.  If not, see http://www.gnu.org/licenses/


# The official name of this plugin.
#
PLUGIN = sc

### The version number of this plugin

RELEASE := $(shell grep 'define SC_RELEASE' version.h | awk '{ print $$3 }' | sed -e 's/[";]//g')
SUBREL  := $(shell echo -n "Stable";)
VERSION := $(RELEASE)-$(SUBREL)
SCAPIVERS := $(shell sed -ne '/define SCAPIVERS/ s/^.[a-zA-Z ]*\([0-9]*\).*$$/\1/p' version.h)

### The directory environment:

BASEDIR = ../..
LIBDIR = ../lib
SYSDIR = ./systems
PREDIR = ./systems-pre
TMPDIR = /tmp

### The C++ compiler and options:

CXX      ?= g++
ifneq ($(RELEASE),1)
CXXFLAGS ?= -O2 -g -fPIC -Wall -Woverloaded-virtual
else
CXXFLAGS ?= -O2 -fPIC -Wall -Woverloaded-virtual
endif

### Includes and Defines

INCLUDES      = -I$(BASEDIR)/include
DEFINES       = -DPLUGIN_NAME_I18N='"$(PLUGIN)"'
SHAREDDEFINES = -DAPIVERSNUM=$(APIVERSNUM) -D_GNU_SOURCE
LIBS          = -lcrypto
SHAREDLIBS    =

### Allow user defined options to overwrite defaults:

-include $(BASEDIR)/Make.config
-include Make.config

### we need this ATM because of the helper.h macros...
CXXFLAGS += -fno-strict-aliasing

### The version number of FFdecsawrapper (taken from FFdecsawrapper's "config.h"):

FFDECSAWRAPPERVERSION := $(shell sed -ne '/define FFDECSAWRAPPERVERSION/ s/^.*"\(.*\)".*$$/\1/p' $(BASEDIR)/include/ffdecsawrapper/config.h)
APIVERSION := $(shell sed -ne '/define APIVERSION/ s/^.*"\(.*\)".*$$/\1/p' $(BASEDIR)/include/ffdecsawrapper/config.h)
ifeq ($(strip $(APIVERSION)),)
   APIVERSION = $(FFDECSAWRAPPERVERSION)
endif
FFDECSAWRAPPERVERSNUM := $(shell sed -ne '/define FFDECSAWRAPPERVERSNUM/ s/^.[a-zA-Z ]*\([0-9]*\) .*$$/\1/p' $(BASEDIR)/include/ffdecsawrapper/config.h)
APIVERSNUM := $(shell sed -ne '/define APIVERSNUM/ s/^.[a-zA-Z ]*\([0-9]*\) .*$$/\1/p' $(BASEDIR)/include/ffdecsawrapper/config.h)
ifeq ($(strip $(APIVERSNUM)),)
   APIVERSNUM = $(FFDECSAWRAPPERVERSNUM)
endif

### The object files (add further files here):

OBJS = $(PLUGIN).o data.o filter.o system.o misc.o cam.o device.o version.o \
       smartcard.o network.o crypto.o system-common.o parse.o log.o \
       override.o

### Internationalization (I18N):

PODIR     = po
I18Npot   = $(PODIR)/$(PLUGIN).pot
I18Nmo    = ffdecsawrapper-$(PLUGIN).mo
I18Nmsgs  = $(addprefix $(LOCALEDIR)/,$(addsuffix /LC_MESSAGES/$(I18Nmo),$(notdir $(foreach file, $(wildcard $(PODIR)/*.po), $(basename $(file))))))
LOCALEDIR = $(BASEDIR)/locale

SHAREDDEFINES += -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE

# max number of CAIDs per slot
MAXCAID = 64
DEFINES += -DFFDECSAWRAPPER_MAXCAID=$(MAXCAID)

# FFdeCSA
CPUOPT     ?= pentium
PARALLEL   ?= PARALLEL_32_INT

ifneq ($(RELEASE),1)
CSAFLAGS   ?= -Wall -fPIC -g -O3 -mmmx -fomit-frame-pointer -fexpensive-optimizations -funroll-loops
else
CSAFLAGS   ?= -Wall -fPIC -O3 -mmmx -fomit-frame-pointer -fexpensive-optimizations -funroll-loops
endif

FFDECSADIR  = FFdecsa
FFDECSA     = $(FFDECSADIR)/FFdecsa.o
FFDECSATEST = $(FFDECSADIR)/FFdecsa_test.done

# FFDECSAWRAPPER
ifdef FFDECSAWRAPPER
DEFINES += -DFFDECSAWRAPPER
FFDECSA =
FFDECSATEST =
endif

# export for system makefiles
export SCAPIVERS
export APIVERSION
export INCLUDES
export SHAREDDEFINES
export SHAREDLIBS
export CXX
export CXXFLAGS

### Targets:

BUILDTARGETS = $(LIBDIR)/libffdecsawrapper-$(PLUGIN).a
SHAREDDEFINES += -DSTATICBUILD
BUILDTARGETS += $(FFDECSATEST) systems i18n

all: $(BUILDTARGETS)
.PHONY: i18n systems systems-pre clean clean-core clean-systems clean-pre dist

# Dependencies:

MAKEDEP = g++ -MM -MG
DEPFILE = .dependencies
DEPFILES = $(subst i18n.c,,$(subst version.c,,$(OBJS:%.o=%.c)))
$(DEPFILE): $(DEPFILES) $(wildcard *.h)
	@$(MAKEDEP) $(DEFINES) $(SHAREDDEFINES) $(INCLUDES) $(DEPFILES) > $@

-include $(DEPFILE)

# Rules

%.o: %.c
	$(CXX) $(CXXFLAGS) -c $(DEFINES) $(SHAREDDEFINES) $(INCLUDES) $<

libffdecsawrapper-$(PLUGIN).so: $(OBJS) $(FFDECSA)
	$(CXX) $(CXXFLAGS) -shared $(OBJS) $(FFDECSA) $(LIBS) $(SHAREDLIBS) -o $@

$(LIBDIR)/libffdecsawrapper-$(PLUGIN).so.$(APIVERSION): libffdecsawrapper-$(PLUGIN).so
	@cp -p $< $@

$(LIBDIR)/libffdecsawrapper-$(PLUGIN).a: $(OBJS)
	$(AR) r $@ $(OBJS)

$(FFDECSA) $(FFDECSATEST): $(FFDECSADIR)/*.c $(FFDECSADIR)/*.h
	@$(MAKE) COMPILER="$(CXX)" FLAGS="$(CSAFLAGS) -march=$(CPUOPT)" PARALLEL_MODE=$(PARALLEL) -C $(FFDECSADIR) all

$(I18Npot): $(shell grep -rl '\(tr\|trNOOP\)(\".*\")' *.c $(SYSDIR))
	xgettext -C -cTRANSLATORS --no-wrap --no-location -k -ktr -ktrNOOP --msgid-bugs-address='<noone@nowhere.org>' -o $@ `ls $^`

%.po: $(I18Npot)
	msgmerge -U --no-wrap --no-location --backup=none -q $@ $<
	@touch $@

%.mo: %.po
	msgfmt -c -o $@ $<

$(I18Nmsgs): $(LOCALEDIR)/%/LC_MESSAGES/$(I18Nmo): $(PODIR)/%.mo
	@mkdir -p $(dir $@)
	cp $< $@

i18n: $(I18Nmsgs)

version.c: FORCE
	@echo >$@.new "/* generated file, do not edit */"; \
	 echo >>$@.new 'const char *ScVersion =' '"'$(VERSION)'";'; \
	 diff $@.new $@ >$@.diff 2>&1; \
	 if test -s $@.diff; then mv -f $@.new $@; fi; \
	 rm -f $@.new $@.diff;

systems:
	@for i in `ls -A -I ".*" $(SYSDIR)`; do $(MAKE) -f ../../Makefile.system -C "$(SYSDIR)/$$i" all || exit 1; done

systems-pre:
	@for i in `ls -A -I ".*" $(PREDIR) | grep -- '-$(SCAPIVERS).so.$(APIVERSION)$$'`; do cp -p "$(PREDIR)/$$i" "$(LIBDIR)"; done

clean-systems:
	@for i in `ls -A -I ".*" $(SYSDIR)`; do $(MAKE) -f ../../Makefile.system -C "$(SYSDIR)/$$i" clean; done

clean-core:
	@if test -d $(FFDECSADIR); then $(MAKE) -C $(FFDECSADIR) clean; fi
	@-rm -f $(LIBDIR)/libsc-*-$(SCAPIVERS).so.$(APIVERSION)
	@-rm -f $(LIBDIR)/libffdecsawrapper-$(PLUGIN).a $(LIBDIR)/libsc-*.a
	@-rm -f $(OBJS) $(DEPFILE) version.c *.so *.tar.gz core* *~
	@-rm -f $(PODIR)/*.mo

clean-pre:
	@-find "$(PREDIR)" -type f -not -name ".empty" -not -iname "*-$(SCAPIVERS).so.*" | xargs rm -f

clean: clean-core clean-systems

FORCE:

