/*
   DVBLoopback - plugin_scan.c
   Copyright AxesOfEvil 2007

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
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
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
#include "plugin_getsid.h"
#include "plugin_msg.h"

#define PLUGIN_ID 30
#define DBG_NAME "Scanner"
#include "debug.h"

static LIST_HEAD(scanner_list);

static int scan_opt = 0;
static int scan_ports = 0;
static struct option Scan_Opts[] = {
  {"scan-ports", 1, &scan_opt, 'p'},
  {0, 0, 0, 0},
};
//SCAN_MIN_WAITTIME: minimum time between an app closing the frontend and the scanner starting
#define SCAN_MIN_WAITTIME (10)
//SCAN_WAITTIME: time between completing a scan on the current port and starting it again
#define SCAN_WAITTIME (10*60)
//SCAN_TIMEOUT: time to watch a specific port for updates if the keys can't be found
#define SCAN_TIMEOUT (30*60)
//SCAN_MIN_TUNETIME: Minimum time to wait before switching to next port (only used after
//                   Each port has successfully tuned once
#define SCAN_MIN_TUNETIME (10*60)
//SCAN_CAM_MINTIME: Minimum time between even/odd keys for them to be considered valid.
//                  Don't play with this unless you know what you are doing
#define SCAN_CAM_MINTIME 5

#define MAX_PORTS 16


extern char * get_camdir();
static char scan_script[256], scan_lib[256];

struct scanner {
  struct list_head list;
  struct poll_ll *fe_fdptr;
  int fecount;
  int has_tuned;
  int port;
  int pid;
  int waiting;
  int done;
  struct parser_adpt *pc_all;
  time_t lastcmd;
  time_t starttime[MAX_PORTS];
  time_t found[MAX_PORTS];
  struct {
    int state;
    int last_pol;
    time_t lasttime;
  } ca[8];
};
unsigned char boo[1024];
static pthread_mutex_t scanner_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t scanner_cond = PTHREAD_COND_INITIALIZER;
static pthread_t scanner_thread;

static struct scanner *find_scanner_from_pc(struct parser_cmds *pc)
{
  struct list_head *ptr;
  list_for_each(ptr, &scanner_list) {
    struct scanner *scanner = list_entry(ptr, struct scanner);
    if(scanner->pc_all->frontend->common == pc->common)
      return scanner;
  }
  return NULL;
}

void * scan_loop(void *arg)
{
  struct scanner *scanner;
  struct list_head *ptr;
  time_t curtime;
  pthread_mutex_lock(&scanner_mutex);
  while(1) {
    int ok = 0;
    while(! ok) {
      curtime = time(NULL);
      list_for_each(ptr, &scanner_list) {
        scanner = list_entry(ptr, struct scanner);
        if(scanner->fecount==0 && curtime - scanner->lastcmd >= SCAN_MIN_WAITTIME) {
          ok = 1;
          break;
        }
      }
      if(! ok) {
        pthread_cond_wait(&scanner_cond, &scanner_mutex);
      }
    }
    //We can start the scanner if we get here
    if(! scanner->pid) {
      if(curtime > scanner->starttime[scanner->port] + SCAN_WAITTIME) {
        //Time to try scanning the current port
        scanner->done = 0;
        for(int i = 0; i < 8; i++) {
          scanner->ca[i].lasttime = curtime;
          scanner->ca[i].state = 0;
        }
        dprintf0("Starting scanner on port %d\n", scanner->port);
        if(0 == (scanner->pid = fork())) {
          char env[256], *env1[3] = {env, (char *) 0};
          struct { char a[8], b[8], c[8];} arg = {"scanner", "", ""};
          char *arg1[] = {arg.a, arg.b, arg.c, (char *) 0};
          snprintf(env,sizeof(env), "LD_PRELOAD=%s", scan_lib);
          snprintf(arg.b, 8, "%d", scanner->pc_all->frontend->common->virt_adapt);
          snprintf(arg.c, 8, "%d", scanner->port);
          int fd = open("/dev/null", O_WRONLY);
          while (dup2(fd, 1) < 0 && errno == EINTR)
            ;
          while (dup2(fd, 2) < 0 && errno == EINTR)
            ;
          while (close(fd) < 0 && errno == EINTR)
            ;
          execve(scan_script, arg1, env1);
          exit(0);
        }
        scanner->starttime[scanner->port] = curtime;
      }
    } else if(waitpid(scanner->pid, NULL, WNOHANG)) {
      scanner->pid = 0;
      scanner->port = (scanner->port+1) % scan_ports;
      scanner->waiting = 0;
    } else if(! scanner->waiting &&
              (  (scanner->done
                    && (! scanner->found[scanner->port]
                          || curtime > scanner->starttime[scanner->port] + SCAN_MIN_TUNETIME)
                 )
                 || curtime > scanner->starttime[scanner->port] + SCAN_TIMEOUT
              )) {
      if(scanner->done) {
        dprintf0("Scan complete for port %d.  Terminating %d\n", scanner->port, scanner->pid);
        scanner->found[scanner->port] = 1;
      }
      kill(scanner->pid, 9);
      scanner->waiting = 1;
    }
    scanner->lastcmd = curtime;
  }
}
static void process_scan(struct msg *msg, unsigned int priority)
{
  if (msg->type == MSG_TRYSCAN) {
    msg->type = MSG_PROCESSED;
    pthread_mutex_lock(&scanner_mutex);
    pthread_cond_signal(&scanner_cond);
    pthread_mutex_unlock(&scanner_mutex);
  } else if(msg->type == MSG_CAMUPDATE) {
    struct scanner *scanner;
    time_t curtime = time(NULL);
    int idx = (((unsigned long)msg->data)& 0xff) >> 1;
    int polarity = ((unsigned long)msg->data)& 0x01;
    msg->type = MSG_PROCESSED;
    ll_find_elem(scanner, scanner_list, pc_all->frontend->common->virt_adapt, msg->id, struct scanner);
    if(scanner == NULL || ! scanner->pid || scanner->waiting)
      return;
    if(polarity != scanner->ca[idx].last_pol &&
       curtime - scanner->ca[idx].lasttime > SCAN_CAM_MINTIME) {
      if(scanner->ca[idx].state < 4)
        scanner->ca[idx].state++;
      if(scanner->ca[idx].state == 4) {
        pthread_mutex_lock(&scanner_mutex);
        scanner->done = 1;
        pthread_mutex_unlock(&scanner_mutex);
      }
    } else {
      pthread_mutex_lock(&scanner_mutex);
      scanner->done = 0;
      pthread_mutex_unlock(&scanner_mutex);
      scanner->ca[idx].state = 0;
    }
    scanner->ca[idx].last_pol = polarity;
    scanner->ca[idx].lasttime = curtime;
    dprintf0("id:%d idx: %d state:%d pol:%d done:%d\n", msg->id, idx, scanner->ca[idx].state, polarity, scanner->done);
  }
}

static void fe_open_pre(struct parser_cmds *pc, struct poll_ll *fdptr,
                         cmdret_t *result, int *ret,
                         unsigned long int cmd, unsigned char *data)
{
  struct scanner *scanner;
  scanner = find_scanner_from_pc(pc);
  if(scanner == NULL)
    return;

  if((fdptr->flags & O_ACCMODE) == O_RDONLY)
    return;

  pthread_mutex_lock(&scanner_mutex);
  if(scanner->fe_fdptr) {
    if((fdptr->flags & (O_SYNC | O_ASYNC)) != (O_SYNC | O_ASYNC)) {
      //This isn't the scanner fd so kill the scanner
      if(scanner->pid && ! scanner->waiting) {
        kill(scanner->pid, 9);
        scanner->waiting = 1;
      }
      close(scanner->fe_fdptr->fd);
      scanner->fe_fdptr->fd = open("/dev/null", O_RDWR);
      scanner->fe_fdptr = NULL;
      scanner->lastcmd = time(NULL);
    }
  }
  pthread_mutex_unlock(&scanner_mutex);
}

static void fe_open_post(struct parser_cmds *pc, struct poll_ll *fdptr,
                         cmdret_t *result, int *ret,
                         unsigned long int cmd, unsigned char *data)
{
  struct scanner *scanner;
  scanner = find_scanner_from_pc(pc);
  if(scanner == NULL)
    return;

  if((fdptr->flags & O_ACCMODE) == O_RDONLY)
    return;
  if(fdptr->fd < 0)
    return;
  pthread_mutex_lock(&scanner_mutex);
  if((fdptr->flags & (O_SYNC | O_ASYNC)) == (O_SYNC | O_ASYNC)) {
    // This is the scanner fd
    scanner->fe_fdptr = fdptr;
  } else {
    scanner->fecount++;
    scanner->lastcmd = time(NULL);
  }
  pthread_mutex_unlock(&scanner_mutex);
}

static void fe_close(struct parser_cmds *pc, struct poll_ll *fdptr,
                         cmdret_t *result, int *ret,
                         unsigned long int cmd, unsigned char *data)
{
  struct scanner *scanner;
  scanner = find_scanner_from_pc(pc);
  if(scanner == NULL)
    return;

  if((fdptr->flags & O_ACCMODE) == O_RDONLY)
    return;

  pthread_mutex_lock(&scanner_mutex);
  if((fdptr->flags & (O_SYNC | O_ASYNC)) == (O_SYNC | O_ASYNC)) {
    // This is the scanner fd
    scanner->fe_fdptr = NULL;
  } else {
    if(scanner->fecount > 0)
      scanner->fecount--;
    scanner->lastcmd = time(NULL);
  }
  pthread_mutex_unlock(&scanner_mutex);
}

#define FIND_CMD(result, adpt, type, _id) do { \
  struct cmd_list *cmd; \
  ll_find_elem(cmd, adpt->pre_##type, id, (_id), struct cmd_list); \
  if(! cmd) \
    ll_find_elem(cmd, adpt->post_##type, id, (_id), struct cmd_list); \
  if(! cmd) \
    result = NULL;\
  else \
    result = cmd->cmd; \
  } while(0);

static void launch_scan()
{
  if(scan_ports) {
    pthread_create(&scanner_thread, &default_attr, scan_loop, NULL);
    msg_send_replace(MSG_LOW_PRIORITY, MSG_TRYSCAN, 0, NULL,
                     10, MSG_RECURRING);
  }
}

static void connect_scan(struct parser_adpt *pc_all)
{
  struct scanner  *scanner;
  struct stat st;
  scanner = (struct scanner *)malloc(sizeof(struct scanner));
  if(! scanner)
    return;
  bzero(scanner, sizeof(struct scanner));
  if(scan_ports) {
    snprintf(scan_script, sizeof(scan_script), "%s/scanner", get_camdir());
    snprintf(scan_lib, sizeof(scan_lib), "%s/libscanwrap.so", get_camdir());
    if(stat(scan_script, &st) == -1 || ! (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))) {
      dprintf0("Couldn't locate executable %s\n", scan_script);
      exit(1);
    }
    if(stat(scan_lib, &st) == -1) {
      dprintf0("Couldn't locate wrapper lib %s\n", scan_lib);
      exit(1);
    }
  }
  scanner->pc_all = pc_all;
  scanner->lastcmd = time(NULL);
  ATTACH_CALLBACK(&pc_all->frontend->pre_open, fe_open_pre, -1);
  ATTACH_CALLBACK(&pc_all->frontend->post_open, fe_open_post, -1);
  ATTACH_CALLBACK(&pc_all->frontend->post_close, fe_close, -1);
  list_add_tail(&scanner->list, &scanner_list);
}

static struct option *parseopt_scan(arg_enum_t cmd)
{
  if(cmd == ARG_INIT) {
    return Scan_Opts;
  } 
  if(cmd == ARG_HELP) {
    printf("   --scan-ports <num_ports>: Turn on scanner\n");
  } 
  if(! scan_opt)
    return NULL;

  switch(scan_opt) {
    case 'p':
      scan_ports = strtol(optarg, NULL, 0);
      dprintf0("Enabling scanner on %d ports\n", scan_ports);
      break;
  }
  //must reset scan_opt after every call
  scan_opt = 0;
  return NULL;
}

//list, plugin_id, name, parse_args, connect, launch, message, send_msg
static struct plugin_cmd plugin_cmds = {{NULL, NULL}, PLUGIN_ID, "scanner",
                     parseopt_scan, connect_scan, launch_scan, process_scan, NULL, NULL, NULL};

int __attribute__((constructor)) __scan_init(void)
{
  // communicates the existance of teh current plugin to the main program.
  if(1) list_add(&plugin_cmds.list, &plugin_cmdlist);
  return 0;
}
