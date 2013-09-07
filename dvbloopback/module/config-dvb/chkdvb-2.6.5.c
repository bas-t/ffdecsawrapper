#include <linux/config.h>       /* needed to get LINUX_VERSION_CODE >= 2.6.13 */
  #include "dvbdev.h"
  int test(void) {
    struct dvb_adapter *adap = NULL;
    const char *name = NULL;
    struct module *module = NULL;
    struct device *device = NULL;
    int ret;
    adap = adap;
    name = name;
    module = module;
    device = device;
//ret = dvb_register_adapter(adap, name, module, device);
return ret;
}
