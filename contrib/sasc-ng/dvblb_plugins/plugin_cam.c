#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/ioctl.h>
#include <linux/dvb/ca.h>
#include <linux/dvb/dmx.h>

#include "../sc/include/vdr/plugin.h"
#include "../sc/include/vdr/dvbdevice.h"
#include "../sc/include/vdr/sclink.h"
#include "../sc/include/vdr/channels.h"
#include "../sc/include/vdr/tools.h"
#include "../sc/sasccam.h"
// Hack to disable debug print control from sc

#include "process_req.h"
#include "plugin_getsid.h"
#include "plugin_cam.h"
#include "plugin_msg.h"

#define PLUGIN_ID 28
#define DBG_NAME "CAM"
#include "debug.h" //This is required to happen AFTER PLUGIN_ID is defined

#define ll_find_elem(elem, lhead, item, value, type) {  \
  struct list_head *lptr;                               \
  type *ptr;                                            \
  elem = NULL;                                          \
  list_for_each(lptr, &lhead) {                         \
    ptr = list_entry(lptr, type);                       \
    if(ptr->item == value)                              \
      elem = ptr;                                       \
  }                                                     \
}

static int cam_opt = 0;
static int opt_budget = 0;
static int opt_fixcat = 1;
static char opt_camdir[256] = {"./sc_files"};
char * get_camdir() { return opt_camdir;}
static char opt_extau[256];
static struct option Cam_Opts[] = {
  {"cam-budget", 0, &cam_opt, 'b'},
  {"cam-dir", 1, &cam_opt, 'd'},
  {"cam-nofixcat", 0, &cam_opt, 'f'},
  {"cam-extau", 1, &cam_opt, 'e'},
  {"cam-scopts", 1, &cam_opt, 'o'},
  {0, 0, 0, 0},
};

struct cam_epid {
  struct list_head list;
  unsigned int epid;
  unsigned char type;
  unsigned int sid;
  int delayclose;
};

LIST_HEAD(sclist);

LIST_HEAD(pid_empty_queue);
LIST_HEAD(pid_list);

struct fdmap_list {
  struct list_head list;
  unsigned int pid;
  int fd;
};
LIST_HEAD(fdmap_empty_queue);
LIST_HEAD(fdmap_ll);

struct scopt_list {
  char cmd[32];
  char value[256];
};
struct scopt_list scopts[32] = {{{'\0'}}};

static cPlugin *sc = NULL;
static char scCap[80];

extern const char *externalAU;
extern void update_keys(int, unsigned char, int, unsigned char *, int);
extern void SetCAMPrint(const char *_plugin_name, unsigned int plugin_id, unsigned int _print_level, unsigned int *_log_level);
const char *cPlugin::ConfigDirectory(const char *PluginName) {return opt_camdir;}

static int init_sc(void) {
  sc=(cPlugin *)VDRPluginCreator();

  dprintf0("initializing plugin: SoftCam (%s): %s\n", sc->Version(), sc->Description());
#ifndef __x86_64__
  //I have no idea why 64bit systems crash with the redirect
  SetCAMPrint(DBG_NAME, PLUGIN_ID, 0, &_dbglvl);
#endif
  if (!sc->Initialize()) {
    dprintf0("Failed to initialize sc\n");
    return false;
  }
  dprintf0("starting plugin:\n");
  if (!sc->Start()) {
    dprintf0("Failed to start sc plugin\n");
    return false;
  }

  memset(scCap, 0, 80);
  sc->SetupParse("LoggerActive","2");
#ifdef USE_AUXSERVER
  sc->SetupParse("Nagra2.AuxServerEnable", "1");
  #ifdef AUXSERVER_PORT
  sc->SetupParse("Nagra2.AuxServerPort", AUXSERVER_PORT);
  #endif
  #ifdef AUXSERVER_HOST
  sc->SetupParse("Nagra2.AuxServerAddr", AUXSERVER_HOST);
  #endif
  #ifdef AUXSERVER_PASSWD
  sc->SetupParse("Nagra2.AuxServerPass", AUXSERVER_PASSWD);
  #endif
#endif
  for(int i = 0; i < 32 && scopts[i].cmd[0] != '\0'; i++) {
    dprintf0("Setting SC options: %s = %s\n", scopts[i].cmd, scopts[i].value);
    sc->SetupParse(scopts[i].cmd, scopts[i].value);
  }
  return true;
}

