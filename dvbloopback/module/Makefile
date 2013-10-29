
obj-m = dvbloopback.o
dvbloopback-objs := dvb_loopback.o dvblb_proc.o dvblb_forward.o

  EXTRA_CFLAGS += -Idrivers/media/dvb-core/ -I$(PWD) \
                  -Idrivers/media/dvb/dvb-core/
  SYMVER = $(PWD)/Module.symvers
  have_modver := $(SYMVER)
  BUILD_DIR ?= /lib/modules/$(shell uname -r)/build
  all: add_modver
	make -C $(BUILD_DIR) M=$(PWD) modules

add_modver: $(SYMVER)

