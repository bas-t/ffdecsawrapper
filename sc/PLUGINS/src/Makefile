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
CXXFLAGS ?= -O2 -g -fPIC -Wall -Woverloaded-virtual

### Includes and Defines

INCLUDES      = -I$(BASEDIR)/include
DEFINES       = -DPLUGIN_NAME_I18N='"$(PLUGIN)"'
SHAREDDEFINES = -DAPIVERSNUM=$(APIVERSNUM) -D_GNU_SOURCE
LIBS          = -lcrypto
SHAREDLIBS    =

### Allow user defined options to overwrite defaults:

-include $(BASEDIR)/Make.config
-include Make.config

### The version number of FFdecsawrapper (taken from FFdecsawrapper's "config.h"):

VDRVERSION := $(shell sed -ne '/define VDRVERSION/ s/^.*"\(.*\)".*$$/\1/p' $(BASEDIR)/include/ffdecsawrapper/config.h)
APIVERSION := $(shell sed -ne '/define APIVERSION/ s/^.*"\(.*\)".*$$/\1/p' $(BASEDIR)/include/ffdecsawrapper/config.h)
ifeq ($(strip $(APIVERSION)),)
   APIVERSION = $(VDRVERSION)
endif
VDRVERSNUM := $(shell sed -ne '/define VDRVERSNUM/ s/^.[a-zA-Z ]*\([0-9]*\) .*$$/\1/p' $(BASEDIR)/include/ffdecsawrapper/config.h)
APIVERSNUM := $(shell sed -ne '/define APIVERSNUM/ s/^.[a-zA-Z ]*\([0-9]*\) .*$$/\1/p' $(BASEDIR)/include/ffdecsawrapper/config.h)
ifeq ($(strip $(APIVERSNUM)),)
   APIVERSNUM = $(VDRVERSNUM)
endif

### The object files (add further files here):

OBJS = $(PLUGIN).o data.o filter.o system.o misc.o cam.o device.o version.o \
       smartcard.o network.o crypto.o system-common.o parse.o log.o \
       override.o

### Internationalization (I18N):

