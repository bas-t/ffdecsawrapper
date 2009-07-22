#ifndef _PROCESS_REQ_H_
#define _PROCESS_REQ_H_
#include <getopt.h>
#include "list.h"
#include "msg_passing.h"
#include "dvbloopback.h"
#include "debug.h"

struct poll_ll {
	struct list_head list;  //this must remain first!!!
	void * kernfd;
	struct pollfd *pfd;
	int fd;
	unsigned int flags;
	int poll;
};

struct common_data {
  int virt_adapt;
  int real_adapt;
  unsigned long buffersize;
  void * private_data;
  pthread_mutex_t cond_lock;
  pthread_cond_t cond;
  int cond_count;
  
};


struct parser_cmds {
  int type;
  pthread_t thread;
  pthread_t pollthread;
  struct common_data *common;
  struct list_head pre_open;
  struct list_head post_open;
  struct list_head pre_close;
  struct list_head post_close;
  struct list_head pre_read;
  struct list_head post_read;
  struct list_head pre_poll;
  struct list_head post_poll;
  struct list_head pre_ioctl;
  struct list_head post_ioctl;
  unsigned char *mmap;
  void * private_data;

  /* everything from here down is private to the processor */
  struct poll_ll realfd_ll[DVBLB_MAXFD];
  struct list_head used_list;
  int virtfd;
  pthread_mutex_t poll_mutex;
  pthread_cond_t poll_cond;
};

struct parser_adpt {
  /* NOTE keep these in the same order as dnames! */
  struct parser_cmds *frontend;
  struct parser_cmds *demux;
  struct parser_cmds *dvr;
  struct parser_cmds *ca;
};

typedef enum {
  CMD_NOCHANGE = 0x00, //Default state
  CMD_STOP =     0x01, //Don't do any further processing in the current loop
  CMD_SKIPCALL = 0x02, //Don't execute the requested command
  CMD_SKIPPOST = 0x04, //Don't execute the post-call loop
  CMD_STOPALL  = 0x07,
} cmdret_t;

struct cmd_list {
  struct list_head list;
  void (*cmd) (struct parser_cmds *, struct poll_ll *, cmdret_t *, int *,
               unsigned long int, unsigned char *);
  unsigned int id;
};
/*
inline static struct cmd_list *register_cmd(void (*cmd) (struct parser_cmds *, struct poll_ll *, cmdret_t *, int *, unsigned long int, unsigned char *)) {
  struct cmd_list *reg = (struct cmd_list *)malloc(sizeof(struct cmd_list));
  reg->cmd = cmd;
  return reg;
}
*/
static inline void _attach_callback(struct list_head *cb,
        void (*cmd) (struct parser_cmds *, struct poll_ll *, cmdret_t *, int *, unsigned long int, unsigned char *),
        int priority, int _id) {
  struct cmd_list *reg = (struct cmd_list *)malloc(sizeof(struct cmd_list));
  reg->cmd = cmd;
  reg->id = _id;
  list_add_priority(&reg->list, cb, priority);
}
#define ATTACH_CALLBACK(_cb, _cmd, _pri) _attach_callback(_cb, _cmd, _pri, PLUGIN_ID)

//#define REGISTER_CMD(var, c) static struct cmd_list var = { {NULL, NULL}, c}

typedef enum {
  ARG_INIT,
  ARG_HELP,
  ARG_PARSE,
} arg_enum_t;

struct plugin_cmd {
  struct list_head list;
  int plugin;
  const char *name;
  struct option * (*parse_args)(arg_enum_t);
  void (*connect)(struct parser_adpt *);
  void (*launch)();
  void (*message)(struct msg *, unsigned int priority);
  void (*send_msg)(struct parser_cmds *pc, int msg);
  void (*shutdown)();
  void (*user_msg)(char *msg);
};

extern void launch_processors(struct parser_adpt *pc_all);
extern void shutdown_parser(struct parser_adpt *pc_all);
extern void init_parser(struct parser_adpt *pc_all, struct common_data *common);

extern struct list_head plugin_cmdlist;
extern pthread_attr_t default_attr;
#endif

