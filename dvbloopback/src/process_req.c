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
#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <signal.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <sched.h>
#include <netinet/in.h>

#include "process_req.h"

#define DBG_NAME dnames[pc->type]
#define PLUGIN_ID (2*(pc->type-3))

#define DVB_DEVICE_FRONTEND   3
#define DVB_DEVICE_DEMUX      4
#define DVB_DEVICE_DVR        5
#define DVB_DEVICE_CA         6

#define MEMSIZE 100000

int find_free(struct parser_cmds *pc)
{
  int i;
  for(i=0; i < DVBLB_MAXFD; i++)
    if(pc->realfd_ll[i].kernfd == 0)
      return i;
  return -1;
}

struct poll_ll *find_pos(struct parser_cmds *pc, void *filp)
{
  struct list_head *ptr;
  struct poll_ll *entry;
  list_for_each(ptr, &pc->used_list) {
    entry = list_entry(ptr, struct poll_ll);
    if (entry->kernfd == filp)
      return entry;
  }
  return NULL;
}

// The poll_handler routine is used to trap a SIGPOLL
// which is sent to restart the poll-loop if something changes
void poll_handler(int num) {}
void *poll_loop(void * parm)
{
  struct parser_cmds *pc = (struct parser_cmds *)parm;
  int count;
  struct pollfd ufd[DVBLB_MAXFD+1];

  struct list_head *ptr;
  struct poll_ll *entry;

  struct dvblb_pollmsg msg;

  pthread_mutex_lock(&pc->poll_mutex);
  while(1) {
    int ret;
    pthread_cond_wait(&pc->poll_cond, &pc->poll_mutex);
    do {
      ret = 1;
      count = 0;
      list_for_each(ptr, &pc->used_list) {
        entry = list_entry(ptr, struct poll_ll);
        if(entry->poll) {
          ufd[count].fd = entry->fd;
          ufd[count].events = POLLIN;
          ufd[count].revents = 0;
          entry->pfd = &ufd[count];
          count++;
        }
      }
      if (count > 0) {
        pthread_mutex_unlock(&pc->poll_mutex);
        ret = poll(ufd, count, -1);
        pthread_mutex_lock(&pc->poll_mutex);
      }
    } while(ret <= 0);
    msg.count = 0;
    list_for_each(ptr, &pc->used_list) {
      entry = list_entry(ptr, struct poll_ll);
      if(entry->pfd && entry->kernfd && entry->poll && entry->pfd->revents) {
        msg.file[msg.count++] = entry->kernfd;
        entry->pfd = NULL;
        entry->poll = 0;
      }
    }
    if (msg.count) {
      ioctl(pc->virtfd, DVBLB_CMD_ASYNC, &msg);
    }
  }
  return NULL;
}

static cmdret_t do_cmd(struct list_head *list, struct parser_cmds *pc,
                         struct poll_ll *fdptr, int *ret,
                         unsigned long int cmd, unsigned char *data)
{
  cmdret_t result = CMD_NOCHANGE;
  struct list_head *ptr;
  struct cmd_list *entry;
  list_for_each(ptr, list) {
    entry = list_entry(ptr, struct cmd_list);
    if(entry->cmd)
      entry->cmd(pc, fdptr, &result, ret, cmd, data);
    if(result & CMD_STOP)
      break;
  }
  return result;
}

void get_thread_priority(char *str)
{
  struct sched_param param;
  int priority;
  int policy;
  int ret;
  sleep(1);
  /* scheduling parameters of target thread */
  ret = pthread_getschedparam (pthread_self(), &policy, &param);
  /* sched_priority contains the priority of the thread */
  priority = param.sched_priority;
  sprintf(str, "The thread scheduling parameters indicate:\n"
    "policy = %d\npriority = %d\n", policy, param.sched_priority);
}

//We can't allow blocking reads, since they could continue to stall even after
//The requesting app is terminated.  So we must emulate a blocking read by
//alwaysw opening with 'O_NONBLOCK', and doing a poll/read pair to fill the
//buffer
static int read_block(struct parser_cmds *pc, int fd, unsigned char *buf,
                      int numbytes) {
  int bytes = 0, ret;
  struct pollfd ufd[2];
  ufd[0].fd = fd;
  ufd[0].events = POLLIN | POLLPRI;
  ufd[1].fd = pc->virtfd;
  ufd[1].events = POLLIN | POLLPRI;
  //printf("Start read on dvr t:%lu\n", pthread_self());
  //wrPtr must always be an integer number of packets!
  while( bytes < numbytes) {
    dprintf1("Starting loop %d of %d\n", bytes, numbytes);
    //poll dvr fd and dvblb fd
    ufd[0].revents = 0;
    ufd[1].revents = 0;
    if(poll(&ufd[0], 2, -1) < 0 || ufd[1].revents) {
      //We should abort
      dprintf0("Aborting block read!\n");
      if(! errno)
        errno = EFAULT;
      return -1;
    }
    ret = read(fd, buf+bytes, numbytes-bytes);
    if (ret <= 0) {
      break;
    }
    bytes += ret;
  }
  if(bytes)
    return bytes;
  else
    return ret;
}

void *forward_data(void *parm)
{
  struct parser_cmds *pc = (struct parser_cmds *)parm;
  char realdev[256];
  char virtdev[256];
  char str[256];
  char *buf;
  void *file;
  int pos;

  struct poll_ll *fdptr;

  int size, ret;
  cmdret_t  result;
  unsigned char ioctldata[1024], *start, *ioctldatastart;
  unsigned long int cmd;
  struct pollfd poll_fds;
  size_t memsize = MEMSIZE;

  int offset = sizeof(unsigned long int) + sizeof (void *) - sizeof(int);
  if (offset < 0) {
    fprintf(stderr, "int is larger than long_int!  that can't be right.\n");
    exit(-1);
  }

  if(pc->type == DVB_DEVICE_DVR) {
    memsize = pc->common->buffersize;
  } else if(pc->type == DVB_DEVICE_DEMUX) {
    memsize =  pc->common->buffersize >> 3;
  }

  start = ioctldata + offset;
  ioctldatastart = ioctldata + offset + sizeof(int);

  sprintf(realdev, "/dev/dvb/adapter%d/%s0", pc->common->real_adapt,
                                             dnames[pc->type]);
  sprintf(virtdev, "/dev/dvb/adapter%d/%s1", pc->common->virt_adapt,
                                             dnames[pc->type]);

  pc->virtfd = open(virtdev, O_RDWR);
  if(pc->virtfd < 0) {
    dprintf0("Could not open %s. Error was: %d\n", virtdev, errno);
    perror("Open failed\n");
    exit(-1);
  }
  poll_fds.fd = pc->virtfd;

  pc->mmap=(unsigned char *)mmap(0, memsize, PROT_READ|PROT_WRITE,
                                 MAP_SHARED, pc->virtfd, 0);
  buf = (char *)malloc(memsize);

  if (! pc->mmap || ! buf) {
    fprintf(stderr, "Failed to execute mmap!\n");
    exit(-1);
  }
  pthread_mutex_init(&pc->poll_mutex, NULL);
  pthread_cond_init(&pc->poll_cond, NULL);
  INIT_LIST_HEAD(&pc->used_list);
  bzero(&pc->realfd_ll, sizeof(pc->realfd_ll));
  pthread_create(&pc->pollthread, &default_attr, poll_loop, pc);

  if(pc->type == DVB_DEVICE_DVR) {
    struct sched_param param;
    param.sched_priority = sched_get_priority_max(SCHED_FIFO);
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
  }
  get_thread_priority(str);
  dprintf0("Starting thread on %s\n%s", virtdev, str);

  //inform master thread that we're done
  pthread_mutex_lock(&pc->common->cond_lock);
  pc->common->cond_count--;
  pthread_cond_signal(&pc->common->cond);
  pthread_mutex_unlock(&pc->common->cond_lock);

  //main loop
  while(1) {
    int poll_signal = 0;
    poll_fds.events = POLLIN;
    poll_fds.revents = 0;
    do {
      ret = poll(&poll_fds, 1, -1);
      if(ret < 0)
        perror("Poll failed");
    } while (!(poll_fds.revents & POLLIN));
    size=read(pc->virtfd, ioctldata, 1024);
    if(size < (int)(sizeof(long int)+ sizeof(void *))) {
      dprintf0("Didn't read enough data\n");
      exit(-1);
    }
    cmd = *((unsigned long int *)ioctldata);
    file = *(void **)(ioctldata + sizeof(cmd));
    if (cmd < DVBLB_MAX_CMDS) {
      struct dvblb_custommsg *ci;
      struct pollfd ufd;
      ci = (struct dvblb_custommsg *)ioctldatastart;
      dprintf3("Got custom command: %d\n", ci->type);
      switch(ci->type) {
        case DVBLB_OPEN:
          pos = find_free(pc);
          if (pos < 0) {
            dprintf0("Too Many files open!\n");
            exit(-1);
            ret = -1;
            break;
          }
          fdptr = &pc->realfd_ll[pos];
          fdptr->flags = ci->u.mode;
          result = do_cmd(&pc->pre_open, pc, fdptr, &ret, cmd, ioctldatastart);
          if(!(result & CMD_SKIPCALL))
             fdptr->fd = open(realdev, fdptr->flags | O_NONBLOCK);
          if(!(result & CMD_SKIPPOST))
             do_cmd(&pc->post_open, pc, fdptr, &ret, cmd, ioctldatastart);
          if (fdptr->fd < 0) {
            ret = fdptr->fd;
            dprintf1("Open failed.  fd: %d\n", fdptr->fd);
            break;
          }
          fdptr->kernfd = file;
          fdptr->pfd = NULL;
          fdptr->poll = 0;

          pthread_mutex_lock(&pc->poll_mutex);
          list_add(&fdptr->list, &pc->used_list);
          pthread_mutex_unlock(&pc->poll_mutex);

          ret = 0;
          dprintf1("Opened fd: %d=%d mode: %o\n", pos, fdptr->fd, fdptr->flags);
          break;
        case DVBLB_CLOSE:
          fdptr = find_pos(pc, file);
          if (fdptr == NULL) {
            ret = -1;
            break;
          }
          //need to kill any polls before closing thread
          pthread_mutex_lock(&pc->poll_mutex);
          list_del(&fdptr->list);
          pthread_mutex_unlock(&pc->poll_mutex);
          pthread_kill(pc->pollthread, SIGPOLL);

          result = do_cmd(&pc->pre_close, pc, fdptr, &ret, cmd, ioctldatastart);
          if(!(result & CMD_SKIPCALL)) {
            ret = close(fdptr->fd);
            if (ret < 0) {
              perror("Close failed:");
            }
          }
          if(!(result & CMD_SKIPPOST))
            do_cmd(&pc->post_close, pc, fdptr, &ret, cmd, ioctldatastart);
          fdptr->kernfd = 0;
          ret = 0;
          dprintf1("Closed fd: %d ret: 0\n", fdptr->fd);
          break;
        case DVBLB_READ:
          fdptr = find_pos(pc, file);
          if (fdptr == NULL) {
            ret = -1;
            break;
          }
          if(ci->u.count > memsize) {
            dprintf2("Too many bytes: %zu > %zu\n", ci->u.count, memsize);
            ci->u.count = memsize;
          }
          {
            int orig_cnt = ci->u.count;
          result = do_cmd(&pc->pre_read, pc, fdptr, &ret, cmd, ioctldatastart);
          if(!(result & CMD_SKIPCALL)) {
            if(fdptr->flags & O_NONBLOCK)
              ret = read(fdptr->fd, pc->mmap, ci->u.count);
            else
              ret = read_block(pc, fdptr->fd, pc->mmap, ci->u.count);
            if (ret < 0)
              ret = -errno;
          }
          if(!(result & CMD_SKIPPOST))
            do_cmd(&pc->post_read, pc, fdptr, &ret, cmd, ioctldatastart);
          dprintf3("Read %d bytes (requested %zu or %d)\n",
                   ret,  ci->u.count, orig_cnt);
          }
          ci->u.count = ret;
          break;
        case DVBLB_WRITE:
          ret = -1;
          break;
        case DVBLB_POLL:
          fdptr = find_pos(pc, file);
          if (fdptr == NULL) {
            ret = -1;
            break;
          }
          result = do_cmd(&pc->pre_poll, pc, fdptr, &ret, cmd, ioctldatastart);
          if(!(result & CMD_SKIPCALL)) {
            ufd.fd = fdptr->fd;
            ufd.events = POLLIN | POLLPRI;
            ret = poll(&ufd, 1, 0);
            if(ret > 0 && !(ufd.revents & POLLIN))
              ret = 0;
            //Only start the poll thread if there isn't data on the real dev
            //if (ret == 0)
            //  poll_signal = 1;
            ci->u.mode = ufd.revents;
          }
          if(!(result & CMD_SKIPPOST))
            do_cmd(&pc->post_poll, pc, fdptr, &ret, cmd, ioctldatastart);
          if (ret == 0) {
            poll_signal = 1;
            fdptr->poll = 1;
          }
          dprintf3("Poll mode:%o ret:%d - sig: %d\n",
                   ci->u.mode, ret, poll_signal);
          break;
      }
    } else {
      dprintf2("Got command %lu (size: %d)\n", cmd & 0xff, size);
      fdptr = find_pos(pc, file);
      if (fdptr == NULL) {
        ret = -1;
      } else {
        result = do_cmd(&pc->pre_ioctl, pc, fdptr, &ret, cmd, ioctldatastart);
        if(!(result & CMD_SKIPCALL)) {
          if(_IOC_SIZE(cmd)) {
            ret = ioctl(fdptr->fd, cmd, ioctldatastart);
          } else {
            ret = ioctl(fdptr->fd, cmd, *(int *)ioctldatastart);
          }
          if(ret < 0)
            ret = -errno;
        }
        dprintf2("ioctl on (%d) returned: %d\n", fdptr->fd, ret);
        if(!(result & CMD_SKIPPOST))
          do_cmd(&pc->post_ioctl, pc, fdptr, &ret, cmd, ioctldatastart);
      }
    }
    *(int *)start = ret;
    pthread_mutex_lock(&pc->poll_mutex);
    // Need to protect ioctl call in addition to the cond
    ioctl(pc->virtfd, cmd, start);
    if(poll_signal) {
      pthread_kill(pc->pollthread, SIGPOLL); // restart poll if already polling
      pthread_cond_signal(&pc->poll_cond); //otherwise start polling
    }
    pthread_mutex_unlock(&pc->poll_mutex);
  }
}

static struct parser_cmds * init_device(struct common_data *common, int type)
{
  struct parser_cmds *pc;
  pc = (struct parser_cmds *)malloc(sizeof(struct parser_cmds));
  if(! pc)
    return NULL;
  bzero(pc, sizeof(struct parser_cmds));
  common->cond_count++; //this doesn't need to be protected (no threads yet)
  pc->common = common;
  pc->type = type;

  INIT_LIST_HEAD(&pc->pre_open);
  INIT_LIST_HEAD(&pc->post_open);

  INIT_LIST_HEAD(&pc->pre_close);
  INIT_LIST_HEAD(&pc->post_close);

  INIT_LIST_HEAD(&pc->pre_read);
  INIT_LIST_HEAD(&pc->post_read);

  INIT_LIST_HEAD(&pc->pre_poll);
  INIT_LIST_HEAD(&pc->post_poll);

  INIT_LIST_HEAD(&pc->pre_ioctl);
  INIT_LIST_HEAD(&pc->post_ioctl);
  return pc;
}

void launch_processors(struct parser_adpt *pc_all)
{
//pthread_attr_t attr;
  signal(SIGPOLL, poll_handler);
//pthread_attr_init(&attr);
//pthread_attr_setscope( &attr, PTHREAD_SCOPE_SYSTEM );
//pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
//pthread_attr_setschedpolicy( &attr, SCHED_FIFO );
  if(pc_all->frontend) {
    pthread_create(&pc_all->frontend->thread, &default_attr, forward_data,
                   pc_all->frontend);
  }
  if(pc_all->demux) {
    pthread_create(&pc_all->demux->thread, &default_attr, forward_data,
                   pc_all->demux);
  }
  if(pc_all->dvr) {
    pthread_create(&pc_all->dvr->thread, &default_attr, forward_data,
                   pc_all->dvr);
  }
  if(pc_all->ca) {
    pthread_create(&pc_all->ca->thread, &default_attr, forward_data,
                   pc_all->ca);
  }
}

static void kill_parser(struct parser_cmds *pc)
{
  if(pc->pollthread) {
    pthread_kill(pc->pollthread, SIGTERM);
    pthread_join(pc->pollthread, NULL);
  }
  if(pc->thread) {
    pthread_kill(pc->thread, SIGTERM);
    pthread_join(pc->thread, NULL);
  }
}
void shutdown_parser(struct parser_adpt *pc_all)
{
  if(pc_all->ca)
    kill_parser(pc_all->ca);
  if(pc_all->dvr)
    kill_parser(pc_all->dvr);
  if(pc_all->demux)
    kill_parser(pc_all->demux);
  if(pc_all->frontend)
    kill_parser(pc_all->frontend);
}

void init_parser(struct parser_adpt *pc_all, struct common_data *common)
{
  char realdev[255], virtdev[255];
  struct stat st;

  int types[] = {DVB_DEVICE_FRONTEND, DVB_DEVICE_DEMUX, DVB_DEVICE_DVR, -1};
  struct parser_cmds **ptr;
  int i;
  
  bzero(pc_all, sizeof(struct parser_adpt));
  for (i = 0; types[i] != -1; i++) {
    sprintf(realdev, "/dev/dvb/adapter%d/%s0", common->real_adapt,
                                               dnames[types[i]]);
    sprintf(virtdev, "/dev/dvb/adapter%d/%s0", common->virt_adapt,
                                               dnames[types[i]]);
    if (stat(realdev, &st) != 0 || stat(virtdev, &st) != 0) {
      continue;
    }
    ptr = (&pc_all->frontend) + i;// * sizeof(struct parser_cmds *);
    *ptr = init_device(common, types[i]);
  }
}