static struct sc_data *find_sc_from_adpt(int adapter) {
  struct sc_data *sc_data;
  ll_find_elem(sc_data, sclist, real, adapter, struct sc_data);
  return sc_data;
}

void cam_del_pid(struct sc_data *sc_data, struct cam_epid *cam_epid) {
  //struct ScLink link;
  struct list_head *ptr;
  struct cam_epid *cam_epid1;
  int epidlist[MAXDPIDS], *epidptr = epidlist;
  list_del(&cam_epid->list);
  list_for_each(ptr, &pid_list) {
    cam_epid1 = list_entry(ptr, struct cam_epid);
    if(cam_epid1->sid == cam_epid->sid)
      *(epidptr++) = cam_epid1->epid;
  }
  *epidptr = 0;
  sc_data->cam->AddPrg(cam_epid->sid,epidlist,0,0);
  //PrepareScLink(&link, sc_data->dev, OP_DELPID);
  //link.data.pids.pid=cam_epid->epid;
  //link.data.pids.type=cam_epid->type;
  //DoScLinkOp(sc, &link);
  list_add(&cam_epid->list, &pid_empty_queue);
}

void _SetCaDescr(int adapter, ca_descr_t *ca_descr) {
  struct sc_data *sc_data = find_sc_from_adpt(adapter);
  unsigned long cadata;
  if(!sc_data)
    return;
  dprintf1("Sending key(%d) %c %d %x%x...%x\n", sc_data->cafd,
           (ca_descr->parity ? 'O' : 'E'), ca_descr->index,
           ca_descr->cw[0], ca_descr->cw[1], ca_descr->cw[7]);
  
  cadata = (ca_descr->index << 1) | (ca_descr->parity==0 ? 0 : 1);
  msg_send(MSG_LOW_PRIORITY, MSG_CAMUPDATE, sc_data->virt, (void *)cadata);
  if(sc_data->cafd >= 0) {
    //Using a real CA
    ca_descr_t tmp_dscr;
    memcpy(&tmp_dscr, ca_descr, sizeof(tmp_dscr));
    tmp_dscr.index--;
    if(ioctl(sc_data->cafd,CA_SET_DESCR,&tmp_dscr)<0) {
      dprintf0("CA_SET_DESCR failed (%s)\n", strerror(errno));
    } else {
      update_keys(sc_data->virt, 'N', ca_descr->index, NULL, 0);
    }
  } else {
    update_keys(sc_data->virt, (ca_descr->parity==0) ? 'E' : 'O',
                ca_descr->index, ca_descr->cw, 0);
  }
}

void _SetCaPid(int adapter, ca_pid_t *ca_pid) {
  struct sc_data *sc_data = find_sc_from_adpt(adapter);
  if(!sc_data)
    return;
  dprintf1("Sending (%d) key P %d PID: %d\n", sc_data->virt, ca_pid->index,
           ca_pid->pid);
  if(sc_data->cafd >= 0) {
    //Using a real CA
    ca_pid_t tmp_pid;
    memcpy(&tmp_pid, ca_pid, sizeof(tmp_pid));
    tmp_pid.index--;
    if(ioctl(sc_data->cafd,CA_SET_PID,&tmp_pid) < 0 && tmp_pid.index > 0) {
      dprintf0("CA_SET_PID failed (%s)\n", strerror(errno));
    }
  }
  update_keys(sc_data->virt, 'P', ca_pid->index, NULL, ca_pid->pid);
}

void process_cam(struct msg *msg, unsigned int priority)
{
  struct list_head *ptr;
  struct cam_epid *cam_epid;
  struct sid_msg *sidmsg;
  int match = 0;
  cChannel *ch;

  int vpid = 0, ppid = 0, tpid = 0, dcnt = 0;
  int apid[MAXAPIDS], dpid[MAXDPIDS];

  //struct ScLink link;
  struct sc_data *sc_data;

  if (msg->type == MSG_RESETSID) {
    sc_data = find_sc_from_adpt(msg->id);
    assert(sc_data);
    dprintf1("Got MSG_RESETSID\n");
    while(! list_empty(&pid_list)) {
      cam_epid = list_entry(pid_list.next, struct cam_epid);
      cam_del_pid(sc_data, cam_epid);
    }
    sc_data->cam->Stop();
    //PrepareScLink(&link, sc_data->dev, OP_TUNE);
    //link.data.tune.source=0;
    //link.data.tune.transponder=0;
    //DoScLinkOp(sc, &link);
    //sc_data->dev->SetChannelDevice(0, 0);
    update_keys(sc_data->virt, 'I', 0, NULL, 0);
    sc_data->valid = 0;
    msg->type = MSG_PROCESSED;
    return;
  }
  if (msg->type == MSG_REMOVESID) {
    unsigned int sid = 0xffff & (unsigned long)(msg->data);
    dprintf1("Got MSG_REMOVESID with sid: %d\n", sid);
    //delay removal of pids so we can continue to watch for key-rolls
    list_for_each(ptr, &pid_list) {
      cam_epid = list_entry(ptr, struct cam_epid);
      if(cam_epid->sid == sid) {
        dprintf1("Mapped sid to %d\n", cam_epid->epid);
        cam_epid->delayclose = 1;
        //update_keys('C', 0, NULL, cam_epid->epid);
        //cam_del_pid(sc_data, cam_epid);
      }
    }
    msg->type = MSG_PROCESSED;
    return;
  }
  if (msg->type == MSG_HOUSEKEEPING) {
    sc->Housekeeping();
    msg->type = MSG_PROCESSED;
    return;
  }
  if (msg->type != MSG_ADDSID)
    return;

  sc_data = find_sc_from_adpt(msg->id);
  assert(sc_data);
  sidmsg = (struct sid_msg *)msg->data;

  if (sidmsg->calen == 0) {
    free_sidmsg(sidmsg);
    msg->type = MSG_PROCESSED;
    return;
  }
  for(ch=Channels.First(); ch; ch=Channels.Next(ch)) {
    if((unsigned int)ch->Sid() == sidmsg->sid) {
      match = 1;
      break;
    }
  }
  if(match) {
    //if the sid is the same as last time, and a tune hasn't happened, we're
    //already good to go
    if(sc_data->valid) {
      list_for_each(ptr, &pid_list) {
        cam_epid = list_entry(ptr, struct cam_epid);
        if(cam_epid->delayclose && cam_epid->sid == sidmsg->sid) {
          dprintf1("Reenabling delayed-closed sid: %d\n", sidmsg->sid);
          cam_epid->delayclose = 0;
        }
      }
      free_sidmsg(sidmsg);
      msg->type = MSG_PROCESSED;
      return;
    }
  } else {
    //the sid is different, but we may be on the same transponder, so clear
    //all delayed-close pids before proceeding
    while (1) {
      ll_find_elem(cam_epid, pid_list, delayclose, 1, struct cam_epid);
      if(cam_epid == NULL)
        break;
      dprintf1("Mapped sid %d to epid %d\n", cam_epid->sid, cam_epid->epid);
      update_keys(sc_data->virt, 'C', 0, NULL, cam_epid->epid);
      for(ch=Channels.First(); ch; ch=Channels.Next(ch)) {
        if((unsigned int)ch->Sid() == cam_epid->sid) {
          Channels.Del(ch);
          break;
        }
      }
      cam_del_pid(sc_data, cam_epid);
    }
  }
  msg->type = MSG_PROCESSED;

  //create new channel
  memset(apid, 0, sizeof(int)*MAXAPIDS);
  memset(dpid, 0, sizeof(int)*MAXDPIDS);
  ch = new cChannel();

  ch->SetId(0, 1, sidmsg->sid, 0);
  if(sidmsg->nit.type==0x43) { //set source type to Satellite.  Use orbit and E/W data
    int source = 0x8000 | (BCD2INT(sidmsg->nit.orbit) & 0x7ff) | ((int)sidmsg->nit.is_east << 11);
    static char Polarizations[] = { 'h', 'v', 'l', 'r' };
    ch->SetSatTransponderData(source, BCD2INT(sidmsg->nit.frequency)/100, Polarizations[sidmsg->nit.polarization], BCD2INT(sidmsg->nit.symbolrate)/10, 0);
    }
  else if(sidmsg->nit.type==0x44) { //set source type to Cable
    ch->SetCableTransponderData(0x4000, BCD2INT(sidmsg->nit.frequency)/10, 0, BCD2INT(sidmsg->nit.symbolrate)/10, 0);
    }
  dcnt = (MAXDPIDS >= sidmsg->epid_count) ?
            sidmsg->epid_count : MAXDPIDS;
  memcpy(dpid, sidmsg->epid, sizeof(int)*dcnt);
  ch->SetPids(vpid, ppid, apid, NULL, dpid, NULL, tpid);

  if(! sc_data->valid) {
    sc_data->valid = 1;
    while(Channels.First())
      Channels.Del(Channels.First());
  }

  Channels.Add(ch);
  if(Channels.First() == Channels.Last()) {
    sc_data->cam->Tune(ch);
    //This is the first channel
    //PrepareScLink(&link, sc_data->dev, OP_TUNE);
    //link.data.tune.source=ch->Source();
    //link.data.tune.transponder=ch->Transponder();
    //dprintf0("Sending Tune cmd to SC\n");
    //DoScLinkOp(sc, &link);
    dprintf0("SC completed  Tune cmd\n");
  }

  int i, epidlist[MAXDPIDS], *epidptr = epidlist;
  for(i=0; i < sidmsg->epid_count && i < MAXDPIDS; i++) {
    pop_entry_from_queue(cam_epid, &pid_empty_queue, struct cam_epid);
    cam_epid->delayclose = 0;
    cam_epid->epid = sidmsg->epid[i];
    cam_epid->type = 5; //epid->type;
    cam_epid->sid = sidmsg->sid;
    list_add(&cam_epid->list, &pid_list);
    *(epidptr++) = sidmsg->epid[i];
    dprintf1("Adding pid %d for sid %d to pidlist\n", cam_epid->epid, cam_epid->sid);
    //PrepareScLink(&link, sc_data->dev, OP_ADDPID);
    //link.data.pids.pid=epid->epid;
    //link.data.pids.type = 5; //epid->type;
    //DoScLinkOp(sc, &link);
  }
  *epidptr = 0;
  sc_data->cam->AddPrg(sidmsg->sid,epidlist,sidmsg->ca,sidmsg->calen);
  free_sidmsg(sidmsg);
}