PODIR     = po
I18Npot   = $(PODIR)/$(PLUGIN).pot
I18Nmo    = vdr-$(PLUGIN).mo
I18Nmsgs  = $(addprefix $(LOCALEDIR)/,$(addsuffix /LC_MESSAGES/$(I18Nmo),$(notdir $(foreach file, $(wildcard $(PODIR)/*.po), $(basename $(file))))))
LOCALEDIR = $(BASEDIR)/locale

### VDR version dependant

# test VDR version
BYVERS = $(strip $(shell if test $(APIVERSNUM) -ge 010703; then echo "*"; fi))
# test if PlayTsVideo() exists (e.g. TSplay patch)
BYTSPL = $(strip $(shell grep -l 'PlayTsVideo' $(BASEDIR)/include/ffdecsawrapper/device.h))

ifneq ($(BYVERS)$(BYTSPL),)
  SHAREDDEFINES += -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE
endif

#
# generic stuff
#

# smartcard default port (dropped)
ifdef DEFAULT_PORT
  $(error DEFAULT_PORT support was removed, use cardslot.conf)
endif

ifdef WITH_PCSC
  DEFINES  += -DWITH_PCSC
  LIBS     += -lpcsclite
endif

# max number of CAIDs per slot
MAXCAID = 64
ifneq ($(strip $(MAXCAID)),)
  DEFINES += -DVDR_MAXCAID=$(MAXCAID)
endif

# FFdeCSA
CPUOPT     ?= pentium
PARALLEL   ?= PARALLEL_32_INT
CSAFLAGS   ?= -Wall -fPIC -g -O3 -mmmx -fomit-frame-pointer -fexpensive-optimizations -funroll-loops
FFDECSADIR  = FFdecsa
FFDECSA     = $(FFDECSADIR)/FFdecsa.o
FFDECSATEST = $(FFDECSADIR)/FFdecsa_test.done

# SASC
ifdef SASC
DEFINES += -DSASC
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

ifdef STATIC
BUILDTARGETS = $(LIBDIR)/libvdr-$(PLUGIN).a
SHAREDDEFINES += -DSTATICBUILD
else
BUILDTARGETS = $(LIBDIR)/libvdr-$(PLUGIN).so.$(APIVERSION) systems-pre
endif
BUILDTARGETS += $(FFDECSATEST) systems i18n

all: $(BUILDTARGETS)
.PHONY: i18n systems systems-pre contrib clean clean-core clean-systems clean-pre dist srcdist

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

libvdr-$(PLUGIN).so: $(OBJS) $(FFDECSA)
	$(CXX) $(CXXFLAGS) -shared $(OBJS) $(FFDECSA) $(LIBS) $(SHAREDLIBS) -o $@

$(LIBDIR)/libvdr-$(PLUGIN).so.$(APIVERSION): libvdr-$(PLUGIN).so
	@cp -p $< $@

$(LIBDIR)/libvdr-$(PLUGIN).a: $(OBJS)
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

contrib:
	@$(MAKE) -C contrib all

clean-systems:
	@for i in `ls -A -I ".*" $(SYSDIR)`; do $(MAKE) -f ../../Makefile.system -C "$(SYSDIR)/$$i" clean; done

clean-core:
	@$(MAKE) -C testing clean
	@$(MAKE) -C contrib clean
	@if test -d $(FFDECSADIR); then $(MAKE) -C $(FFDECSADIR) clean; fi
	@-rm -f $(LIBDIR)/libsc-*-$(SCAPIVERS).so.$(APIVERSION)
	@-rm -f $(LIBDIR)/libvdr-$(PLUGIN).a $(LIBDIR)/libsc-*.a
	@-rm -f $(OBJS) $(DEPFILE) version.c *.so *.tar.gz core* *~
	@-rm -f $(PODIR)/*.mo

clean-pre:
	@-find "$(PREDIR)" -type f -not -name ".empty" -not -iname "*-$(SCAPIVERS).so.*" | xargs rm -f

clean: clean-core clean-systems

dist: ARCHIVE := $(PLUGIN)-$(RELEASE)
dist: clean-core
	@for i in `ls -A -I ".*" $(SYSDIR)`; do $(MAKE) -f ../../Makefile.system -C "$(SYSDIR)/$$i" dist; done
	@-rm -rf $(TMPDIR)/$(ARCHIVE)
	@mkdir $(TMPDIR)/$(ARCHIVE)
	@cp -a * $(TMPDIR)/$(ARCHIVE)
	@echo -n "release" >$(TMPDIR)/$(ARCHIVE)/$(DISTFILE)
	@path="$(TMPDIR)/$(ARCHIVE)/$(notdir $(SYSDIR))";\
	 for i in `ls -A -I ".*" $$path`; do if [ -f "$$path/$$i/nonpublic.mk" ]; then rm -rf "$$path/$$i"; fi; if [ -f "$$path/$$i/nonpublic.sh" ]; then (cd $$path/$$i ; source ./nonpublic.sh ; rm ./nonpublic.sh); fi; done
	@strip --strip-unneeded --preserve-dates $(TMPDIR)/$(ARCHIVE)/$(notdir $(PREDIR))/* || true
	@tar czf vdr-$(ARCHIVE).tar.gz -C $(TMPDIR) $(ARCHIVE)
	@-rm -rf $(TMPDIR)/$(ARCHIVE)
	@echo Distribution package created as vdr-$(ARCHIVE).tar.gz

copy: ARCHIVE := $(PLUGIN)-$(VERSION)
copy: clean clean-pre
	@-rm -rf $(TMPDIR)/$(ARCHIVE)
	@mkdir $(TMPDIR)/$(ARCHIVE)
	@cp -a .hgtags .hgignore * $(TMPDIR)/$(ARCHIVE)
	@echo -n $(SUBREL) | sed -e 's/HG-/CP-/' >$(TMPDIR)/$(ARCHIVE)/$(DISTFILE)
	@tar czf vdr-$(ARCHIVE).tar.gz -C $(TMPDIR) $(ARCHIVE)
	@-rm -rf $(TMPDIR)/$(ARCHIVE)
	@echo Full copy package created as vdr-$(ARCHIVE).tar.gz

FORCE:
