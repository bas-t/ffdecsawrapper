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
#include <assert.h>

#include "list.h"
#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>
#include <linux/dvb/version.h>
#include "plugin_getsid.h"
#include "msg_passing.h"

#ifndef FE_SET_FRONTEND2
  #define FE_SET_FRONTEND2 FE_SET_FRONTEND
#endif

static LIST_HEAD(getsidlist);

#define DBG_NAME "CHANNEL"
#define PLUGIN_ID PLUGIN_GETSID
#include "debug.h" //This is required to happen AFTER PLUGIN_ID is defined

struct filter {
  struct list_head list;  //this must remain first!!!
  struct list_head sids;  //contains sids to find for this fd
  __u16 pid;
  int   fd;
  int used;
  int parse_err;
};

struct sidnum {
  struct list_head list;
  int sid;
  int seen;
};

//ISO 13818.1 says there can only be one PAT section, but I've already coded
//it so let's leave the capability
#define MAX_PAT_SECTIONS 5
struct pat {
  int patfd;
  int version;
  unsigned char last_section; 
  unsigned char section_seen[MAX_PAT_SECTIONS];
  int has_nit;
  struct list_head dmx_filter_ll;
};

#define MAX_SIMULTANEOUS_PMT 32

static int sid_opt = 0;
static int opt_maxfilters = 2;
static int opt_max_fail = 20;
static int opt_allpids = 0;
static int opt_resetpidmap = 0;
static int opt_experimental = 0;
static int opt_orbit = 0;
static char *opt_ignore = 0;
static char *opt_unseen = 0;
static int opt_maxrestart = 0;
static struct option Sid_Opts[] = {
  {"sid-filt", 1, &sid_opt, 'f'},
  {"sid-allpid", 0, &sid_opt, 'p'},
  {"sid-nocache", 0, &sid_opt, 'c'},
  {"sid-orbit", 1, &sid_opt, 'o'},
  {"sid-experimental", 0, &sid_opt, 'e'},
  {"sid-ignore", 1, &sid_opt, 'i'},
  {"sid-restart", 1, &sid_opt, 'r'},
  {"sid-unseen", 1, &sid_opt, 'u'},
  {0, 0, 0, 0},
};

static pthread_mutex_t list_lock = PTHREAD_MUTEX_INITIALIZER;
LIST_HEAD(sid_empty_queue);
LIST_HEAD(sidnum_empty_queue);
LIST_HEAD(epid_empty_queue);
LIST_HEAD(dmxcmd_empty_queue);
LIST_HEAD(sidmsg_empty_queue);
LIST_HEAD(filt_empty_queue);

void free_sidmsg(struct sid_msg *sidmsg) {
  list_add_l(&sidmsg->list, &sidmsg_empty_queue, &list_lock);
}

void free_addsid_msg(void *msg) {
  struct sid_msg *sidmsg = (struct sid_msg *)msg;
  free_sidmsg(sidmsg);
}

static int add_pid(int patfd, int pid) {
  struct dmx_sct_filter_params sct_filter;

  bzero(&sct_filter, sizeof(struct dmx_sct_filter_params));
  sct_filter.pid      = (__u16) pid;
  sct_filter.flags    = DMX_IMMEDIATE_START;
  if(ioctl(patfd, DMX_SET_BUFFER_SIZE, 0x40000) < 0) {
    dprintf0("Failed to set buffersize on fd:%d, pid:%d (err:%d)\n",
             patfd,pid, errno);
    return 1;
  }
  if (ioctl(patfd, DMX_SET_FILTER, &sct_filter) < 0) {
    dprintf0("Failed to set filter on fd:%d pid:%d (err: %d)\n",
             patfd, pid, errno);
    return 1;
  }
  return 0;
}

static void free_sid(struct sid *sid_ll) {
  struct epid *epid_ll;
  pthread_mutex_lock(&list_lock);
  while(! list_empty(&sid_ll->epid)) {
    epid_ll = list_entry(sid_ll->epid.next, struct epid);
    list_del(&epid_ll->list);
    list_add(&epid_ll->list, &epid_empty_queue);
  }
  list_add(&sid_ll->list, &sid_empty_queue);
  pthread_mutex_unlock(&list_lock);
}

static void free_pat(struct pat *pat)
{
  while(! list_empty(&pat->dmx_filter_ll)) {
    struct filter *filt = list_entry(pat->dmx_filter_ll.next, struct filter);
    list_del(&filt->list);
    while(! list_empty(&filt->sids)) {
      struct sidnum *sidnum = list_entry(filt->sids.next, struct sidnum);
      list_del(&sidnum->list);
      list_add_l(&sidnum->list, &sidnum_empty_queue, &list_lock);
    }
    if(filt->fd > 0) {
      close(filt->fd);
    }
    list_add_l(&filt->list, &filt_empty_queue, &list_lock);
  }
}