//taken and adapted from libdtv, (c) Rolf Hakenes
// CRC32 lookup table for polynomial 0x04c11db7
static unsigned int crc_table[256] = {
   0x00000000, 0x04c11db7, 0x09823b6e, 0x0d4326d9, 0x130476dc, 0x17c56b6b,
   0x1a864db2, 0x1e475005, 0x2608edb8, 0x22c9f00f, 0x2f8ad6d6, 0x2b4bcb61,
   0x350c9b64, 0x31cd86d3, 0x3c8ea00a, 0x384fbdbd, 0x4c11db70, 0x48d0c6c7,
   0x4593e01e, 0x4152fda9, 0x5f15adac, 0x5bd4b01b, 0x569796c2, 0x52568b75,
   0x6a1936c8, 0x6ed82b7f, 0x639b0da6, 0x675a1011, 0x791d4014, 0x7ddc5da3,
   0x709f7b7a, 0x745e66cd, 0x9823b6e0, 0x9ce2ab57, 0x91a18d8e, 0x95609039,
   0x8b27c03c, 0x8fe6dd8b, 0x82a5fb52, 0x8664e6e5, 0xbe2b5b58, 0xbaea46ef,
   0xb7a96036, 0xb3687d81, 0xad2f2d84, 0xa9ee3033, 0xa4ad16ea, 0xa06c0b5d,
   0xd4326d90, 0xd0f37027, 0xddb056fe, 0xd9714b49, 0xc7361b4c, 0xc3f706fb,
   0xceb42022, 0xca753d95, 0xf23a8028, 0xf6fb9d9f, 0xfbb8bb46, 0xff79a6f1,
   0xe13ef6f4, 0xe5ffeb43, 0xe8bccd9a, 0xec7dd02d, 0x34867077, 0x30476dc0,
   0x3d044b19, 0x39c556ae, 0x278206ab, 0x23431b1c, 0x2e003dc5, 0x2ac12072,
   0x128e9dcf, 0x164f8078, 0x1b0ca6a1, 0x1fcdbb16, 0x018aeb13, 0x054bf6a4,
   0x0808d07d, 0x0cc9cdca, 0x7897ab07, 0x7c56b6b0, 0x71159069, 0x75d48dde,
   0x6b93dddb, 0x6f52c06c, 0x6211e6b5, 0x66d0fb02, 0x5e9f46bf, 0x5a5e5b08,
   0x571d7dd1, 0x53dc6066, 0x4d9b3063, 0x495a2dd4, 0x44190b0d, 0x40d816ba,
   0xaca5c697, 0xa864db20, 0xa527fdf9, 0xa1e6e04e, 0xbfa1b04b, 0xbb60adfc,
   0xb6238b25, 0xb2e29692, 0x8aad2b2f, 0x8e6c3698, 0x832f1041, 0x87ee0df6,
   0x99a95df3, 0x9d684044, 0x902b669d, 0x94ea7b2a, 0xe0b41de7, 0xe4750050,
   0xe9362689, 0xedf73b3e, 0xf3b06b3b, 0xf771768c, 0xfa325055, 0xfef34de2,
   0xc6bcf05f, 0xc27dede8, 0xcf3ecb31, 0xcbffd686, 0xd5b88683, 0xd1799b34,
   0xdc3abded, 0xd8fba05a, 0x690ce0ee, 0x6dcdfd59, 0x608edb80, 0x644fc637,
   0x7a089632, 0x7ec98b85, 0x738aad5c, 0x774bb0eb, 0x4f040d56, 0x4bc510e1,
   0x46863638, 0x42472b8f, 0x5c007b8a, 0x58c1663d, 0x558240e4, 0x51435d53,
   0x251d3b9e, 0x21dc2629, 0x2c9f00f0, 0x285e1d47, 0x36194d42, 0x32d850f5,
   0x3f9b762c, 0x3b5a6b9b, 0x0315d626, 0x07d4cb91, 0x0a97ed48, 0x0e56f0ff,
   0x1011a0fa, 0x14d0bd4d, 0x19939b94, 0x1d528623, 0xf12f560e, 0xf5ee4bb9,
   0xf8ad6d60, 0xfc6c70d7, 0xe22b20d2, 0xe6ea3d65, 0xeba91bbc, 0xef68060b,
   0xd727bbb6, 0xd3e6a601, 0xdea580d8, 0xda649d6f, 0xc423cd6a, 0xc0e2d0dd,
   0xcda1f604, 0xc960ebb3, 0xbd3e8d7e, 0xb9ff90c9, 0xb4bcb610, 0xb07daba7,
   0xae3afba2, 0xaafbe615, 0xa7b8c0cc, 0xa379dd7b, 0x9b3660c6, 0x9ff77d71,
   0x92b45ba8, 0x9675461f, 0x8832161a, 0x8cf30bad, 0x81b02d74, 0x857130c3,
   0x5d8a9099, 0x594b8d2e, 0x5408abf7, 0x50c9b640, 0x4e8ee645, 0x4a4ffbf2,
   0x470cdd2b, 0x43cdc09c, 0x7b827d21, 0x7f436096, 0x7200464f, 0x76c15bf8,
   0x68860bfd, 0x6c47164a, 0x61043093, 0x65c52d24, 0x119b4be9, 0x155a565e,
   0x18197087, 0x1cd86d30, 0x029f3d35, 0x065e2082, 0x0b1d065b, 0x0fdc1bec,
   0x3793a651, 0x3352bbe6, 0x3e119d3f, 0x3ad08088, 0x2497d08d, 0x2056cd3a,
   0x2d15ebe3, 0x29d4f654, 0xc5a92679, 0xc1683bce, 0xcc2b1d17, 0xc8ea00a0,
   0xd6ad50a5, 0xd26c4d12, 0xdf2f6bcb, 0xdbee767c, 0xe3a1cbc1, 0xe760d676,
   0xea23f0af, 0xeee2ed18, 0xf0a5bd1d, 0xf464a0aa, 0xf9278673, 0xfde69bc4,
   0x89b8fd09, 0x8d79e0be, 0x803ac667, 0x84fbdbd0, 0x9abc8bd5, 0x9e7d9662,
   0x933eb0bb, 0x97ffad0c, 0xafb010b1, 0xab710d06, 0xa6322bdf, 0xa2f33668,
   0xbcb4666d, 0xb8757bda, 0xb5365d03, 0xb1f740b4};

