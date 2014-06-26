/*
   DVBLoopback
   Copyright Alan Nisota 2006

   This file is part of DVBLoopback.

    DVBLoopback is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    DVBLoopback is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with DVBLoopback; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <errno.h>
#include "list.h"
#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>

#include "process_req.h"
#include "msg_passing.h"

#define PLUGIN_ID 9
#define DBG_NAME "LEGACYSW"
#include "debug.h" //This is required to happen AFTER PLUGIN_ID is defined

#ifdef FE_DISHNETWORK_SEND_LEGACY_CMD
enum {
  LEGSW_UNKNOWN = 0,
  LEGSW_SW64,
  LEGSW_SW21,
};
static int legsw_opt = 0;
static int opt_mapdiseqc = LEGSW_UNKNOWN;
static struct option LegacySW_Opts[] = {
  {"legsw-diseqcmap", 1, &legsw_opt, 'd'},
  {0, 0, 0, 0},
};

static void fe_ioctl(struct parser_cmds *pc, struct poll_ll *fdptr,
                    cmdret_t *result, int *ret,
                    unsigned long int cmd, unsigned char *data)
{
    if(FE_DISEQC_SEND_MASTER_CMD == cmd) {
      struct dvb_diseqc_master_cmd *msg = (struct dvb_diseqc_master_cmd *)data;
      unsigned char lcmd = 0;
      if(msg->msg_len != 4)
        return;
      if(msg->msg[0] != 0xe0 || msg->msg[1] != 0x10 || msg->msg[2] != 0x38)
        return;
      switch(msg->msg[3] & 0x0c) {
        case 0x00: //Port 0
          if(opt_mapdiseqc == LEGSW_SW64) {
            if(msg->msg[3] & 0x02) {
              lcmd = 0x1a;
            } else {
              lcmd = 0x39;
            }
          } else if(opt_mapdiseqc == LEGSW_SW21) {
            lcmd = 0x34;
          }
          break;
        case 0x04: //Port 1
          if(opt_mapdiseqc == LEGSW_SW64) {
            if(msg->msg[3] & 0x02) {
              lcmd = 0x5c;
            } else {
              lcmd = 0x4b;
            }
          } else if(opt_mapdiseqc == LEGSW_SW21) {
            lcmd = 0x65;
          }
          break;
        case 0x08: //Port 2
          if(opt_mapdiseqc == LEGSW_SW64) {
            if(msg->msg[3] & 0x02) {
              lcmd = 0x2e;
            } else {
              lcmd = 0x0d;
            }
          }
          break;
      }
      printf("Got Diseqc cmd!!!\n len: %d %02x->%02x\n", msg->msg_len, msg->msg[3], lcmd);
      if (lcmd == 0)
        return;
      if(msg->msg[3] & 0x02)
        lcmd |= 0x80;
      *result = CMD_SKIPCALL;
      *ret = ioctl(fdptr->fd, FE_DISHNETWORK_SEND_LEGACY_CMD, lcmd);
      if(*ret < 0) {
        *ret = errno;
      }
    }
}

static void connect_legsw(struct parser_adpt *pc_all)
{
  if(opt_mapdiseqc) {
    ATTACH_CALLBACK(&pc_all->frontend->pre_ioctl, fe_ioctl, -1);
  }
}
static struct option *parseopt_legsw(arg_enum_t cmd)
{
  if(cmd == ARG_INIT) {
    return LegacySW_Opts;
  } 
  if(cmd == ARG_HELP) {
    printf("   --legsw-diseqcmap <legacy-type> :\n");
    printf("                        map diseqc switch cmds to legacy cmds\n");
    printf("                        legacy-type must be sw64 or sw21\n");
  } 
  if(! legsw_opt)
    return NULL;

  switch(legsw_opt) {
    case 'd':
      if(strncmp(optarg, "sw64", 4) == 0) {
        opt_mapdiseqc = LEGSW_SW64;
      } else if(strncmp(optarg, "sw21", 4) == 0) {
        opt_mapdiseqc = LEGSW_SW21;
      }
      break;
  }
  //must reset sid_opt after every call
  legsw_opt = 0;
  return NULL;
}

//list, plugin_id, name, parse_args, connect, launch, message, send_msg
static struct plugin_cmd plugin_cmds = {{NULL, NULL}, PLUGIN_ID, 
                     "legacy switch",
                     parseopt_legsw, connect_legsw, NULL, NULL, NULL, NULL, NULL};
int __attribute__((constructor)) __legacysw_init(void)
{
  list_add(&plugin_cmds.list, &plugin_cmdlist);
  return 0;
}
#endif //FE_DISHNETWORK_SEND_LEGACY_CMD
