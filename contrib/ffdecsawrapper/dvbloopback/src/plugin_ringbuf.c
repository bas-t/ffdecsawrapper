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
#include <errno.h>
#include <assert.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/ioctl.h>
#include <sys/timeb.h>
#include <signal.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include "process_req.h"
#include "plugin_ringbuf.h"
#include "msg_passing.h"

#define PLUGIN_ID PLUGIN_RINGBUF
#define DBG_NAME "RINGBUF"
#include "debug.h" //This is required to happen AFTER PLUGIN_ID is defined

//#define CHECK_READ_OK
//#define POLL_WORKS

//defining WRITE_RAW_DVR will creat /tmp/rawdvr.mpg and write out all data
//read from the 'real' dvr to that file
//#define WRITE_RAW_DVR
#ifdef WRITE_RAW_DVR
static int rawdvr;
#endif

static LIST_HEAD(ringbuflist);
static pthread_mutex_t list_lock = PTHREAD_MUTEX_INITIALIZER;
static int rb_sendmsg;
static int (* rb_check_read_callback)(struct ringbuffer *, int) = NULL;
#define min(a,b) ((a) < (b) ? (a) : (b))
inline int min1(int a, int b) { return min(a, b); }
unsigned int mtime() {
  struct timeb tp;
  unsigned int time0;
  ftime(&tp);
  time0 = tp.time * 1000 + tp.millitm;
  return time0;
}

static int rb_max_free(struct ringbuffer *rb)
{
  int allowed_bytes;
  if(rb->wrPtr >= rb->rdPtr)
    allowed_bytes = rb->end - rb->wrPtr + min1(rb->rdPtr - rb->buffer, rb->reserved) - TSPacketSIZE;
  else
    allowed_bytes = rb->rdPtr - rb->wrPtr - TSPacketSIZE;
  return allowed_bytes;
}

static int rb_free(struct ringbuffer *rb, int numbytes)
{
  return (rb_max_free(rb) >= numbytes);
}

static int rb_avail(struct ringbuffer *rb, int numbytes)
{
  int avail_bytes;
  if(rb->rdPtr > rb->wrPtr)
    avail_bytes = rb->end - rb->rdPtr + rb->wrPtr - rb->buffer - TSPacketSIZE;
  else
    avail_bytes = rb->wrPtr - rb->rdPtr - TSPacketSIZE;
  if (avail_bytes && avail_bytes >= numbytes && rb_check_read_callback) {
    avail_bytes = rb_check_read_callback(rb, numbytes);
  }
  return (avail_bytes);
}

static void rb_fill_bytes(struct ringbuffer *rb, int numbytes) {
  int bytes;
  unsigned char *newptr;
  //printf("Start read on dvr t:%lu\n", pthread_self());
  //wrPtr must always be an integer number of packets!
  numbytes -= numbytes % TSPacketSIZE;
  bytes = read(rb->fd, rb->wrPtr, numbytes);
  //printf("Read done\n");
  if (bytes <= 0) {
    if (!(rb->flags & O_NONBLOCK))
      perror("Read Failed");
    return;
  }
#ifdef WRITE_RAW_DVR
  write(rawdvr, rb->wrPtr, bytes);
#endif
  newptr = rb->wrPtr + bytes;
  if (newptr > rb->end)
    memcpy(rb->buffer, rb->end, newptr - rb->end);
  if (newptr >= rb->end)
    newptr = rb->buffer + (newptr - rb->end);

  pthread_mutex_lock(&rb->rw_lock);
  rb->wrPtr = newptr;
  pthread_mutex_unlock(&rb->rw_lock);
  if(rb_sendmsg)
    msg_replace(MSG_HIGH_PRIORITY, MSG_RINGBUF, rb->num, rb);
}