static int read_pat(unsigned char *pes, struct pat *pat, unsigned int size) {
  unsigned int sec, end;
  int version;
  unsigned char *ptr, last_sec;
  struct sidnum *sidnum;

  if (pes[0] != 0x00) {
    dprintf0(
             "read_pat: expected PAT table 0x00 but got 0x%02x\n", pes[0]);
    return 1;
  }
  end = (((pes[1] & 0x03) << 8 | pes[2]) + 3 - 4);
  if(end > size-4) {
    dprintf0("read_pat: invalid PAT table size (%d > %d)\n", end, size-4);
    return 1;
  }
  version = (pes[5] >> 1) & 0x1f;
  sec = pes[6];
  last_sec = pes[7];
  if(last_sec >= MAX_PAT_SECTIONS) {
    dprintf0("read_pat: illegal section count %d > %d\n",
             last_sec, MAX_PAT_SECTIONS);
    return 1;
  }
  if (pat->version != version || last_sec != pat->last_section) {
    pat->version = version;
    pat->last_section = last_sec;
    while(! list_empty(&pat->dmx_filter_ll)) {
      struct filter *filt = list_entry(pat->dmx_filter_ll.next, struct filter);
      list_del(&filt->list);
      while(! list_empty(&filt->sids)) {
        sidnum = list_entry(filt->sids.next, struct sidnum);
        list_del(&sidnum->list);
        list_add_l(&sidnum->list, &sidnum_empty_queue, &list_lock);
      }
      list_add_l(&filt->list, &filt_empty_queue, &list_lock);
    }
  }
  if(pat->section_seen[sec])
    return 0;
  pat->section_seen[sec] = 1;
  for(ptr = pes + 8; ptr < pes + end; ptr += 4) {
    int sid, pid;
    struct filter *filt;
    sid = (ptr[0] << 8) | ptr[1];
    pid = ((ptr[2] & 0x1F) << 8) | ptr[3];
    if(sid != 0) {
      dprintf2("found PID: %d for sid: %d\n", pid, sid);
      ll_find_elem(filt, pat->dmx_filter_ll, pid, pid, struct filter);
      if(! filt) {
        pop_entry_from_queue_l(filt, &filt_empty_queue, struct filter,
                               &list_lock);
        bzero(filt, sizeof(struct filter));
        INIT_LIST_HEAD(&filt->sids);
        list_add(&filt->list, &pat->dmx_filter_ll);
        filt->pid = pid;
      }
      pop_entry_from_queue_l(sidnum, &sidnum_empty_queue, struct sidnum,
                           &list_lock);
      sidnum->sid = sid;
      sidnum->seen = 0;
      list_add(&sidnum->list, &filt->sids);
    } else {
      pat->has_nit = pid;
      dprintf2("found NIT at PID: %d", pid);
    }
  }
  return 0;
}
/*
static int get_type(int type)
{
  switch (type) {
    case 2:  //video
      return 1;
    case 4:  //audio
      return 0;
  }
  return 5;
}
*/
unsigned char *parse_ca(unsigned char *buf, unsigned char *ca, int len)
{
  unsigned char *ptr = buf;
  int count = 0;
  while (len >= 2) {
    count = 2 + *(ca +1 );
    if (*ca != 0x09) {
      ca += count;
      len -= count;
      continue;
    }
    if(len < 0)
      return NULL;
    memcpy(ptr, ca, count);
    len -= count;
    ca += count;
    ptr += count;
  }
  return ptr;
}

static int read_nit(unsigned char *buf, struct nit_data *nit, unsigned int size) {
  int len, tsl_len, td_len, tag_len, network_desc_len;
  int network_id, pos, tag;
  if (buf[0] != 0x40 && buf[0] != 0x41 && buf[0] != 0x72) {
    dprintf0(
             "read_nit expected table 0x40  or 0x41 but got 0x%02x\n", buf[0]);
    return -1;
  }
  if (buf[0] != 0x40) {
    return 0;
  }
  len = ((buf[1] & 0x07) << 8) | buf[2];
  network_id = (buf[3]<<8) | buf[4];
  network_desc_len = ((buf[8] & 0x0f) << 8) | buf[9];
  tsl_len = ((buf[10+network_desc_len] & 0x0f) << 8) | buf[11+network_desc_len];
  pos = 12+network_desc_len;
  while (pos-network_desc_len-12 < tsl_len - 6 && pos < len - 3) {
    td_len = ((buf[pos+4] & 0x0f) << 8) | buf[pos+5];
    while (td_len > 0) {
      tag = buf[pos+6];
      tag_len = buf[pos+7];
      if(tag == 0x43 && tag_len >= 11) { //satellite descriptor
        nit->type = tag;
        nit->frequency = (buf[pos+8] << 24) | (buf[pos+9]<<16) | (buf[pos+10]<<8) | buf[pos+11];
        nit->orbit = (buf[pos+12] << 8) | buf[pos+13];
        nit->is_east = buf[pos+14] >> 7;
        nit->polarization = (buf[pos+14] >> 5) & 0x03;
        nit->modulation = buf[pos+14] & 0x1f;
        nit->symbolrate = (buf[pos+15]<<16) | (buf[pos+16]<<8) | buf[pos+17];
        nit->fec = buf[pos+18];
        printf("Orbit: %08x%c\n", nit->orbit, nit->is_east ? 'E' : 'W');
        return 1;
      } else if(tag == 0x44 && tag_len >= 11) { //cable descriptor
        nit->type = tag;
        nit->frequency = (buf[pos+8] << 24) | (buf[pos+9]<<16) | (buf[pos+10]<<8) | buf[pos+11];
        nit->modulation = buf[pos+14];
        nit->symbolrate = (buf[pos+15]<<16) | (buf[pos+16]<<8) | buf[pos+17];
        nit->fec = buf[pos+18];
        printf("Cable: %08x\n", nit->frequency);
        return 1;
      }
      pos += tag_len+2;
      td_len -= tag_len+2;
    }
    pos += 6;
  }
  return 0;
}

