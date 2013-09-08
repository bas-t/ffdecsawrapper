
obj-m = chkdvb.o

EXTRA_CFLAGS ?= -Idrivers/media/dvb/dvb-core/ -I$(PWD) \
                -Idrivers/media/dvb-core/

BUILD_DIR ?= /lib/modules/$(shell uname -r)/build

all: clean
	make -C $(BUILD_DIR) M=$(PWD) V=1 modules

clean:
	make -C $(BUILD_DIR) M=$(PWD) clean