static int rb_fill_bytes_block(struct ringbuffer *rb, int numbytes) {
  int bytes = 0, ret, size;
  unsigned char *newptr;
  struct pollfd ufd[2];
  ufd[0].fd = rb->fd;
  ufd[0].events = POLLIN | POLLPRI;
  ufd[1].fd = rb->virtfd;
  ufd[1].events = POLLIN | POLLPRI;
  //printf("Start read on dvr t:%lu\n", pthread_self());
  //wrPtr must always be an integer number of packets!
  numbytes -= numbytes % TSPacketSIZE;
  while( bytes < numbytes && (size = rb_max_free(rb)) > 0) {
    dprintf1("Starting loop %d of %d\n", bytes, numbytes);
    //poll dvr fd and dvblb fd
    ufd[0].revents = 0;
    ufd[1].revents = 0;
    if(poll(&ufd[0], 2, -1) < 0 || ufd[1].revents) {
      //We should abort
      dprintf0("Aborting block read!\n");
      return (errno ? errno : EFAULT);
    }
    //opportunistically read as many bytes as possible
    ret = read(rb->fd, rb->wrPtr, size);
    if (ret <= 0) {
      break;
    }
#ifdef WRITE_RAW_DVR
    write(rawdvr, rb->wrPtr, ret);
#endif
    bytes += ret;
    newptr = rb->wrPtr + ret;
    if (newptr > rb->end)
      memcpy(rb->buffer, rb->end, newptr - rb->end);
    if (newptr >= rb->end)
      newptr = rb->buffer + (newptr - rb->end);

    pthread_mutex_lock(&rb->rw_lock);
    rb->wrPtr = newptr;
    pthread_mutex_unlock(&rb->rw_lock);
  }
  if(bytes && rb_sendmsg)
    msg_replace(MSG_HIGH_PRIORITY, MSG_RINGBUF, rb->num, rb);
  return 0;
}

static void rb_get_bytes(struct ringbuffer *rb, unsigned char *ptr,
                         int numbytes)
{
  unsigned char *newptr;
  newptr = rb->rdPtr;
  if (newptr + numbytes > rb->end) {
    memcpy(ptr, newptr, rb->end - newptr);
    numbytes -= rb->end - newptr;
    ptr += rb->end - newptr;
    newptr = rb->buffer;
  }
  memcpy(ptr, newptr, numbytes);
  pthread_mutex_lock(&rb->rw_lock);
  rb->rdPtr = newptr + numbytes;
  pthread_mutex_unlock(&rb->rw_lock);
}

static struct ringbuffer *find_rb_from_pc(struct parser_cmds *pc) {
  struct list_head *ptr;
  int num =  pc->common->virt_adapt; 
  list_for_each(ptr, &ringbuflist) {
    struct ringbuffer *rb = list_entry(ptr, struct ringbuffer);
    if (rb->num == num)
      return rb;
  }
  return NULL;
}

static void rb_release(struct ringbuffer *rb)
{
  dprintf1("Releasing ringbuffer: %d\n", rb->num);
  if(rb->state != RB_CLOSING) {
    dprintf0("Ringbuffer state should have been RB_CLOSING but was '%d;'\n",
              rb->state);
  }
  pthread_mutex_lock(&list_lock);
  rb->state = RB_CLOSED;
  pthread_mutex_unlock(&list_lock);
#ifdef WRITE_RAW_DVR
  close(rawdvr);
#endif
}