unsigned int crc32 (const unsigned char *d, int len, unsigned int crc)
{
   register int i;
   const unsigned char *u=(unsigned char*)d; // Saves '& 0xff'

   for (i=0; i<len; i++)
      crc = (crc << 8) ^ crc_table[((crc >> 24) ^ *u++)];

   return crc;
}

static int parse_ca(unsigned char * buf, int len)
{
  int count, pos = 0;
  int found = 0;
  while (pos < len) {
    count = buf[pos+1] +2;
    //printf("Found CA: %d (len %d)\n", buf[pos], count);
    if(buf[pos] == 0x09) {
      buf[pos] = 0xff;
      found++;
    }
    pos+=count;
  }
  return found;
}

static void replace_cat(unsigned char *buf, int len, int pid)
{
  int desc_len, count;
  int found = 0;
  int pos = 0;
  if(len > 0) {
    if (buf[0] == 0x02) {
      count = (((buf[1] & 0x03) << 8) | buf[2]) + 3 - 4;
      desc_len = ((buf[10] & 0x03) << 8) | buf[11];
      if(desc_len > len - 12 || count > len) {
        //bogus data
        return;
      }
      found+=parse_ca(buf+12, desc_len);
      //handle epids here
      for(pos = 12 + desc_len; pos < count;) {
        desc_len = ((buf[pos+3] & 0x03) << 8) | buf[pos+4];
        if(pos+desc_len +5 > count) {
          //bogus data
          return;
        }
        found+=parse_ca(buf+pos+5, desc_len);
        pos += desc_len + 5;
      }
      if(found) {
        //compute CRC32
        int crc = crc32(buf, len-4, 0xFFFFFFFF);
        buf[len-4] = (crc >> 24) & 0xff;
        buf[len-3] = (crc >> 16) & 0xff;
        buf[len-2] = (crc >> 8) & 0xff;
        buf[len-1] = (crc >> 0) & 0xff;
      }
      dprintf1("Replaced %d cas on pid %d (len=%d)\n", found, pid, len);
    }
  }
}

