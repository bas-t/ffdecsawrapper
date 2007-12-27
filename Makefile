#
# Softcam plugin to VDR
#
# This code is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This code is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
# Or, point your browser to http://www.gnu.org/copyleft/gpl.html

# The official name of this plugin.
# This name will be used in the '-P...' option of VDR to load the plugin.
# By default the main source file also carries this name.
#
PLUGIN = sc

### The version number of this plugin (taken from the main source file):

VERSION = $(shell grep 'define SCVERSION' version.h | awk '{ print $$3 }' | sed -e 's/[";]//g')
SCAPIVERS = $(shell sed -ne '/define SCAPIVERS/ s/^.[a-zA-Z ]*\([0-9]*\).*$$/\1/p' $(PLUGIN).c)

### The directory environment:

VDRDIR = ../../..
LIBDIR = ../../lib
SYSDIR = ./systems
PREDIR = ./systems-pre
TMPDIR = /tmp

### The C++ compiler and options:

CXX      ?= g++
CXXFLAGS ?= -O2 -g -fPIC -Wall -Woverloaded-virtual

### Includes and Defines

INCLUDES      = -I$(VDRDIR)/include
DEFINES       = -DPLUGIN_NAME_I18N='"$(PLUGIN)"'
SHAREDDEFINES = -DAPIVERSNUM=$(APIVERSNUM) -D_GNU_SOURCE
LIBS          = -lcrypto
SHAREDLIBS    =

### Allow user defined options to overwrite defaults:

-include $(VDRDIR)/Make.config
-include Make.config

### The version number of VDR (taken from VDR's "config.h"):

VDRVERSION = $(shell sed -ne '/define VDRVERSION/ s/^.*"\(.*\)".*$$/\1/p' $(VDRDIR)/include/vdr/config.h)
APIVERSION = $(shell sed -ne '/define APIVERSION/ s/^.*"\(.*\)".*$$/\1/p' $(VDRDIR)/include/vdr/config.h)
ifeq ($(strip $(APIVERSION)),)
   APIVERSION = $(VDRVERSION)
endif
VDRVERSNUM = $(shell sed -ne '/define VDRVERSNUM/ s/^.[a-zA-Z ]*\([0-9]*\) .*$$/\1/p' $(VDRDIR)/include/vdr/config.h)
APIVERSNUM = $(shell sed -ne '/define APIVERSNUM/ s/^.[a-zA-Z ]*\([0-9]*\) .*$$/\1/p' $(VDRDIR)/include/vdr/config.h)
ifeq ($(strip $(APIVERSNUM)),)
   APIVERSNUM = $(VDRVERSNUM)
endif

### The name of the distribution archive:

ARCHIVE = $(PLUGIN)-$(VERSION)
PACKAGE = vdr-$(ARCHIVE)

### The object files (add further files here):

OBJS = $(PLUGIN).o data.o filter.o system.o misc.o cam.o \
       smartcard.o network.o crypto.o system-common.o parse.o log.o

### Internationalization (I18N):

PODIR     = po
I18Npot   = $(PODIR)/$(PLUGIN).pot
ifeq ($(strip $(APIVERSION)),1.5.7)
  I18Nmo  = $(PLUGIN).mo
else
  I18Nmo  = vdr-$(PLUGIN).mo
