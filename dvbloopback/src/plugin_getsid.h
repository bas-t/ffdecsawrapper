#ifndef _PLUGIN_GETSID_H_
#define _PLUGIN_GETSID_H_

#include "process_req.h"
#include "messages.h"
#define PLUGIN_GETSID 8

#define MAX_EPID 128
struct nit_data {
  unsigned int frequency;
  unsigned short orbit;
  unsigned char is_east;
  unsigned char polarization;
  unsigned char modulation;
  unsigned int symbolrate;
  unsigned char fec;
  unsigned char type;
};

struct sid_msg {
  struct list_head list;
  unsigned long sid;
  int calen;
  int epid_count;
  unsigned char ca[1024];
  int epid[MAX_EPID];
  unsigned char epidtype[MAX_EPID];
  struct nit_data nit;
};

struct epid {
  struct list_head list;
  unsigned int epid;
  unsigned char type;
};

struct sid {
  struct list_head list;
  unsigned long sid;
  struct list_head epid;
  unsigned char ca[1024];
  int calen;
};

struct dmxcmd {
  struct list_head list;
  int pid;
  int fd;
  int checked;
  struct sid *sid;
};

struct sid_data {
  struct list_head list;
  struct common_data *common;

  struct list_head cmdqueue;
  struct list_head sidlist;

  struct nit_data nit;
  int has_map;
  int removed_sid;
  int sendmsg;
  unsigned char tunecache[256]; //Should be large enough
  pthread_t thread;
  pthread_mutex_t mutex;
  pthread_cond_t cond;
};

enum {
  GETSID_CMD_RESET  = 0,
  GETSID_CMD_START  = 1,
  GETSID_CMD_PID    = 2
};

enum
{
    // video
    MPEG1Video     = 0x01, ///< ISO 11172-2 (aka MPEG-1)
    MPEG2Video     = 0x02, ///< ISO 13818-2 & ITU H.262 (aka MPEG-2)
    MPEG4Video     = 0x10, ///< ISO 14492-2 (aka MPEG-4)
    H264Video      = 0x1b, ///< ISO 14492-10 & ITU H.264 (aka MPEG-4-AVC)
    OpenCableVideo = 0x80,

    // audio
    MPEG1Audio     = 0x03, ///< ISO 11172-3
    MPEG2Audio     = 0x04, ///< ISO 13818-3
    MPEG2AudioAmd1 = 0x11, ///< ISO 13818-3/AMD-1 Audio using LATM syntax
    AACAudio       = 0x0f, ///< ISO 13818-7 Audio w/ADTS syntax
    AC3Audio       = 0x81,
    DTSAudio       = 0x8a,

    // DSM-CC Object Carousel
    DSMCC          = 0x08, ///< ISO 13818-1 Annex A DSM-CC & ITU H.222.0
    DSMCC_A        = 0x0a, ///< ISO 13818-6 type A Multi-protocol Encap
    DSMCC_B        = 0x0b, ///< ISO 13818-6 type B Std DSMCC Data
    DSMCC_C        = 0x0c, ///< ISO 13818-6 type C NPT DSMCC Data
    DSMCC_D        = 0x0d, ///< ISO 13818-6 type D Any DSMCC Data
    DSMCC_DL       = 0x14, ///< ISO 13818-6 Download Protocol
    MetaDataPES    = 0x15, ///< Meta data in PES packets
    MetaDataSec    = 0x16, ///< Meta data in metadata_section's
    MetaDataDC     = 0x17, ///< ISO 13818-6 Metadata in Data Carousel 
    MetaDataOC     = 0x18, ///< ISO 13818-6 Metadata in Object Carousel 
    MetaDataDL     = 0x19, ///< ISO 13818-6 Metadata in Download Protocol

    // other
    PrivSec        = 0x05, ///< ISO 13818-1 private tables   & ITU H.222.0
    PrivData       = 0x06, ///< ISO 13818-1 PES private data & ITU H.222.0

    MHEG           = 0x07, ///< ISO 13522 MHEG
    H222_1         = 0x09, ///< ITU H.222.1

    MPEG2Aux       = 0x0e, ///< ISO 13818-1 auxiliary & ITU H.222.0

    FlexMuxPES     = 0x12, ///< ISO 14496-1 SL/FlexMux in PES packets
    FlexMuxSec     = 0x13, ///< ISO 14496-1 SL/FlexMux in 14496_sections

    MPEG2IPMP      = 0x1a, ///< ISO 13818-10 Digital Restrictions Mangment
    MPEG2IPMP2     = 0x7f, ///< ISO 13818-10 Digital Restrictions Mangment

    // special id's, not actually ID's but can be used in FindPIDs
    AnyMask        = 0xFFFF0000,
    AnyVideo       = 0xFFFF0001,
    AnyAudio       = 0xFFFF0002,
};

void free_sidmsg(struct sid_msg *sidmsg);
#endif