static void dmxread_call(struct parser_cmds *pc, struct poll_ll *fdptr,
                      cmdret_t *result, int *ret, 
                      unsigned long int cmd, unsigned char *data)
{
  struct fdmap_list *fd_map;
  ll_find_elem(fd_map, fdmap_ll, fd, fdptr->fd, struct fdmap_list);
  if(! fd_map)
    return;
  replace_cat(pc->mmap, *ret, fd_map->pid);
}

static void dmxioctl_call(struct parser_cmds *pc, struct poll_ll *fdptr,
                      cmdret_t *result, int *ret, 
                      unsigned long int cmd, unsigned char *data)
{
  struct fdmap_list *fd_map;
  int pid;
  ll_find_elem(fd_map, fdmap_ll, fd, fdptr->fd, struct fdmap_list);
  if(fd_map)
    return;
  if(cmd == DMX_SET_FILTER) {
    struct dmx_sct_filter_params *dmx =
           (struct dmx_sct_filter_params *)data;
    pid = dmx->pid;
  } else if(cmd == DMX_SET_PES_FILTER) {
    struct dmx_pes_filter_params *dmx =
           (struct dmx_pes_filter_params *)data;
    pid = dmx->pid;
    //supporting PES filters means parsing dvr.  let's skip that for now.
    return;
  } else {
    return;
  }
  //This is a hack.  We need to get the pmt handels from the pat like getsid
  if(pid > 100)
    return;
  pop_entry_from_queue(fd_map, &fdmap_empty_queue, struct fdmap_list);
  fd_map->fd = fdptr->fd;
  fd_map->pid = pid;
  list_add(&fd_map->list, &fdmap_ll);
}
static void dmxclose_call(struct parser_cmds *pc, struct poll_ll *fdptr,
                      cmdret_t *result, int *ret, 
                      unsigned long int cmd, unsigned char *data)
{
  struct fdmap_list *fd_map;
  ll_find_elem(fd_map, fdmap_ll, fd, fdptr->fd, struct fdmap_list);
  if(fd_map) {
    list_del(&fd_map->list);
    list_add(&fd_map->list, &fdmap_empty_queue);
  }
}