static bool cmp_wild(const char *pat, const char *s)
{
  while(1) {
    if (!*s || !*pat) return *pat==*s; // true if length matched
    if (*pat!='?' && *pat!=*s) return false;
    pat++; s++;
    }
}

static bool match_sid(int sid, const char *param)
{
  bool ret = false;
  if (param) {
    char s_sid[32];
    sprintf(s_sid,"%u",sid);

    char *save, *ignored = strdup(param);
    char *tok = strtok_r(ignored, ",", &save);
    while(tok) {
      if (cmp_wild(tok,s_sid)) {
        ret = true;
        break;
        }
      tok = strtok_r(0, ",", &save);
      }
    free(ignored);
    }
  return ret;
}

static int read_pmt(unsigned char *buf, struct filter *filt,
                    struct sid_data *sid_data, unsigned int size) {
  //
  // NOTE we aren't using last_sec here yet!
  //
  struct sid *sid_ll;
  struct epid *epid_ll;
  struct sidnum *sidnum;

  unsigned char *captr;
  unsigned int count, skip, pos;
  int sid, sec, last_sec, pcrpid, epid, type;
  if (buf[0] != 0x02) {
    dprintf0(
             "read_pmt expected table 0x02 but got 0x%02x\n", buf[0]);
    return -1;
  }
  count = (((buf[1] & 0x03) << 8) | buf[2]) + 3 - 4;
  sid = (buf[3] << 8) | buf[4];
  sec = buf[6];
  last_sec = buf[7];
  pcrpid = ((buf[8] & 0x1F) << 8) | buf[9];
  skip = ((buf[10] & 0x03) << 8) | buf[11];
  if(skip > count - 12 || count > size) {
    dprintf0("skip: %d > count: %d - 12 || count > size: %d\n",
           skip, count, size);
    return -1;
  }

  pop_entry_from_queue_l(sid_ll, &sid_empty_queue, struct sid, &list_lock);
  INIT_LIST_HEAD(&sid_ll->epid);

  captr = parse_ca(sid_ll->ca, buf + 12, skip);
  if(captr == NULL) {
    dprintf0("Bad CA found\n");
    free_sid(sid_ll);
    return -1;
  }
  dprintf3("read_pmt: sid: %d pcrpid: %d skip: %d count: %d\n", sid, pcrpid, skip, count); 
  ll_find_elem(sidnum, filt->sids, sid, sid, struct sidnum);
  if(sidnum == NULL) {
    dprintf1("Sid %d is unexpected\n", sid);
    free_sid(sid_ll);
    return -1;
  }
  if (match_sid(sid,opt_ignore)) {
    dprintf0("Ignoring SID %d.\n", sid);
    free_sid(sid_ll);
    return -1;
  }
  if(sidnum->seen && !match_sid(sid,opt_unseen)) {
    dprintf0("Already seen sid: %d\n", sid);
    free_sid(sid_ll);
    return -1;
  }
  sidnum->seen = 1;
  sid_ll->sid = sid;
  for(pos = 12 + skip; pos < count;) {
    type = buf[pos];
    epid = ((buf[pos+1] & 0x1F) << 8) | buf[pos+2];
    skip = ((buf[pos+3] & 0x03) << 8) | buf[pos+4];
    captr = parse_ca(captr, buf+pos+5, skip);
    pop_entry_from_queue_l(epid_ll, &epid_empty_queue, struct epid, &list_lock);
    
    epid_ll->epid = epid;
    epid_ll->type = type;
    list_add_tail(&epid_ll->list, &sid_ll->epid);
    dprintf3("read_pmt: epid %d (type %d) mapped to sid %d\n", epid, type, sid);
    pos += 5 + skip;
  }
  sid_ll->calen = captr - sid_ll->ca;
  pthread_mutex_lock(&sid_data->mutex);
  if(! sid_data->has_map) {
    pthread_mutex_unlock(&sid_data->mutex);
    free_sid(sid_ll);
    return -1;
  }
  list_add(&sid_ll->list, &sid_data->sidlist);
  pthread_mutex_unlock(&sid_data->mutex);
  return 0;
}

