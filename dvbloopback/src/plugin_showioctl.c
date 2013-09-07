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
#include "list.h"
#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>

#include "plugin_getsid.h"
#include "msg_passing.h"

#define PLUGIN_ID 10
#define DBG_NAME "IOCTL"
#include "debug.h" //This is required to happen AFTER PLUGIN_ID is defined

//Get rid of the usefless ioctl typechecking
#ifdef _IOC_TYPECHECK
  #undef _IOC_TYPECHECK
  #define _IOC_TYPECHECK(t) (sizeof(t))
#endif
static void fe_ioctl(struct parser_cmds *pc, struct poll_ll *fdptr,
                    cmdret_t *result, int *ret,
                    unsigned long int cmd, unsigned char *data)
{
    __u32 d32 = (*(unsigned long *)data) & 0xffffffff;
    __u16 d16 = d32 & 0xffff;
    char str[256], str1[80];;
    if(! ((_dbglvl >> DVBDBG_IOCTL) & 1))
      return;
    switch(cmd) {
      case FE_GET_INFO:
        {
          struct dvb_frontend_info *d = (struct dvb_frontend_info *)data;
          tmprintf("","FE_GET_INFO(%d): '%s'\n"
                 "min:%u max:%u step:%u tol:%u minsr:%u maxsr:%u tolsr:%u\n",
                 fdptr->fd, d->name, d->frequency_min, d->frequency_max,
                 d->frequency_stepsize, d->frequency_tolerance,
                 d->symbol_rate_min, d->symbol_rate_max,
                 d->symbol_rate_tolerance);
        }
        break;
      case FE_SET_TONE:
        tmprintf("","FE_SET_TONE(%d): %s\n", fdptr->fd, (d32 ==SEC_TONE_ON) ?
               "ON" : "OFF");
        break;
      case FE_SET_VOLTAGE:
        tmprintf("","FE_SET_VOLTAGE(%d): %s\n", fdptr->fd, (d32 ==SEC_VOLTAGE_13) ?
               "13" : (d32 == SEC_VOLTAGE_18 ? "18" : "OFF"));
        break;
      case FE_ENABLE_HIGH_LNB_VOLTAGE:
        tmprintf("","FE_ENABLE_HIGH_LNB_VOLTAGE:(%d): %d\n", fdptr->fd, d32);
        break;
      case FE_READ_STATUS:
        str[0] = 0;
        if(d32 & FE_HAS_SIGNAL)
          strcat(str," SIGNAL");
        if(d32 & FE_HAS_CARRIER)
          strcat(str," CARRIER");
        if(d32 & FE_HAS_VITERBI)
          strcat(str," VITERBI");
        if(d32 & FE_HAS_SYNC)
          strcat(str," SYNC");
        if(d32 & FE_HAS_LOCK)
          strcat(str," LOCK");
        if(d32 & FE_TIMEDOUT)
          strcat(str," TIMEDOUT");
        if(d32 & FE_REINIT)
          strcat(str," REINIT");
        tmprintf("","FE_READ_STATUS(%d):%s\n", fdptr->fd, str);
        break;
      case FE_READ_BER:
        tmprintf("","FE_READ_BER(%d): %u\n", fdptr->fd, d32);
        break;
      case FE_READ_SIGNAL_STRENGTH:
        tmprintf("","FE_READ_SIGNAL_STRENGTH(%d): %u\n", fdptr->fd, d16);
        break;
      case FE_READ_SNR:
        tmprintf("","FE_READ_SNR(%d): %u\n", fdptr->fd, d16);
        break;
      case FE_READ_UNCORRECTED_BLOCKS:
        tmprintf("","FE_READ_UNCORRECTED_BLOCKS(%d): %u\n", fdptr->fd, d32);
        break;
#ifdef FE_SET_FRONTEND2
      case FE_SET_FRONTEND2:
        {
          struct dvb_frontend_parameters_new *fe =
                 (struct dvb_frontend_parameters_new *)data;
          tmprintf("","FE_SET_FRONTEND2(%d): freq: %d inv: %s\n"
                      "  QPSK: sr: %d fec: %d\n"
                      "  DVBS2: sr: %d fec: %d mod: %d rol: %d\n", fdptr->fd,
                fe->frequency, (fe->inversion == INVERSION_OFF ? "OFF" :
                (fe->inversion == INVERSION_ON ? "ON" : "AUTO")),
                fe->u.qpsk.symbol_rate, fe->u.qpsk.fec_inner,
                fe->u.qpsk2.symbol_rate, fe->u.qpsk2.fec_inner,
                fe->u.qpsk2.modulation, fe->u.qpsk2.rolloff_factor);
        }
        break;
      case FE_GET_FRONTEND2:
        tmprintf("","FE_GET_FRONTEND2(%d)\n", fdptr->fd);
        break;
      case FE_SET_STANDARD:
        {
          char t[10];
          switch(d32) {
            case FE_QPSK:   strcpy(t, "QPSK"); break;
            case FE_QAM:    strcpy(t, "QAM"); break;
            case FE_OFDM:   strcpy(t, "OFDM"); break;
            case FE_ATSC:   strcpy(t, "ATSC"); break;
            case FE_DVB_S:  strcpy(t, "DVB_S"); break;
            case FE_DVB_C:  strcpy(t, "DVB_C"); break;
            case FE_DVB_T:  strcpy(t, "DVB_T"); break;
            case FE_DVB_S2: strcpy(t, "DVB_S2"); break;
            default: strcpy(t, "Unkown"); break;
          }
          tmprintf("","FE_SET_STANDARD(%d): %s\n", fdptr->fd, t);
        }
        break;
      case FE_GET_EXTENDED_INFO:
        tmprintf("","FE_GET_EXTENDED_INFO(%d)\n", fdptr->fd);
        break;
#endif
      case FE_SET_FRONTEND:
        {
          struct dvb_frontend_parameters *fe =
                 (struct dvb_frontend_parameters *)data;
          tmprintf("","FE_SET_FRONTEND(%d): freq: %d inv: %s\n"
                      "  QPSK: sr: %d fec: %d\n", fdptr->fd,
                fe->frequency, (fe->inversion == INVERSION_OFF ? "OFF" :
                (fe->inversion == INVERSION_ON ? "ON" : "AUTO")),
                fe->u.qpsk.symbol_rate, fe->u.qpsk.fec_inner);
        }
        break;
      case FE_GET_FRONTEND:
        tmprintf("","FE_GET_FRONTEND(%d)\n", fdptr->fd);
        break;
      case FE_GET_EVENT:
        {
          struct dvb_frontend_event *event =
                 (struct dvb_frontend_event *)data;
          str[0] = 0;
          if(*ret >= 0) {
            d32 = event->status;
            if(d32 & FE_HAS_SIGNAL)
              strcat(str," SIGNAL");
            if(d32 & FE_HAS_CARRIER)
              strcat(str," CARRIER");
            if(d32 & FE_HAS_VITERBI)
              strcat(str," VITERBI");
            if(d32 & FE_HAS_SYNC)
              strcat(str," SYNC");
            if(d32 & FE_HAS_LOCK)
              strcat(str," LOCK");
            if(d32 & FE_TIMEDOUT)
              strcat(str," TIMEDOUT");
            if(d32 & FE_REINIT)
              strcat(str," REINIT");
            strcat(str,"\n");
          }
          tmprintf("","FE_GET_EVENT(%d) returned %d\n%s", fdptr->fd, *ret, str);
        }
        break;
      case FE_DISEQC_SEND_MASTER_CMD:
        {
          struct dvb_diseqc_master_cmd *msg =
                                (struct dvb_diseqc_master_cmd *)data;
          str[0] = 0;
          for(int i = 0; i < msg->msg_len; i++) {
            sprintf(str1," %02x", msg->msg[i]);
            strcat(str, str1);
          }
          tmprintf("","FE_DISEQC_SEND_MASTER_CMD(%d):%s\n", fdptr->fd, str);
        }
        break; 
#ifdef FE_DISHNETWORK_SEND_LEGACY_CMD
      case FE_DISHNETWORK_SEND_LEGACY_CMD:
        {
          int s, p, c = d32 & 0x7f;
          switch(c) {
            case 0x34: s=21, p = 0; break;
            case 0x65: s=21, p = 1; break;
            case 0x46: s=42, p = 1; break;
            case 0x17: s=42, p = 1; break;
            case 0x39: s=64, p = 10; break;
            case 0x1a: s=64, p = 11; break;
            case 0x4b: s=64, p = 20; break;
            case 0x5c: s=64, p = 21; break;
            case 0x0d: s=64, p = 30; break;
            case 0x2e: s=64, p = 31; break;
            default: s=0; p=0; break;
          }
          tmprintf("","FE_DISHNETWORK_SEND_LEGACY_CMD(%d): SW%d p:%d\n", fdptr->fd,
                 s, p);
        }
        break;
#endif
      default:
        break;
    }
}
static void demux_ioctl(struct parser_cmds *pc, struct poll_ll *fdptr,
                    cmdret_t *result, int *ret,
                    unsigned long int cmd, unsigned char *data)
{
    char str[256], str1[80];
    __u32 d32 = (*(unsigned long *)data) & 0xffffffff;
    if(! ((_dbglvl >> DVBDBG_IOCTL) & 2))
      return;
    switch(cmd) {
      case DMX_START:
        tmprintf("","DMX_START(%d)\n", fdptr->fd);
        break;
      case DMX_STOP:
        tmprintf("","DMX_STOP(%d)\n", fdptr->fd);
        break;
      case DMX_SET_FILTER:
        {
          struct dmx_sct_filter_params *dmx =
                 (struct dmx_sct_filter_params *)data;
          str[0] = 0;
          if(dmx->flags)
            sprintf(str,"    flags: %s%s%s%s\n",
                   (dmx->flags & DMX_CHECK_CRC ? " CHECK_CRC" : ""),
                   (dmx->flags & DMX_ONESHOT ? " ONESHOT" : ""),
                   (dmx->flags & DMX_IMMEDIATE_START ? " IMMEDIATE_START" : ""),
                   (dmx->flags & DMX_KERNEL_CLIENT ? " KERNEL_CLIENT" : ""));
          for(int i = 0; i < DMX_FILTER_SIZE; i++) {
            if(dmx->filter.filter[i] == 0)
              break;
            sprintf(str1,"    filter: %02x mask: %02x mode: %02x\n",
               dmx->filter.filter[i], dmx->filter.mask[i],
               dmx->filter.mode[i]);
            strcat(str, str1);
          }
          tmprintf("","DMX_SET_FILTER(%d): pid:%d timeout:%d\n%s", fdptr->fd, 
                 dmx->pid, dmx->timeout, str);
        }
        break;
      case DMX_SET_PES_FILTER:
        {
          struct dmx_pes_filter_params *dmx =
                 (struct dmx_pes_filter_params *)data;
          char t[10];
          switch(dmx->pes_type) {
            case DMX_PES_AUDIO0: strcpy(t, "AUDIO"); break;
            case DMX_PES_VIDEO0: strcpy(t, "VIDEO"); break;
            case DMX_PES_TELETEXT0: strcpy(t, "TELETEXT"); break;
            case DMX_PES_SUBTITLE0: strcpy(t, "SUBTITLE"); break;
            case DMX_PES_PCR0: strcpy(t, "PCR"); break;

            case DMX_PES_AUDIO1: strcpy(t, "AUDIO1"); break;
            case DMX_PES_VIDEO1: strcpy(t, "VIDEO1"); break;
            case DMX_PES_TELETEXT1: strcpy(t, "TELETEXT1"); break;
            case DMX_PES_SUBTITLE1: strcpy(t, "SUBTITLE1"); break;
            case DMX_PES_PCR1: strcpy(t, "PCR1"); break;

            case DMX_PES_AUDIO2: strcpy(t, "AUDIO2"); break;
            case DMX_PES_VIDEO2: strcpy(t, "VIDEO2"); break;
            case DMX_PES_TELETEXT2: strcpy(t, "TELETEXT2"); break;
            case DMX_PES_SUBTITLE2: strcpy(t, "SUBTITLE2"); break;
            case DMX_PES_PCR2: strcpy(t, "PCR2"); break;

            case DMX_PES_AUDIO3: strcpy(t, "AUDIO3"); break;
            case DMX_PES_VIDEO3: strcpy(t, "VIDEO3"); break;
            case DMX_PES_TELETEXT3: strcpy(t, "TELETEXT3"); break;
            case DMX_PES_SUBTITLE3: strcpy(t, "SUBTITLE3"); break;
            case DMX_PES_PCR3: strcpy(t, "PCR3"); break;

            case DMX_PES_OTHER: strcpy(t, "OTHER"); break;
            default: strcpy(t, "Unknown"); break;
          }
          if(dmx->flags)
            sprintf(str,"    flags: %s%s%s%s\n",
                   (dmx->flags & DMX_CHECK_CRC ? " CHECK_CRC" : ""),
                   (dmx->flags & DMX_ONESHOT ? " ONESHOT" : ""),
                   (dmx->flags & DMX_IMMEDIATE_START ? " IMMEDIATE_START" : ""),
                   (dmx->flags & DMX_KERNEL_CLIENT ? " KERNEL_CLIENT" : ""));
          tmprintf("","DMX_SET_PES_FILTER(%d): pid:%d in:%s out:%s type:%s\n%s",
                 fdptr->fd, dmx->pid,
                 dmx->input == DMX_IN_FRONTEND ? "FRONTEND" : "DVR",
                 dmx->output == DMX_OUT_DECODER ? "DECODER" :
                     (dmx->output == DMX_OUT_TAP ? "TAP" : "TS_TAP"), t, str);
        }
        break;
      case DMX_SET_BUFFER_SIZE:
        tmprintf("","DMX_SET_BUFFER_SIZE(%d): %d\n", fdptr->fd, d32);
        break;
      case DMX_GET_PES_PIDS:
        {
          __u16 *pid = (__u16 *)data;
          str[0] = 0;
          for(int i = 0; i < 5; i++) {
            if(pid[i] <= 0)
              break;
            tmprintf(str1," %d", pid[i]);
            strcat(str, str1);
          }
          tmprintf("","DMX_GET_PES_PIDS(%d):%s\n", fdptr->fd, str);
        }
        break;
      case DMX_GET_CAPS:
        {
          struct dmx_caps *dmx = (struct dmx_caps*)data;
          tmprintf("","DMX_GET_CAPS(%d): %u num:%d\n", fdptr->fd, dmx->caps,
                 dmx->num_decoders);
        }
        break;
      case DMX_SET_SOURCE:
        tmprintf("","DMX_SET_SOURCE(%d): %d\n", fdptr->fd, d32);
        break;
      case DMX_GET_STC:
        tmprintf("","DMX_GET_STC(%d)\n", fdptr->fd);
        break;
    }
}

static void connect_imsg(struct parser_adpt *pc_all)
{
  ATTACH_CALLBACK(&pc_all->frontend->post_ioctl, fe_ioctl,    -1);
  ATTACH_CALLBACK(&pc_all->demux->post_ioctl,    demux_ioctl, -1);
}

//list, plugin_id, name, parse_args, connect, launch, message, send_msg
static struct plugin_cmd plugin_cmds = {{NULL, NULL}, PLUGIN_ID,
                       "ioctl debug",
                       NULL, connect_imsg, NULL, NULL, NULL, NULL, NULL};
int __attribute__((constructor)) __showioctl_init(void)
{
  list_add(&plugin_cmds.list, &plugin_cmdlist);
  return 0;
}
