VERSION = 0.0.2
TOOL = forward

CC       ?= gcc
CXX      ?= g++
CXXFLAGS ?= -g -Wall -Werror
CFLAGS   ?= -g -Wall

ifdef DVB_DIR
  INCLUDES = -I$(DVB_DIR)/linux/include
  DVB_MOD_DIR = DVB_DIR=$(DVB_DIR)
endif

DEFINES += -DRELEASE_VERSION=\"$(VERSION)\"
INCLUDES += -I../module

#OBJS = forward.o process_req.o msg_passing.o plugin_getsid.o plugin_ringbuf.o \
#       plugin_dss.o version.o
OBJS = forward.o process_req.o msg_passing.o \
       plugin_dss.o version.o
LIBS = -lpthread

INC_DEPS := $(shell ls *.h)

all: $(TOOL)

$(TOOL): $(OBJS)
	$(CXX) $(CFLAGS) -o $(TOOL) $(OBJS) $(LIBS)

clean:
	rm -f $(OBJS)
	rm version.cpp

%.o: %.c $(INC_DEPS)
	$(CXX) $(CXXFLAGS) -c $(DEFINES) $(INCLUDES) $<

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $(DEFINES) $(INCLUDES) $<

version.o: version.cpp
	$(CXX) $(CXXFLAGS) -o $@ -c $(DEFINES) $<

version.cpp: FORCE
	echo 'const char *source_version =' '"'`(svnversion $(shell pwd) 2>/dev/null) || echo Unknown`'";' > .vers.new ; diff .vers.new $@ > .vers.diff 2>&1 ; if test -s .vers.diff ; then mv -f .vers.new $@ ; fi ; rm -f .vers.new .vers.diff

FORCE:
