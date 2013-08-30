
obj-m = dvbloopback.o
dvbloopback-objs := dvb_loopback.o dvblb_proc.o dvblb_forward.o

  EXTRA_CFLAGS += -Idrivers/media/dvb-core/ -I$(PWD) \
                  -Idrivers/media/dvb/dvb-core/
  SYMVER = $(PWD)/Module.symvers
  have_modver := $(SYMVER)
  BUILD_DIR ?= /lib/modules/$(shell uname -r)/build
  all: add_modver
	./config_dvb.pl "BUILD_DIR=$(BUILD_DIR)" "EXTRA_CFLAGS=$(EXTRA_CFLAGS)"
	make -C $(BUILD_DIR) M=$(PWD) modules

ifeq ($(strip $(have_modver)),) 
add_modver:
	echo "Skipping Modver $(SYMVER)"
else
add_modver: $(SYMVER)
endif

clean:
	make -C $(BUILD_DIR) M=$(PWD) clean
	cd config-dvb && make clean
	rm -f config-dvb/chkdvb.c
	rm -f dvbdevwrap.h
	rm -f dvbdev.h
	rm -f Module.symvers

