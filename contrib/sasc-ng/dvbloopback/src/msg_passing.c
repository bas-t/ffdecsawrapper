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
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sched.h>
#include <linux/dvb/frontend.h>
#include <linux/dvb/dmx.h>
#include "process_req.h"
#include "plugin_getsid.h"
#include "plugin_ringbuf.h"
#include "msg_passing.h"

struct msgctrl {
  struct list_head msglist;
  struct list_head empty_queue;
  pthread_mutex_t mutex;
  pthread_cond_t cond;
};
static struct msgctrl message_control[MSG_HIGH_PRIORITY+1];

void msg_loop_init()
{
  struct msgctrl *msgctrl;
  int priority;
  int x;
  for (priority=0; priority <= MSG_HIGH_PRIORITY; priority++) {
    msgctrl = &message_control[priority];
    bzero(msgctrl, sizeof(struct msgctrl));
    INIT_LIST_HEAD(&msgctrl->msglist);
    INIT_LIST_HEAD(&msgctrl->empty_queue);
    for (x = 0; x < 10; x++) {
      struct msg *msg = (struct msg *)malloc(sizeof(struct msg));
      list_add(&msg->list, &msgctrl->empty_queue);
    }
    pthread_mutex_init(&msgctrl->mutex, NULL);
    pthread_cond_init(&msgctrl->cond, NULL);
  }
}
void * msg_loop(void * arg)
{
  unsigned long priority = (unsigned long)arg;
  struct list_head *ptr;
  struct msgctrl *msgctrl;

  if (priority == MSG_HIGH_PRIORITY) {
    struct sched_param param;
    param.sched_priority = sched_get_priority_min(SCHED_FIFO);
    if(pthread_setschedparam(pthread_self(), SCHED_FIFO, &param)) {
      perror("sched_setscheduler");
    }
  }
  if (priority > MSG_HIGH_PRIORITY) {
    tmprintf("MSG", "Invalid priority: %lu\n", priority);
    exit(-1);
  }

  msgctrl = &message_control[priority];
  pthread_mutex_lock(&msgctrl->mutex);
  while(1) { //main loop
    struct timespec ts = {0, 0};
    ts.tv_sec = time(NULL) + 60*60; //1 hour
    while(1) { //iterate over all elements in queue
      struct msg *msg;
      time_t now = time(NULL);
      int orig_type;

      //We don't need the 'safe' variant because we always break after removing
      //an element
      list_for_each(ptr, &msgctrl->msglist) {
        msg = list_entry(ptr, struct msg);
        if(now < msg->next_run) {
          if (msg->next_run < ts.tv_sec)
            ts.tv_sec = msg->next_run;
          continue;
        }
        list_del(&msg->list);
        break;
      }
      if (!msg)
        break;
      //If we've seen all elements on the queue, or the queue is empty,
      //we are done
      if(ptr == &msgctrl->msglist)
        break;
      pthread_mutex_unlock(&msgctrl->mutex);

      if(msg->type == MSG_TERMINATE)
        return NULL;

      orig_type = msg->type;
      list_for_each(ptr, &plugin_cmdlist) {
        struct plugin_cmd *cmd = list_entry(ptr, struct plugin_cmd);
        if(cmd->message)
          cmd->message(msg, priority);
        if(msg->type == MSG_PROCESSED)
          break;
      }
      if(msg->type != MSG_PROCESSED) {
        tmprintf("MSG", "Got unprocessed message type: %d\n", msg->type);
      }
      if (msg->recurring) {
        msg->next_run = time(NULL) + msg->recurring;
        msg->type = orig_type;
        if (msg->next_run < ts.tv_sec)
          ts.tv_sec = msg->next_run;
        pthread_mutex_lock(&msgctrl->mutex);
        list_add_tail(&msg->list, &msgctrl->msglist);
      } else {
        pthread_mutex_lock(&msgctrl->mutex);
        list_add(&msg->list, &msgctrl->empty_queue);
      }
    }
    pthread_cond_timedwait(&msgctrl->cond, &msgctrl->mutex, &ts);
  }
}

void msg_remove_type_from_list(unsigned int priority, int type, int id,
                               void (*cmd)(void *)) {
  struct list_head *ptr;
  struct msgctrl *msgctrl;

  if (priority > MSG_HIGH_PRIORITY)
    return;
  msgctrl = &message_control[priority];
  pthread_mutex_lock(&msgctrl->mutex);
  list_for_each(ptr, &msgctrl->msglist) {
    struct msg *msg = list_entry(ptr, struct msg);
    if (msg->type == type && msg->id == id) {
      list_del(&msg->list);
      if(cmd)
        cmd(msg->data);
      list_add(&msg->list, &msgctrl->empty_queue);
      break;
    }
  }
  pthread_mutex_unlock(&msgctrl->mutex);
}

static struct msg *msg_add_type_to_list(unsigned int priority, int type, int id,
                                        int replace) {
  struct list_head *ptr;
  struct msg *msg;
  struct msgctrl *msgctrl;

  msgctrl = &message_control[priority];

  if(replace) {
    //if a message of this type is already on the list
    list_for_each(ptr, &msgctrl->msglist) {
      msg = list_entry(ptr, struct msg);
      if (msg->type == type && msg->id == id) {
        return msg;
      }
    }
  }
  if(! list_empty(&msgctrl->empty_queue)) {
    msg = list_entry(msgctrl->empty_queue.next, struct msg);
    list_del(&msg->list);
  } else {
    msg = (struct msg *)malloc(sizeof(struct msg));
  }
  list_add_tail(&msg->list, &msgctrl->msglist);
  msg->type = type;
  msg->id = id;
  msg->recurring = 0;
  msg->next_run = 0;
  return msg;
}

void msg_send_replace(unsigned int priority, int type, int id, void *data,
                      int delay, unsigned int flags)
{
  struct msg *msg;
  struct msgctrl *msgctrl;

  if (priority > MSG_HIGH_PRIORITY)
    return;
  msgctrl = &message_control[priority];

  pthread_mutex_lock(&msgctrl->mutex);
  msg = msg_add_type_to_list(priority, type, id, flags & MSG_REPLACE);
  msg->data = data;
  if (delay)
    msg->next_run = delay + time(NULL);
  if (flags & MSG_RECURRING)
    msg->recurring = delay;
  pthread_cond_signal(&msgctrl->cond);
  pthread_mutex_unlock(&msgctrl->mutex);
}

void msg_terminate(unsigned int priority)
{
  struct msg *msg;
  struct msgctrl *msgctrl;
  msgctrl = &message_control[priority];
  pthread_mutex_lock(&msgctrl->mutex);
  if(! list_empty(&msgctrl->empty_queue)) {
    msg = list_entry(msgctrl->empty_queue.next, struct msg);
    list_del(&msg->list);
  } else {
    msg = (struct msg *)malloc(sizeof(struct msg));
  }
  msg->type = MSG_TERMINATE;
  msg->id = 0;
  msg->recurring = 0;
  msg->next_run = 0;
  list_add(&msg->list, &msgctrl->msglist);
  pthread_cond_signal(&msgctrl->cond);
  pthread_mutex_unlock(&msgctrl->mutex);
}
