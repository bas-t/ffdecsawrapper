#ifndef _MSG_PASSING_H_
#define _MSG_PASSING_H_
#include "list.h"

#define MSG_PROCESSED -1
#define MSG_TERMINATE -2
#define MSG_LOW_PRIORITY 0
#define MSG_HIGH_PRIORITY 1

#define MSG_REPLACE   0x01
#define MSG_RECURRING 0x02

struct msg {
  struct list_head list;
  int type;
  int id;
  int recurring;
  time_t next_run;
  void *data;
};

extern void msg_loop_init();
extern void *msg_loop(void *arg);
extern void msg_remove_type_from_list(unsigned int priority, int type, int id,
                               void (*cmd)(void *msg));
extern void msg_send_replace(unsigned int priority, int type, int id,
                       void *data, int delay, unsigned int flags);
extern void msg_terminate(unsigned int priority);
#define msg_send(a,b,c,d) msg_send_replace(a,b,c,d,0,0)
#define msg_replace(a,b,c,d) msg_send_replace(a,b,c,d,0,MSG_REPLACE)
#define msg_delayed(a,b,c,d,e,f) msg_send_replace(a,b,c,d,e, \
                                                  ((f)? MSG_RECURRING : 0)
#endif