static void open_call(struct parser_cmds *pc, struct poll_ll *fdptr,
                      cmdret_t *result, int *ret, 
                      unsigned long int cmd, unsigned char *data)
{
  char realdev[256];

  struct ringbuffer *rb = find_rb_from_pc(pc);
  if(! rb)
    return;

  *result = CMD_SKIPCALL;
  if(rb->state == RB_OPEN) {
    dprintf0("Cannot open dvr; it is already open.\n");
    fdptr->fd = -EMFILE;
    return;
  }
  while(rb->state == RB_CLOSING) {
    //This isn't ideal, but the situation shouldn't last too long
    sched_yield();
  }
  sprintf(realdev, "/dev/dvb/adapter%d/%s0", pc->common->real_adapt,
                                             dnames[pc->type]);
  //we need to open nonblocking so we never get deadlocked
  fdptr->fd = open(realdev, fdptr->flags | O_NONBLOCK);
  if(fdptr->fd < 0) {
    fdptr->fd = -errno;
    perror("Failed to open dvr");
    return;
  }

  rb->virtfd = pc->virtfd;
  rb->fd = fdptr->fd;
  rb->flags = fdptr->flags;
  rb->rdPtr = rb->wrPtr = rb->buffer;
  pthread_mutex_lock(&list_lock);
  rb->state = RB_OPEN;
  rb->readok = 0;
  pthread_mutex_unlock(&list_lock);
  dprintf1("Creating ringbuffer: %d\n", rb->num);
#ifdef WRITE_RAW_DVR
  rawdvr = open("/tmp/rawdvr.mpg", O_WRONLY | O_CREAT | O_TRUNC, 0666);
#endif
}

static void close_call(struct parser_cmds *pc, struct poll_ll *fdptr,
                       cmdret_t *result, int *ret, 
                       unsigned long int cmd, unsigned char *data)
{
  struct ringbuffer *rb = find_rb_from_pc(pc);
  if(rb == NULL || rb->state != RB_OPEN)
    return;
  pthread_mutex_lock(&list_lock);
  rb->state = RB_CLOSING;
  pthread_mutex_unlock(&list_lock);
  //NOTE: need to use high priority queue to prevent a read/close race
  //MSG_RINGCLOSE MUST release the ringbuffer!
  if(rb_sendmsg)
    msg_replace(MSG_HIGH_PRIORITY, MSG_RINGCLOSE, rb->num, rb);
  else
    rb_release(rb);
}

static void poll_call(struct parser_cmds *pc, struct poll_ll *fdptr,
                      cmdret_t *result, int *ret, 
                      unsigned long int cmd, unsigned char *data)
{
  struct dvblb_custommsg *ci = (struct dvblb_custommsg *)data;
  int avail;
  struct ringbuffer *rb = find_rb_from_pc(pc);

  if(! rb || rb->state != RB_OPEN)
  {
    dprintf0("Shouldn't be here!\n");
    rb->readok = (*ret ? 1 : 0);
    return;
  }
  if(*ret) {
    //oportunistically fill the buffer, doesn't matter if we're blocking or not
    rb_fill_bytes(rb, rb_max_free(rb));
  }
  avail = rb_avail(rb, 0);
  if(avail > 0) {
    *ret = 1;
    ci->u.mode |= POLLIN;
    rb->readok = 1;
  } else {
    *ret = 0;
    ci->u.mode &= ~(POLLIN | POLLPRI);
    rb->readok = 0;
  }
}

