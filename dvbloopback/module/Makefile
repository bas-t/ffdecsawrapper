obj-m = dvbloopback.o
dvbloopback-objs := dvb_loopback.o dvblb_proc.o dvblb_forward.o

  all: add_modver
	$(MAKE) -C $(BUILD_DIR) M=$(MODDIR) modules
  add_modver: $(SYMVER)
