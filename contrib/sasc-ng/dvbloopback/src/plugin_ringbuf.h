#include <pthread.h>
#include "process_req.h"
#include "list.h"
#include "messages.h"

#define PLUGIN_RINGBUF 12

#define TSPacketSIZE 188
//#define RB_RESERVED 100000
//#define RB_BUFSIZE ((2000000 / TSPacketSIZE) * TSPacketSIZE)
//#define RB_BUFSIZE 1024 * TSPacketSIZE
enum {
  RB_CLOSED = 0,
  RB_CLOSING,
  RB_OPEN
};

struct ringbuffer {
  struct list_head list;
  int virtfd;
  int fd;
  unsigned int flags;
  unsigned char num;
  unsigned char *rdPtr;
  unsigned char *wrPtr;
  unsigned char *end;
  pthread_mutex_t rw_lock;
  void (*release)(struct ringbuffer *);
  int readok;
  int state;
  unsigned long reserved;
  //buffer MUST be last do to our allocation method
  unsigned char *buffer;
};

void ringbuf_register_callback(int (* cb)(struct ringbuffer *, int));