static void read_call(struct parser_cmds *pc, struct poll_ll *fdptr,
                      cmdret_t *result, int *ret, 
                      unsigned long int cmd, unsigned char *data)
{
  struct dvblb_custommsg *ci = (struct dvblb_custommsg *)data;
  unsigned int trybytes, small = TSPacketSIZE * 1;
  int avail, err;
  struct ringbuffer *rb = find_rb_from_pc(pc);

  if(! rb || rb->state != RB_OPEN)
    return;

  *result = CMD_SKIPCALL;
//  ci->u.count -= ci->u.count % TSPacketSIZE;
  //trybytes must be multiple of TSPacketSIZE and > 0
  trybytes = ci->u.count % TSPacketSIZE;
  if(trybytes)
    trybytes =  ci->u.count + TSPacketSIZE - trybytes;
  else
    trybytes =  ci->u.count;
  avail = rb_avail(rb, 0);
  if (rb->flags & O_NONBLOCK) {
    if (avail > 0) {
       if ((unsigned int)avail > ci->u.count)
         avail = ci->u.count ;
       else
         ci->u.count = avail;
       rb_get_bytes(rb, pc->mmap, avail);
       *ret = ci->u.count;
    } else {
#ifdef CHECK_READ_OK
      assert(rb->readok != 1);
#endif
      *ret = -EAGAIN;
    }
    rb->readok = 0;
    rb_fill_bytes(rb, rb_max_free(rb));
    return;
  }
  //Blocking read
  if(avail > 0 && (unsigned int)avail >= ci->u.count) {
    // enough bytes can be read from ring buffer
    rb_get_bytes(rb, pc->mmap, ci->u.count);
    if((unsigned int)avail > ci->u.count * 16)
      trybytes = ci->u.count >> 2;
    else if((unsigned int)avail > ci->u.count * 4)
      trybytes = ci->u.count >> 1;
    if (trybytes >= TSPacketSIZE) {
      //NOTE: because trybytes < ci->u.count, and we just read
      // ci->u.count bytes from the ringbuffer, we KNOW that
      // there is room to read at least trybytes into the buffer
      trybytes -= trybytes % TSPacketSIZE;
      if((err = rb_fill_bytes_block(rb, trybytes)) != 0) {
        *ret = -err;
        return;
      }
    }
    *ret = ci->u.count;
    return;
  }
  // not enough bytes can be read from ring buffer
  while(1) {
    if(rb_free(rb, trybytes)) {
      //the ring buffer has room  to get 'trybytes' bytes
      printf("Buffer has room, reading %d bytes\n", trybytes);
      if((err = rb_fill_bytes_block(rb, trybytes)) != 0) {
        *ret = -err;
        return;
      }
      if(trybytes < ci->u.count)
        sched_yield(); //give up the rest of our timeslice
    } else {
      // signal decryptor
      // wait for decryptor
      dprintf0("Buffer is full, waiting for decode on %d bytes\n", trybytes);
      rb_avail(rb, ci->u.count);
    }
    if (rb_avail(rb, 0) >= (int)ci->u.count) {
      // enough bytes can be read from ring buffer
      rb_get_bytes(rb, pc->mmap, ci->u.count);
      *ret = ci->u.count;
      printf("Returning %d\n", *ret);
      return;
    }
    trybytes = small;
  }
}

static void enable_msg(struct parser_cmds *pc, int enable)
{
  rb_sendmsg = enable;
}

void ringbuf_register_callback(int (* cb)(struct ringbuffer *, int)) {
  rb_check_read_callback = cb;
}

static void connect_rb(struct parser_adpt *pc_all) {
  struct ringbuffer *rb;

  unsigned long rbsize = (1 + pc_all->dvr->common->buffersize / TSPacketSIZE)
                         * TSPacketSIZE;
  unsigned long rbextra = rbsize / 20;
  rb  = (struct ringbuffer *)malloc(sizeof(struct ringbuffer)+rbsize+rbextra);
  memset(rb, 0, sizeof(struct ringbuffer));
  pthread_mutex_init(&rb->rw_lock, NULL);
  rb->release = rb_release;
  rb->num = pc_all->dvr->common->virt_adapt;
  rb->buffer = (unsigned char *)&rb->buffer + sizeof(rb->buffer);
  rb->end = rb->buffer + rbsize;
  rb->state = RB_CLOSED;
  rb->reserved = rbextra;
  list_add_tail(&rb->list, &ringbuflist);

  ATTACH_CALLBACK(&pc_all->dvr->pre_open, open_call,   -1);
  ATTACH_CALLBACK(&pc_all->dvr->pre_close, close_call, -1);
  ATTACH_CALLBACK(&pc_all->dvr->post_poll, poll_call,  -1);
  ATTACH_CALLBACK(&pc_all->dvr->pre_read,  read_call,  -1);
}

//list, plugin_id, name, parse_args, connect, launch, message, send_msg
static struct plugin_cmd plugin_cmds = {{NULL, NULL}, PLUGIN_RINGBUF,
                             "ringbuffer",
                             NULL, connect_rb, NULL, NULL, enable_msg, NULL, NULL};
int __attribute__((constructor)) __ringbuf_init(void)
{
#ifndef NO_RINGBUF
  list_add(&plugin_cmds.list, &plugin_cmdlist);
#endif
  return 0;
}