static int start(char *dmxdev, struct sid_data *sid_data, int timeout) {
  unsigned char pes[4096];
  struct pollfd  pfd, pollfd[MAX_SIMULTANEOUS_PMT];
  int pat_restart = 0, done = 0, size, i;
  int ret;

  struct pat pat;
  struct list_head *lptr;

  restart:

  bzero(&pat, sizeof(struct pat));
  INIT_LIST_HEAD(&pat.dmx_filter_ll);
  pat.patfd = open(dmxdev, O_RDWR | O_NONBLOCK);
  if(pat.patfd < 0) {
    perror("start: open returned:");
    free_pat(&pat);
    return 1;
  }
  if(add_pid(pat.patfd, 0x00)) {
    perror("start: failed to initialize pat demux:");
    free_pat(&pat);
    return 1;
  }

  pfd.fd = pat.patfd;
  pfd.events = POLLIN;
  pfd.revents = 0;
  while(! done) {
    if (poll(&pfd, 1, timeout) <= 0) continue;
    if ((size = read(pat.patfd, pes, sizeof(pes))) < 0) {
      dprintf0("start: pat read pes returned err: %d\n", errno);
      perror("start: pat read pes returned");
      close(pat.patfd);
      free_pat(&pat);
      return 1;
    }
///read_pat
    if(read_pat(pes, &pat, size)) {
      close(pat.patfd);
      free_pat(&pat);
      return 1;
    }
    done = 1;
    for (int i = 0; i < MAX_PAT_SECTIONS; i++) {
      if(pat.section_seen == 0) {
        done = 0;
        break;
      }
    }
  }
  close(pat.patfd);

  if(opt_orbit) {
    sid_data->nit.orbit = opt_orbit;
    pat.has_nit = 0;
  }

  if(pat.has_nit) {
    int fd;
    fd=open(dmxdev, O_RDWR | O_NONBLOCK);
    if (fd <= 0) {
      dprintf0("start: Failed to open demux device for NIT!\n");
      perror("demux open failed:");
    }
    else if (add_pid(fd, pat.has_nit)) {
        char str[80];
        sprintf(str, "start: Failed to initialize NIT demux");
        perror(str);
    } else {
      pfd.fd = fd;
      pfd.events = POLLIN;
      pfd.revents = 0;
      done = 0;
      int nit_retries = 0; 
      while(done <= 0) { 
        if (poll(&pfd, 1, timeout) <= 0) continue;
        if ((size = read(fd, pes, sizeof(pes))) < 0) {
          dprintf0("start: nit read pes returned err: %d\n", errno);
          perror("start: nit read pes returned");
        }
        done = read_nit(pes, &sid_data->nit, size);
        if (done <= 0) 
          nit_retries++; 
        if (nit_retries == 10) {
          if(++pat_restart<=opt_maxrestart) {
            dprintf0("start: giving up reading nit and restarting...\n");
            close(fd);
            free_pat(&pat);
            done = 0;
            goto restart;
          }
          dprintf0("start: giving up reading nit\n");
          done = 1;
        } 
      }
    }
    close(fd);
  }
 
  while(1) {
    int num_filters = 0, found = 0;
    ret = 0;

    list_for_each(lptr, &pat.dmx_filter_ll) {
      struct filter *filt = list_entry(lptr, struct filter);
      if(filt->used) {
        if(filt->fd >0) {
          close(filt->fd);
          filt->fd = -1;
        }
        continue;
      }
      filt->fd=open(dmxdev, O_RDWR | O_NONBLOCK);
      if (filt->fd <= 0) {
        dprintf0("start: Failed to open %dth demux device!\n", num_filters);
        perror("demux open failed:");
        ret = 1;
        break;
      }
      if (add_pid(filt->fd, filt->pid)) {
        char str[80];
        sprintf(str, "start: Failed to initialize %dth pmt demux", num_filters);
        perror(str);
        close(filt->fd);
        filt->fd = -1;
        ret = 1;
        break;
      }
      filt->used = 1;
      num_filters++;
      if(num_filters >= opt_maxfilters) {
        ret = 1;
        break;
      }
    }

    if(! num_filters)
      break;

    dprintf2("Reading %d of %d filters\n", num_filters, opt_maxfilters);
    while(found != num_filters) {
      int count = 0;
      list_for_each(lptr, &pat.dmx_filter_ll) {
        struct filter *filt = list_entry(lptr, struct filter);
        if (filt->used && filt->fd > 0 && ! list_empty(&filt->sids)) {
          pollfd[count].fd=filt->fd;
          pollfd[count].events = POLLIN;
          pollfd[count].revents = 0;
          count++;
        }
      }
      dprintf3("polling %d fds\n", count);
      poll(pollfd, count, timeout);
      for(i = 0; i < count; i++) {
        if(! (pollfd[i].revents & POLLIN))
          continue;
        // read pmt
        while((size = read(pollfd[i].fd, pes, sizeof(pes))) >= 3) {
          struct filter *filt;
          dprintf3("Read %d bytes\n", size);
          ll_find_elem(filt, pat.dmx_filter_ll, fd, pollfd[i].fd, struct filter);
          int ret = read_pmt(pes, filt, sid_data, size);
          if(ret < 0) {
             filt->parse_err++;
             if (filt->parse_err > opt_max_fail)
               goto exit;
          }
          if(ret == 0) {
            struct sidnum *sidnum;
            filt->parse_err = 0;
            ll_find_elem(sidnum, filt->sids, seen, 0, struct sidnum);
            if(sidnum == NULL) {
              found ++;
              break;
            }
          } else {
            pthread_mutex_lock(&sid_data->mutex);
            ret = ! sid_data->has_map;
            pthread_mutex_unlock(&sid_data->mutex);
            if(ret)
              goto exit;
          }
        }
      }
      pthread_mutex_lock(&sid_data->mutex);
      ret = ! sid_data->has_map;
      pthread_mutex_unlock(&sid_data->mutex);
      if(ret)
        goto exit;
    }
  }
exit:
  free_pat(&pat);
  return ret;
}

static int check_av(unsigned char type) {
  switch(type) {
    case MPEG1Video:
    case MPEG2Video:
    case MPEG4Video:
    case H264Video:
    case OpenCableVideo:
      return 1;
    case MPEG1Audio:
    case MPEG2Audio:
    case MPEG2AudioAmd1:
    case AACAudio:
    case AC3Audio:
    case DTSAudio:
      return 1;
    case MHEG:
    case H222_1:
      return 1;
    default:
      return 0;
  }
  return 0;
}

struct sid* find_pid(struct list_head *sidlist, unsigned int pid) {
  struct list_head *ptr, *ptr1;
  struct sid *sid_ll;
  struct epid *epid_ll;
  list_for_each(ptr, sidlist) {
    sid_ll = list_entry(ptr, struct sid);
    list_for_each(ptr1, &sid_ll->epid) {
      epid_ll = list_entry(ptr1, struct epid);
      if(epid_ll->epid == pid) {
        if(opt_allpids || check_av(epid_ll->type)) {
          return sid_ll;
        } else {
          dprintf3("Skipping pid %d because type is %d\n", epid_ll->epid,
                   epid_ll->type);
          return NULL;
        }
      }
    }
  }
  return NULL;
}
static void *read_sid(void *arg)
{
  struct sid_data *sid_data = (struct sid_data *)arg;
  struct sid *sid_ll;
  struct dmxcmd *dmxcmd;
  char dmxdev[256];
  int ret;

  sprintf(dmxdev, "/dev/dvb/adapter%d/demux0", sid_data->common->real_adapt);
  pthread_mutex_lock(&sid_data->mutex);
  while(1) {
    while(1) {
      ll_find_elem(dmxcmd, sid_data->cmdqueue, checked, 0, struct dmxcmd);
      if(dmxcmd) {
          break;
      }
      pthread_cond_wait(&sid_data->cond, &sid_data->mutex);
    }
    //pop_entry_from_queue_l(dmxcmd, &sid_data->cmdqueue, struct dmxcmd,
    //                       &list_lock);
    //dmxcmd_stored = 0;
    if(dmxcmd->fd == -1 && dmxcmd->sid == 0 && dmxcmd->sid == NULL)
      return NULL;
    if(! sid_data->has_map) {
      //Try to read pmt, use a short-timeout in case the lock
      //hasn't happened yet
      pthread_mutex_unlock(&sid_data->mutex);
      dprintf2("Got Start!\n");
      //set has_map first, since it could get invalidated...
      sid_data->has_map = 1;
      ret =  start(dmxdev, sid_data, 200 /*ms*/);
      dprintf2("returned: %d\n", ret);
      if(ret)
        usleep(200000); /*200ms*/
      pthread_mutex_lock(&sid_data->mutex);
      if(ret || sid_data->has_map == 0) {
        sid_data->has_map = 0;
        while(! list_empty(&sid_data->sidlist)) {
          sid_ll = list_entry(sid_data->sidlist.next, struct sid);
          list_del(&sid_ll->list);
          free_sid(sid_ll);
        }
      }
      //We skip processing in case the dmx was closed while pmt parsing
      continue;
    }
    dprintf2("Got Pid: %d\n", dmxcmd->pid);
    dmxcmd->checked = 1;
    if((sid_ll = find_pid(&sid_data->sidlist, dmxcmd->pid))) {
      dprintf1("Found sid: %lu\n", sid_ll->sid);
      dmxcmd->sid = sid_ll;
      if (sid_data->sendmsg) {
        struct list_head *lptr;
        struct sid_msg *sidmsg;
        struct epid *epid;
        int count = 0;
        //Duplicate dmxcmds to prevent a race
        pop_entry_from_queue_l(sidmsg, &sidmsg_empty_queue, struct sid_msg,
                               &list_lock);
        sidmsg->sid = dmxcmd->sid->sid;
        sidmsg->calen = dmxcmd->sid->calen;
        memcpy(sidmsg->ca, dmxcmd->sid->ca, dmxcmd->sid->calen);
        list_for_each(lptr, &dmxcmd->sid->epid) {
          epid = list_entry(lptr, struct epid);
          sidmsg->epid[count] = epid->epid;
          sidmsg->epidtype[count] = epid->type;
          count++;
        }
        sidmsg->epid_count = count;
        sidmsg->nit = sid_data->nit;
        msg_send(MSG_LOW_PRIORITY, MSG_ADDSID, sid_data->common->real_adapt,
                 sidmsg);
      } else {
        dprintf0("Didn't find sid for pid: %d\n", dmxcmd->pid);
      }
    }
  }
}