void connect_cam(struct parser_adpt *pc_all)
{
  int cardnum = pc_all->frontend->common->real_adapt;
  struct list_head *ptr;
  char *cadev;
  char tmpstr[5];
  struct sc_data *sc_data;

  if(! sc)
    if(! init_sc())
      return;
 
  sc_data = (struct sc_data *)malloc(sizeof(struct sc_data));
  memset(sc_data, 0, sizeof(struct sc_data));


  sprintf(tmpstr," %d",cardnum+1);
  strncat(scCap, tmpstr, 80);
  sc->SetupParse("ScCaps",scCap);

  sc_data->valid = 0;
  sc_data->virt = pc_all->frontend->common->virt_adapt;
  sc_data->real = cardnum;
  sc_data->cam = new sascCam(cardnum);

  if (opt_budget) {
    sc_data->cafd = -1;
  } else {
    asprintf(&cadev, "/dev/dvb/adapter%d/ca0", cardnum);
    sc_data->cafd = open(cadev, O_RDWR|O_NONBLOCK);
    if (sc_data->cafd >= 0) {
      ca_caps_t ca_caps;
      if (ioctl(sc_data->cafd, CA_GET_CAP, &ca_caps) == 0 &&
          ca_caps.slot_num > 0 && (ca_caps.slot_type & CA_CI_LINK)) {
        dprintf0("Found a FF card\n");
      } else {
        sc_data->cafd = -1;
      }
    }
    free(cadev);
  }
  list_add(&sc_data->list, &sclist);
  if(opt_fixcat) {
    ATTACH_CALLBACK(&pc_all->demux->pre_ioctl, dmxioctl_call, 0);
    ATTACH_CALLBACK(&pc_all->demux->post_read, dmxread_call, 0);
    ATTACH_CALLBACK(&pc_all->demux->pre_close, dmxclose_call, 0);
  }
  list_for_each(ptr, &plugin_cmdlist) {
    struct plugin_cmd *cmd = list_entry(ptr, struct plugin_cmd);
    if(cmd->plugin == PLUGIN_GETSID) {
      cmd->send_msg(pc_all->frontend, 1);
      break;
    }
  }
}

