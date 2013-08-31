#ifndef _DVBLB_DEBUG_H_
#define _DVBLB_DEBUG_H_
enum {
  DVBDBG_FRONTEND = 0,
  DVBDBG_DEMUX    = 2,
  DVBDBG_DVR      = 4,
  DVBDBG_CA       = 6,
  DVBDBG_SID      = 8,
  DVBDBG_IOCTL    = 10,
  DVBDBG_RINGBUF  = 12,
  DVBDBG_DSS      = 14,
};
extern unsigned int _dbglvl;
int tmprintf(const char *plugin, const char *fmt, ...);

#define dprintf(args...) tmprintf("", args)
#define dprintf0(args...) tmprintf(DBG_NAME, args)
#define dprintf1(args...) if(1 <= ((_dbglvl >> PLUGIN_ID) & 3)) \
	tmprintf(DBG_NAME, args)
#define dprintf2(args...) if(2 <= ((_dbglvl >> PLUGIN_ID) & 3)) \
	tmprintf(DBG_NAME, args)
#define dprintf3(args...) if(3 == ((_dbglvl >> PLUGIN_ID) & 3)) \
	tmprintf(DBG_NAME, args)
#else
#ifdef PLUGIN_ID
  #if (PLUGIN_ID % 2) == 1
    #undef  dprintf1
    #undef  dprintf2
    #undef  dprintf3
  #else
    #if PLUGIN_ID > 30
      #error PLUGIN_ID is too large (must be between 8 and 30)
    #elif PLUGIN_ID < 8 && PLUGIN_ID != 0
      #error PLUGIN_ID is too small (must be between 8 and 30)
    #endif
  #endif
#endif //#ifdef PLUGIN_ID
#endif