static struct sid_data *find_siddata_from_pc(struct parser_cmds *pc)
{
  struct list_head *ptr;
  list_for_each(ptr, &getsidlist) {
    struct sid_data *sid_data = list_entry(ptr, struct sid_data);
    if(sid_data->common == pc->common)
      return sid_data;
  }
  return NULL;
}

static void clear_sid_data(struct sid_data *sid_data)
{
    int adapt;
    struct sid *sid_ll;
    struct dmxcmd *dmxcmd;

  
    adapt = sid_data->common->real_adapt;
    while(! list_empty(&sid_data->sidlist)) {
      sid_ll = list_entry(sid_data->sidlist.next, struct sid);
      list_del(&sid_ll->list);
      free_sid(sid_ll);
    }
  
    //reset queued pids
    {
      struct list_head *lptr;
      list_for_each(lptr, &sid_data->cmdqueue) {
        dmxcmd = list_entry(lptr, struct dmxcmd);
        dmxcmd->checked = 0;
        dmxcmd->sid = NULL;
      }
    }
    //force a rescan of pmts
    sid_data->has_map = 0;
  
    pthread_cond_signal(&sid_data->cond);
}

static void fe_tune(struct parser_cmds *pc, struct poll_ll *fdptr,
                    cmdret_t *result, int *ret,
                    unsigned long int cmd, unsigned char *data)
{
  struct sid_data *sid_data;
  int adapt;

  sid_data = find_siddata_from_pc(pc);
  adapt = sid_data->common->real_adapt;
  if(sid_data == NULL)
    return;

  if(cmd == FE_SET_FRONTEND || cmd == FE_SET_FRONTEND2) {
    dprintf0("Tuning frontend\n");
    if(memcmp(&sid_data->tunecache, data, sizeof(struct dvb_frontend_parameters))) {
      pthread_mutex_lock(&sid_data->mutex);
      memcpy(&sid_data->tunecache, data, sizeof(struct dvb_frontend_parameters));
      if(sid_data->sendmsg) {
        msg_remove_type_from_list(MSG_LOW_PRIORITY, MSG_ADDSID, adapt,
                                  free_addsid_msg);
        msg_remove_type_from_list(MSG_LOW_PRIORITY, MSG_REMOVESID, adapt, NULL);
       msg_remove_type_from_list(MSG_LOW_PRIORITY, MSG_RESETSID, adapt, NULL);
      } 
      msg_send(MSG_LOW_PRIORITY, MSG_RESETSID, adapt, NULL);

      clear_sid_data(sid_data);

      pthread_mutex_unlock(&sid_data->mutex);
    } else {
      dprintf0("Skipping cache reset since tuning matches last tune\n");
      if(opt_experimental)
        *result = CMD_SKIPCALL;
    }
#if DVB_API_VERSION >=5
  } else if (cmd == FE_SET_PROPERTY) {
    dprintf0("Tuning frontend (new)\n");
    pthread_mutex_lock(&sid_data->mutex);
    if(sid_data->sendmsg) {
      msg_remove_type_from_list(MSG_LOW_PRIORITY, MSG_ADDSID, adapt,
                                  free_addsid_msg);
      msg_remove_type_from_list(MSG_LOW_PRIORITY, MSG_REMOVESID, adapt, NULL);
      msg_remove_type_from_list(MSG_LOW_PRIORITY, MSG_RESETSID, adapt, NULL);
    } 
    msg_send(MSG_LOW_PRIORITY, MSG_RESETSID, adapt, NULL);
    clear_sid_data(sid_data);
    pthread_mutex_unlock(&sid_data->mutex);
    memset(&sid_data->tunecache, 0, sizeof(struct dvb_frontend_parameters));
#endif
  } else if(cmd == FE_DISEQC_SEND_MASTER_CMD
            || cmd == FE_DISEQC_SEND_BURST
            || cmd == FE_SET_TONE
            || cmd == FE_SET_VOLTAGE
#ifdef FE_SET_STANDARD
            || cmd == FE_SET_STANDARD
#endif
#ifdef FE_DISHNETWORK_SEND_LEGACY_CMD
            || cmd == FE_DISHNETWORK_SEND_LEGACY_CMD
#endif
    ) 
  {
    dprintf0("Clearing tuning cache due to switch cmd\n");
    memset(&sid_data->tunecache, 0, sizeof(struct dvb_frontend_parameters));
  }
}

