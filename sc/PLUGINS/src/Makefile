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

PLUGIN = sc

### The version number of this plugin

RELEASE ?= $(shell git describe --always)
SUBREL  := $(shell echo -n "Master";)
VERSION := $(RELEASE)-$(SUBREL)
SCAPIVERS := $(shell sed -ne '/define SCAPIVERS/ s/^.[a-zA-Z ]*\([0-9]*\).*$$/\1/p' version.h)

### The directory environment:

BASEDIR = ../..
LIBDIR = ../lib
SYSDIR = ./systems
PREDIR = ./systems-pre
TMPDIR = /tmp

### Includes and Defines

INCLUDES      = -I$(BASEDIR)/include
DEFINES       = -DPLUGIN_NAME='"$(PLUGIN)"'
SHAREDDEFINES = -DAPIVERSNUM=$(APIVERSNUM) -D_GNU_SOURCE
LIBS          = -lcrypto
SHAREDLIBS    =

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

  SHAREDDEFINES += -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE

# max number of CAIDs per slot
MAXCAID = 64
DEFINES += -DFFDECSAWRAPPER_MAXCAID=$(MAXCAID)

# FFDECSAWRAPPER
DEFINES += -DFFDECSAWRAPPER
FFDECSA =
FFDECSATEST =
CSAFLAGS = "-Wall -fPIC -O2 -mmmx -msse -msse2 -fomit-frame-pointer -fexpensive-optimizations -funroll-loops"

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
BUILDTARGETS += $(FFDECSATEST) systems

all: $(BUILDTARGETS)
.PHONY: systems

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

$(LIBDIR)/libffdecsawrapper-$(PLUGIN).a: $(OBJS)
	$(AR) r $@ $(OBJS)

$(FFDECSA) $(FFDECSATEST): $(FFDECSADIR)/*.c $(FFDECSADIR)/*.h
	@$(MAKE) COMPILER=$(CXX) FLAGS=$(CSAFLAGS) PARALLEL_MODE=$(PARALLEL) -C $(FFDECSADIR) all

version.c: FORCE
	@echo >$@.new "/* generated file, do not edit */"; \
	 echo >>$@.new 'const char *ScVersion =' '"'$(VERSION)'";'; \
	 diff $@.new $@ >$@.diff 2>&1; \
	 if test -s $@.diff; then mv -f $@.new $@; fi; \
	 rm -f $@.new $@.diff;

systems:
	@for i in `ls -A -I ".*" $(SYSDIR)`; do $(MAKE) -f ../../Makefile.system -C "$(SYSDIR)/$$i" all || exit 1; done

FORCE:
