
#define PLUGIN_CAM 28
class sascCam;

struct sc_data {
  struct list_head list;
  sascCam *cam;
  unsigned int lastsid;
  int cafd;
  int virt;
  int real;
  int valid;
};

extern "C" void *VDRPluginCreator(void);