static void fe_close(struct parser_cmds *pc, struct poll_ll *fdptr,
                    cmdret_t *result, int *ret,
                    unsigned long int cmd, unsigned char *data)
{
  struct sid_data *sid_data;
  int adapt;

  sid_data = find_siddata_from_pc(pc);
  adapt = sid_data->common->real_adapt;

  if(sid_data == NULL)
    return;

  if((fdptr->flags & O_ACCMODE) == O_RDONLY)
    return;

  pthread_mutex_lock(&sid_data->mutex);
  if(sid_data->sendmsg) {
    msg_remove_type_from_list(MSG_LOW_PRIORITY, MSG_ADDSID, adapt,
                              free_addsid_msg);
    msg_remove_type_from_list(MSG_LOW_PRIORITY, MSG_REMOVESID, adapt, NULL);
    msg_remove_type_from_list(MSG_LOW_PRIORITY, MSG_RESETSID, adapt, NULL);
  }
  msg_send(MSG_LOW_PRIORITY, MSG_RESETSID, adapt, NULL);
  pthread_mutex_unlock(&sid_data->mutex);
}

//if a demux is closed, we really don't want to clear the pid<->sid map
//insead we should only do something when a mapped pid is closed, and the
//'something' should be to invalidate the associated sid
static void close_demux(struct parser_cmds *pc, struct poll_ll *fdptr,
                         cmdret_t *result, int *ret,
                         unsigned long int cmd, unsigned char *data)
{
  struct sid_data *sid_data;
  struct dmxcmd *dmxcmd, *tmpdmx;

  sid_data = find_siddata_from_pc(pc);
  if(sid_data == NULL)
    return;

  pthread_mutex_lock(&sid_data->mutex);
  //if(sid_data->sendmsg)
    //remove only GETSID on this demux!
    //msg_remove_type_from_list(MSG_LOW_PRIORITY, MSG_ADDSID, free_addsid_msg);

  //removed this fd from the cmdqueue
  ll_find_elem(dmxcmd, sid_data->cmdqueue, fd, fdptr->fd, struct dmxcmd);
  if(dmxcmd) {
    list_del(&dmxcmd->list);
    if(dmxcmd->checked && dmxcmd->sid) {
      msg_send(MSG_LOW_PRIORITY, MSG_REMOVESID, sid_data->common->real_adapt,
               (void *)(dmxcmd->sid->sid));
      //check that this sid is no longer being viewed
      ll_find_elem(tmpdmx, sid_data->cmdqueue, sid, dmxcmd->sid, struct dmxcmd);
      if(! tmpdmx) {
        sid_data->removed_sid = 1;
      }
    }
    list_add_l(&dmxcmd->list, &dmxcmd_empty_queue, &list_lock);
  }
  pthread_mutex_unlock(&sid_data->mutex);
}

static void set_demux(struct parser_cmds *pc, struct poll_ll *fdptr,
                      cmdret_t *result, int *ret,
                      unsigned long int cmd, unsigned char *data)
{
  struct sid_data *sid_data;
  struct dmxcmd *dmxcmd;
  int pid;

  sid_data = find_siddata_from_pc(pc);
  if(sid_data == NULL)
    return;

  if(cmd == DMX_SET_PES_FILTER) {
    pid = *(unsigned short *)data; //get pid from *filter_params
    pthread_mutex_lock(&sid_data->mutex);

    if(opt_resetpidmap && sid_data->removed_sid) {
      sid_data->removed_sid = 0;
      //NOTE: If this occurs while a 'ADD_SID' is in the message queue, 
      //then the message will be corrupted!  We should deep-copy the epids
      clear_sid_data(sid_data);
    }

    //Make sure we didn't just change pids on the same fd
    ll_find_elem(dmxcmd, sid_data->cmdqueue, fd, fdptr->fd, struct dmxcmd);
    if(dmxcmd) {
      if(dmxcmd->sid)
        msg_send(MSG_LOW_PRIORITY, MSG_REMOVESID, sid_data->common->real_adapt,
                 (void *)(dmxcmd->sid->sid));
      list_del(&dmxcmd->list);
      list_add_l(&dmxcmd->list, &dmxcmd_empty_queue, &list_lock);
    }

    pop_entry_from_queue_l(dmxcmd, &dmxcmd_empty_queue, struct dmxcmd,
                           &list_lock);
    dmxcmd->pid = pid;
    dmxcmd->fd = fdptr->fd;
    dmxcmd->sid = NULL;
    dmxcmd->checked = 0;
    list_add_tail(&dmxcmd->list, &sid_data->cmdqueue);

    dprintf1("Sending PID: %d\n", pid);
    pthread_cond_signal(&sid_data->cond);
    pthread_mutex_unlock(&sid_data->mutex);
  }
}