endif
I18Nmsgs  = $(addprefix $(LOCALEDIR)/,$(addsuffix /LC_MESSAGES/$(I18Nmo),$(notdir $(foreach file, $(wildcard $(PODIR)/*.po), $(basename $(file))))))
LOCALEDIR = $(VDRDIR)/locale
HASLOCALE = $(shell grep -l 'I18N_DEFAULT_LOCALE' $(VDRDIR)/include/vdr/i18n.h)

ifeq ($(strip $(HASLOCALE)),)
  OBJS += i18n.o
endif

#
# generic stuff
#

# smartcard default port
ifdef DEFAULT_PORT
  TEST := $(shell echo '$(DEFAULT_PORT)' | sed -ne '/".*",.*,.*,.*/p')
  ifneq ($(strip $(TEST)),)
    DEFINES += -DDEFAULT_PORT='$(DEFAULT_PORT)'
  else
    $(error DEFAULT_PORT has bad format)
  endif 
endif

# max number of CAIDs per slot
MAXCAID := $(shell sed -ne '/define MAXCASYSTEMIDS/ s/^.[a-zA-Z ]*\([0-9]*\).*$$/\1/p' $(VDRDIR)/ci.c)
ifneq ($(strip $(MAXCAID)),)
  DEFINES += -DVDR_MAXCAID=$(MAXCAID)
endif

# FFdeCSA
CPUOPT     ?= pentium
PARALLEL   ?= PARALLEL_32_INT
CSAFLAGS   ?= -Wall -fPIC -g -O3 -mmmx -fomit-frame-pointer -fexpensive-optimizations -funroll-loops
FFDECSADIR  = FFdecsa
FFDECSA     = $(FFDECSADIR)/FFdecsa.o

# SASC
ifdef SASC
DEFINES += -DSASC
FFDECSA =
endif

# export for system makefiles
export SCAPIVERS
export APIVERSION
export INCLUDES
export SHAREDDEFINES
export SHAREDLIBS
export CXX
export CXXFLAGS

# Dependencies:

MAKEDEP = g++ -MM -MG
DEPFILE = .dependencies
$(DEPFILE): $(subst i18n.c,,$(OBJS:%.o=%.c)) $(wildcard *.h)
	@$(MAKEDEP) $(DEFINES) $(SHAREDDEFINES) $(INCLUDES) $(subst i18n.c,,$(OBJS:%.o=%.c)) > $@

-include $(DEPFILE)

### Targets:

ifdef STATIC
BUILDTARGETS = $(LIBDIR)/libvdr-$(PLUGIN).a systems
SHAREDDEFINES += -DSTATICBUILD
else
BUILDTARGETS = $(LIBDIR)/libvdr-$(PLUGIN).so.$(APIVERSION) systems systems-pre
endif

ifneq ($(strip $(HASLOCALE)),)
BUILDTARGETS += i18n
endif

default-target: all
all: $(BUILDTARGETS)
.PHONY: i18n systems systems-pre clean clean-core clean-systems clean-pre dist srcdist

%.o: %.c
	$(CXX) $(CXXFLAGS) -c $(DEFINES) $(SHAREDDEFINES) $(INCLUDES) $<

libvdr-$(PLUGIN).so: $(OBJS) $(FFDECSA)
	$(CXX) $(CXXFLAGS) -shared $(OBJS) $(FFDECSA) $(LIBS) $(SHAREDLIBS) -o $@

$(LIBDIR)/libvdr-$(PLUGIN).so.$(APIVERSION): libvdr-$(PLUGIN).so
	@cp -p $< $@

$(LIBDIR)/libvdr-$(PLUGIN).a: $(OBJS)
	$(AR) r $@ $(OBJS)

$(FFDECSA): $(FFDECSADIR)/*.c $(FFDECSADIR)/*.h
	@$(MAKE) COMPILER="$(CXX)" FLAGS="$(CSAFLAGS) -march=$(CPUOPT) -DPARALLEL_MODE=$(PARALLEL)" -C $(FFDECSADIR) all

$(I18Npot): $(shell grep -rl '\(tr\|trNOOP\)(\".*\")' *.c $(SYSDIR))
	xgettext -C -cTRANSLATORS --no-wrap --no-location -k -ktr -ktrNOOP --msgid-bugs-address='<noone@nowhere.org>' -o $@ $^

%.po: $(I18Npot)
	msgmerge -U --no-wrap --no-location --backup=none -q $@ $<
	@touch $@

%.mo: %.po
	msgfmt -c -o $@ $<

$(I18Nmsgs): $(LOCALEDIR)/%/LC_MESSAGES/$(I18Nmo): $(PODIR)/%.mo
	@mkdir -p $(dir $@)
	cp $< $@

i18n: $(I18Nmsgs)

i18n.c: $(PODIR)/*.po i18n-template.c po2i18n.pl
	./po2i18n.pl <i18n-template.c >i18n.c

systems:
	@for i in `ls -A -I ".*" $(SYSDIR)`; do $(MAKE) -f ../../Makefile.system -C "$(SYSDIR)/$$i" all || exit 1; done

systems-pre:
	@for i in `ls -A -I ".*" $(PREDIR) | grep -- '-$(SCAPIVERS).so.$(APIVERSION)$$'`; do cp -p "$(PREDIR)/$$i" "$(LIBDIR)"; done

clean-systems:
	@for i in `ls -A -I ".*" $(SYSDIR)`; do $(MAKE) -f ../../Makefile.system -C "$(SYSDIR)/$$i" clean; done

clean-core:
	@$(MAKE) -C testing clean
	@if test -d $(FFDECSADIR); then $(MAKE) -C $(FFDECSADIR) clean; fi
	@-rm -f $(LIBDIR)/libsc-*-$(SCAPIVERS).so.$(APIVERSION)
	@-rm -f $(LIBDIR)/libvdr-$(PLUGIN).a $(LIBDIR)/libsc-*.a
	@-rm -f $(OBJS) $(DEPFILE) i18n.c *.so *.tar.gz core* *~
	@-rm -f $(PODIR)/*.mo

clean-pre:
	@-find "$(PREDIR)" -type f -not -iname "*-$(SCAPIVERS).so.*" | xargs rm -f

clean: clean-core clean-systems

dist: clean-core
	@for i in `ls -A -I ".*" $(SYSDIR)`; do $(MAKE) -f ../../Makefile.system -C "$(SYSDIR)/$$i" dist; done
	@-rm -rf $(TMPDIR)/$(ARCHIVE)
	@mkdir $(TMPDIR)/$(ARCHIVE)
	@cp -a * $(TMPDIR)/$(ARCHIVE)
	@path="$(TMPDIR)/$(ARCHIVE)/$(notdir $(SYSDIR))";\
	 for i in `ls -A -I ".*" $$path`; do if [ -f "$$path/$$i/nonpublic.mk" ]; then rm -rf "$$path/$$i"; fi; if [ -f "$$path/$$i/nonpublic.sh" ]; then (cd $$path/$$i ; source ./nonpublic.sh ; rm ./nonpublic.sh); fi; done
	@strip --strip-unneeded --preserve-dates $(TMPDIR)/$(ARCHIVE)/$(notdir $(PREDIR))/*
	@tar czf $(PACKAGE).tar.gz -C $(TMPDIR) $(ARCHIVE)
	@-rm -rf $(TMPDIR)/$(ARCHIVE)
	@echo Distribution package created as $(PACKAGE).tar.gz

fulldist: clean clean-pre
	@-rm -rf $(TMPDIR)/$(ARCHIVE)
	@mkdir $(TMPDIR)/$(ARCHIVE)
	@cp -a * $(TMPDIR)/$(ARCHIVE)
	@tar czf $(PACKAGE)-full.tar.gz -C $(TMPDIR) $(ARCHIVE)
	@-rm -rf $(TMPDIR)/$(ARCHIVE)
	@echo Full distribution package created as $(PACKAGE)-full.tar.gz
