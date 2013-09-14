DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);
#define wrap_dvb_reg_adapter(a, b, c) dvb_register_adapter(a, b, c, &dvblb_basedev->dev, adapter_nr)