static void enable_msg(struct parser_cmds *pc, int enable)
{
  struct sid_data *sid_data = find_siddata_from_pc(pc);
  if(sid_data)
    sid_data->sendmsg = enable;
}

static void connect_sid(struct parser_adpt *pc_all)
{
  struct sid_data *sid_data;

  sid_data = (struct sid_data *)malloc(sizeof(struct sid_data));
  if(! sid_data)
    return;
  bzero(sid_data, sizeof(struct sid_data));
  //these shouldn't be needed
  sid_data->sendmsg = 0;
  sid_data->has_map = 0;
  sid_data->common = pc_all->frontend->common;
  pthread_mutex_init(&sid_data->mutex, NULL);
  pthread_cond_init(&sid_data->cond, NULL);
  INIT_LIST_HEAD(&sid_data->sidlist);
  INIT_LIST_HEAD(&sid_data->cmdqueue);
  if(opt_experimental) {
    ATTACH_CALLBACK(&pc_all->frontend->pre_ioctl,  fe_tune, -1);
  } else {
    ATTACH_CALLBACK(&pc_all->frontend->post_ioctl, fe_tune, -1);
  }
  ATTACH_CALLBACK(&pc_all->frontend->post_close, fe_close,    -1);
  ATTACH_CALLBACK(&pc_all->demux->post_ioctl,    set_demux,   -1);
  ATTACH_CALLBACK(&pc_all->demux->post_close,    close_demux, -1);
  list_add_tail(&sid_data->list, &getsidlist);
}

static void launch_sid()
{
  struct list_head *ptr;
  list_for_each(ptr, &getsidlist) {
    struct sid_data *sid_data = list_entry(ptr, struct sid_data);
    pthread_create(&sid_data->thread, &default_attr, read_sid, sid_data);
  }
}

static void shutdown_sid()
{
  struct dmxcmd *dmxcmd;
  struct list_head *ptr;
  list_for_each(ptr, &getsidlist) {
    struct sid_data *sid_data = list_entry(ptr, struct sid_data);
    if(sid_data->thread) {
      pthread_mutex_lock(&sid_data->mutex);
      pop_entry_from_queue_l(dmxcmd, &dmxcmd_empty_queue, struct dmxcmd,
                             &list_lock);
      dmxcmd->pid = 0;
      dmxcmd->fd = -1; //Send a terminate command
      dmxcmd->sid = NULL;
      dmxcmd->checked = 0;
      list_add(&dmxcmd->list, &sid_data->cmdqueue);
      pthread_cond_signal(&sid_data->cond);
      pthread_mutex_unlock(&sid_data->mutex);
      pthread_join(sid_data->thread, NULL);
    }
  }
}

static struct option *parseopt_sid(arg_enum_t cmd)
{
  if(cmd == ARG_INIT) {
    return Sid_Opts;
  } 
  if(cmd == ARG_HELP) {
    printf("   --sid-filt <num>  : Maximum number of open filters (default 2)\n");
    printf("   --sid-allpid      : Parse all pids instead of just A/V\n");
    printf("   --sid-ignore <sid1,sid2,...>\n");
    printf("                     : When tuning, ignore given SIDs\n");
    printf("   --sid-unseen <sid1,sid2,...>\n");
    printf("                     : When tuning, treat given SIDs as unseen allways\n");
    printf("   --sid-nocache     : Don't cache pid<->sid mapping\n");
    printf("   --sid-orbit <val> : Set the satellit orbit to 'val' and don't scan the NIT\n");
    printf("   --sid-experimental: Enable experimental tuning code\n");
    printf("   --sid-restart <n> : Max number of PAT/NIT read restarts\n");
  }
  if(! sid_opt)
    return NULL;

  switch(sid_opt) {
    case 'f':
      opt_maxfilters = atoi(optarg);
      if (opt_maxfilters < 1)
        opt_maxfilters = 1;
      else if (opt_maxfilters > MAX_SIMULTANEOUS_PMT)
        opt_maxfilters = MAX_SIMULTANEOUS_PMT;
      break;
    case 'p':
      opt_allpids = 1;
      break;
    case 'o':
      opt_orbit = strtol(optarg, NULL, 0);
      break;
    case 'c':
      opt_resetpidmap = 1;
      break;
    case 'e':
      opt_experimental = 1;
      break;
    case 'i':
      opt_ignore = optarg;
      break;
    case 'u':
      opt_unseen = optarg;
      break;
    case 'r':
      opt_maxrestart = atoi(optarg);
      break;
  }
  //must reset sid_opt after every call
  sid_opt = 0;
  return NULL;
}

//list, plugin_id, name, parse_args, connect, launch, message, send_msg
static struct plugin_cmd plugin_cmds = {{NULL, NULL}, PLUGIN_ID, "channel",
                parseopt_sid, connect_sid, launch_sid, NULL, enable_msg,
                shutdown_sid, NULL};
int __attribute__((constructor)) __getsid_init(void)
{
  list_add(&plugin_cmds.list, &plugin_cmdlist);
  return 0;
}