static void launch_cam()
{
  //Call housecleaning every 60 seconds
  msg_send_replace(MSG_LOW_PRIORITY, MSG_HOUSEKEEPING, 0, NULL,
                   60, MSG_RECURRING);
}

static void shutdown_cam()
{
  if (sc)
  {
    sc->Stop();
    struct sc_data *sc_data;
    struct list_head *ptr;
    list_for_each(ptr, &sclist) {
      sc_data = list_entry(ptr, struct sc_data);
      if (sc_data->cafd >= 0) {
        close(sc_data->cafd);
        sc_data->cafd = -1;
        sc_data->valid = 0;
      }
    }
    delete sc;
    sc = NULL;
  }
}

static void usermsg_cam(char * str) {
  char *data;
  cString outstr;
  int res;
  if(sc) {
    printf(":: %s ||\n", str);
    data = strchr(str, ' ');
    if(data) {
      *data = '\0';
      data++;
    }
    printf("User Message %s data: %s\n", str, data);
    outstr = sc->SVDRPCommand((const char *)str, (const char *)data, res);
    printf("%s",*outstr);
    printf("User Message %s returned: %d\n", str, res);
  }
}

static struct option *parseopt_cam(arg_enum_t cmd)
{
  if(cmd == ARG_INIT) {
    return Cam_Opts;
  }
  if(cmd == ARG_HELP) {
    printf("   --cam-budget      : Force budget card mode\n");
    printf("   --cam-dir <dir>   : Set directory for sc files (default ./sc_files)\n");
    printf("   --cam-nofixcat    : Don't remove cat entries\n");
    printf("   --cam-extau <cmd> : Execute cmd to retrieve updated keys\n");
    printf("   --cam-scopts <cmd>: Set sc options (opt=value)\n");
    return NULL;
  }
  if(! cam_opt)
    return NULL;

  switch(cam_opt) {
    case 'b':
      opt_budget = 1;
      break;
    case 'd':
      strncpy(opt_camdir, optarg, sizeof(opt_camdir)-1);
      opt_camdir[sizeof(opt_camdir)-1]=0;
      break;
    case 'e':
      strncpy(opt_extau, optarg, sizeof(opt_extau)-1);
      opt_extau[sizeof(opt_extau)-1]=0;
      externalAU = opt_extau;
    case 'f':
      opt_fixcat = 0;
      break;
    case 'o':
      {
        int i;
        for(i = 0; i < 32 && scopts[i].cmd[0] != '\0'; i++)
          ;
        if(i == 32) {
          dprintf0("Too many SC options.  Ignoring: %s\n", optarg);
        } else {
          char *pos =optarg;
          while(*pos != '=' && *pos != '\0')
            pos++;
          if(*pos == '\0') {
            dprintf0("Could not parse SC argument: %s\n", optarg);
          } else {
            memcpy(scopts[i].cmd, optarg, pos - optarg);
            memcpy(scopts[i].value, pos+1, strlen(optarg) - (1+pos - optarg));
printf("here: %s %s %d\n", scopts[i].cmd, scopts[i].value, i);
            if(i != 31)
              scopts[i+1].cmd[0] = '\0';
          }
        }
        break;
      }
  }
  //must reset cam_opt after every call
  cam_opt = 0;
  return NULL;
}
//list, plugin_id, name, parse_args, connect, launch, message, send_msg
static struct plugin_cmd plugin_cmds = {{NULL, NULL}, PLUGIN_ID, "cam",
                 parseopt_cam, connect_cam, launch_cam, process_cam, NULL,
                 shutdown_cam, usermsg_cam};

int __attribute__((constructor)) __cam_init(void)
{
  list_add_tail(&plugin_cmds.list, &plugin_cmdlist);
  return 0;
}
