#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/ioctl.h>
#include "plugin_ringbuf.h"
#include "plugin_getsid.h"

#include "../FFdecsa/FFdecsa.h"

#define PLUGIN_ID 30
#define DBG_NAME "CSA"
#include "debug.h" //This is required to happen AFTER PLUGIN_ID is defined

#define FF_MAX_IDX 16
#define FF_MAX_PID 8

#define push_empty_queue(_item, _queue) {      \
          pthread_mutex_lock(&list_lock);    \
          list_add(_item, _queue);             \
          pthread_mutex_unlock(&list_lock);  \
}

struct keyindex {
  int valid;
  unsigned char even[8];
  unsigned char odd[8];
  void *keys;
  int queued;
  int status;
};

struct csastruct {
  list_head list;
  int adapter;
  ringbuffer *rb;
  unsigned char *csaPtr;
  struct list_head pid_map;
  struct keyindex keyindex[FF_MAX_IDX];
  int nexus_fixup;
  unsigned int avg;
  int avgcnt;
  struct parser_cmds *dvr;
  int state;
  pthread_mutex_t keylock;
  pthread_mutex_t state_lock;
  pthread_cond_t csa_cond;
};

static unsigned char **range;
static int cluster_size;
static uint cluster_size_bytes;

static LIST_HEAD(csalist);
static pthread_mutex_t list_lock = PTHREAD_MUTEX_INITIALIZER;


enum {
  NOT_ENCRYPTED = 0,
  ENCRYPTED_NOT_READY = 1,
  ENCRYPTED_READY = 2,
};

struct pid {
  struct list_head list;
  int pid;
  int index;
};
static LIST_HEAD(pidmap_empty_queue);

static inline struct csastruct *find_csa_from_adpt(int adapt)
{
  struct csastruct *csa;
  ll_find_elem(csa, csalist, adapter, adapt, struct csastruct);
  return csa;
}

void update_keys(int adpt, unsigned char keytype, int index, unsigned char *key, int pid)
{
    int i;
    struct pid *pid_ll;
    struct csastruct *csa = find_csa_from_adpt(adpt);

    if(key == NULL) {
      dprintf0("Got command(%d): %c idx: %d pid: %d\n", adpt, keytype, index,
               pid);
    } else {
      dprintf0("Got command(%d): %c idx: %d pid: %d key: "
             "%02x%02x...%02x\n", adpt, keytype, index, pid,
             key[0], key[1], key[7]);
    }
    assert(csa);
    if(! csa)
      return;
    if(index < 0 || index >= FF_MAX_IDX)
      return;

    pthread_mutex_lock(&csa->state_lock);
    if (keytype == 'I') //Invalidate
    {
        csa->state = NOT_ENCRYPTED;
        dprintf1("Setting state: NOT_ENCRYPTED\n");
        pthread_mutex_lock(&csa->keylock);
        for(i=0; i < FF_MAX_IDX; i++)
          csa->keyindex[i].valid = 0;
        //csa_ready = 0;
        pthread_mutex_unlock(&csa->keylock);
        while(! list_empty(&csa->pid_map)) {
          pid_ll = list_entry(csa->pid_map.next, struct pid);
          list_del(&pid_ll->list);
          push_empty_queue(&pid_ll->list, &pidmap_empty_queue);
        }
        pthread_mutex_unlock(&csa->state_lock);
        return;
    }
    if (keytype == 'P') //PID
    {
        ll_find_elem(pid_ll, csa->pid_map, pid, pid, struct pid);
        if(pid_ll) {
          pthread_mutex_unlock(&csa->state_lock);
          return;
        }
        pthread_mutex_lock(&csa->keylock);
        if(! csa->keyindex[index].valid) {
          csa->keyindex[index].valid = 1;
          csa->keyindex[index].status = 0;
          csa->keyindex[index].queued = 0;
        }
        pthread_mutex_unlock(&csa->keylock);
        pop_entry_from_queue_l(pid_ll, &pidmap_empty_queue, struct pid, &list_lock);
        pid_ll->pid = pid;
        pid_ll->index = index;
        list_add(&pid_ll->list, &csa->pid_map);
        pthread_mutex_unlock(&csa->state_lock);
        dprintf1("Adding pid %d to list\n", pid);
        return;
    }
    if (keytype == 'C') //Close PID
    {
        ll_find_elem(pid_ll, csa->pid_map, pid, pid, struct pid);
        if(pid_ll) {
          index = pid_ll->index;
          list_del(&pid_ll->list);
          push_empty_queue(&pid_ll->list, &pidmap_empty_queue);
          ll_find_elem(pid_ll, csa->pid_map, index, index, struct pid);
          if(pid_ll == NULL) {
            //no valid pids on this index
            pthread_mutex_lock(&csa->keylock);
            csa->keyindex[index].status = 0;
            pthread_mutex_unlock(&csa->keylock);
            if(list_empty(&csa->pid_map)) {
              //state = ENCRYPTED_NOT_READY;
              csa->state = NOT_ENCRYPTED;
              dprintf1("Setting state: NOT_ENCRYPTED\n");
            }
          }
        }
        pthread_mutex_unlock(&csa->state_lock);
        dprintf1("Removing pid %d to list state = %d\n", pid, csa->state);
        return;
    }
    ll_find_elem(pid_ll, csa->pid_map, index, index, struct pid);
    if(pid_ll == NULL) {
        pthread_mutex_unlock(&csa->state_lock);
        return;
    }
    pthread_mutex_lock(&csa->keylock);
    if (keytype == 'E') //Even
    {
        memcpy( csa->keyindex[index].even, key, 8);
        csa->keyindex[index].queued |= 0x02;
        csa->keyindex[index].status |= 0x02;
        if(csa->state == NOT_ENCRYPTED && csa->keyindex[index].status == 3) {
          csa->state = ENCRYPTED_NOT_READY;
          dprintf1("Setting state: ENCRYPTED_NOT_READY\n");
        }
        dprintf1("Processed Even Key (idx=%d): State = %d, ready = %d\n",
                 index, csa->state, csa->keyindex[index].status);
    }
    else if (keytype == 'O') //Odd
    {
        memcpy( csa->keyindex[index].odd, key, 8);
        csa->keyindex[index].queued |= 0x01;
        csa->keyindex[index].status |= 0x01;
        if(csa->state == NOT_ENCRYPTED && csa->keyindex[index].status == 3) {
          csa->state = ENCRYPTED_NOT_READY;
          dprintf1("Setting state: ENCRYPTED_NOT_READY\n");
        }
        dprintf1("Processed Odd Key (idx=%d): State = %d, ready = %d\n",
                 index, csa->state, csa->keyindex[index].status);
    }
    else if (keytype == 'N') //Nexus
    {
        csa->nexus_fixup = 1;
        if(csa->state == NOT_ENCRYPTED) {
          csa->state = ENCRYPTED_NOT_READY;
          dprintf1("Setting state: ENCRYPTED_NOT_READY\n");
        }
    }
    pthread_mutex_unlock(&csa->keylock);
    pthread_mutex_unlock(&csa->state_lock);
    return;
}

static int process_ts(struct csastruct *csa, unsigned char *buffer, uint end,
                      int force)
{
    unsigned char tmp;
    struct pid *pid_ll;
    int ret = 0, rangeptr = 0, dec = 0;
    uint pos = 0;
    uint start_enc, end_enc;
    int index = -1;
    int odd_even;
    int pkt_count = 0;
    if(csa->nexus_fixup) {
      while(pos < end) {
        buffer[pos+3] &= 0x3F;
        pos += TSPacketSIZE;
      }
      return end;
    }
    //old way
    //if(! end || ! force && end < 6000)
    //  return 0;

    while (pos < end)
    {
        tmp=buffer[pos+3] & 0xC0;
        if(tmp == 0xc0 || tmp == 0x80)
            break;
        pos += TSPacketSIZE;
    }
    //new way
    if(! force && end - pos < cluster_size_bytes)
      return  pos;
    ret = pos;
    start_enc = end_enc = pos;
    pthread_mutex_lock(&csa->state_lock);
    while(end - end_enc >= TSPacketSIZE && pkt_count < cluster_size) {
        odd_even = buffer[end_enc+3] & 0xC0;
        if(odd_even) {
            int pid = ((buffer[end_enc + 1] << 8) + buffer[end_enc + 2])
                      & 0x1FFF;
            ll_find_elem(pid_ll, csa->pid_map, pid, pid, struct pid);
            if(pid_ll == NULL) {
                //What to do with an unknown pid?
                //Let's try to decode it anyway
                dprintf3("Didn't find pid %d\n", pid);
                end_enc+=TSPacketSIZE;
                continue;
            }
            if(index == -1) {
                //First encrypted packet (with a valid pid)
                index = pid_ll->index;
                tmp=odd_even;
                pkt_count++;
	    }
            else if(index != pid_ll->index) {
                //Encrypted packet with a different index
                if(start_enc != end_enc) {
                    //there are previous packets that can be decoded
                    range[rangeptr++] = buffer + start_enc;
                    range[rangeptr++] = buffer + end_enc;
                    range[rangeptr] = 0;
                }
                //skip this packet
                start_enc = end_enc + TSPacketSIZE;
            } else {
              pkt_count++;
            }
        }
        end_enc+=TSPacketSIZE;
    }
    pthread_mutex_unlock(&csa->state_lock);
    if (index != -1 && start_enc != end_enc) {
        //add the last set of packets to the encryption queue
        range[rangeptr++] = buffer + start_enc;
        range[rangeptr++] = buffer + end_enc;
        range[rangeptr] = 0;
    }
    if (rangeptr > 0) {
        pthread_mutex_lock(&csa->keylock);
        if (! csa->keyindex[index].valid || csa->keyindex[index].status != 3) {
           pthread_mutex_unlock(&csa->keylock);
           return ret;
        }
        if (csa->keyindex[index].queued == 3)
        {
            csa->keyindex[index].queued = 0;
            set_even_control_word(csa->keyindex[index].keys, csa->keyindex[index].even);
            set_odd_control_word(csa->keyindex[index].keys, csa->keyindex[index].odd);
        }

        if (csa->keyindex[index].queued & 0x02 && tmp != 0x80)
        {
            csa->keyindex[index].queued &= 0x01;
            set_even_control_word(csa->keyindex[index].keys, csa->keyindex[index].even);
        }
        if (csa->keyindex[index].queued & 0x01 && tmp != 0xC0)
        {
            csa->keyindex[index].queued &= 0x02;
            set_odd_control_word(csa->keyindex[index].keys, csa->keyindex[index].odd);
        }
        pthread_mutex_unlock(&csa->keylock);

        if((_dbglvl >> PLUGIN_ID) & 3) {
          csa->avg = (csa->avg * 99 + pkt_count*100) / 100;
          if(csa->avg == 0)
            csa->avg = 1;
          csa->avgcnt++;
          if(csa->avgcnt == 100) {
            csa->avgcnt = 0;
            dprintf0("decrypted packets max:%d now:%u of %u avg:%f\n",
                cluster_size, pkt_count, end / TSPacketSIZE, csa->avg / 100.0);
          }
        }
        dec = decrypt_packets(csa->keyindex[index].keys, range) * TSPacketSIZE;
        if(ret + buffer == range[0])
          ret+=dec;
        //printf("decrypted now=%d, decrypted=%d, total=%d\n", pkt, pkt_done, pkt_cnt);
    }
    return ret;
}

static struct csastruct *find_csa_from_rb(struct ringbuffer *rb, int init) {
  struct csastruct *entry;
  int notalign;

  if(rb->state != RB_OPEN)
    return NULL;
  pthread_mutex_lock(&list_lock);
  ll_find_elem(entry, csalist, rb, rb, struct csastruct);
  if (! init) {
    pthread_mutex_unlock(&list_lock);
    return entry;
  }
  if(! entry) {
    dprintf0("Creating csa for rb: %d\n", rb->num);
    entry = find_csa_from_adpt(rb->num);
    assert(entry);
  } else {
    dprintf0("Resetting csa for rb: %d\n", rb->num);
  }
  pthread_mutex_lock(&rb->rw_lock);
  //things may have changed since above
  if(rb->state != RB_OPEN) {
    pthread_mutex_unlock(&rb->rw_lock);
    pthread_mutex_unlock(&list_lock);
    return NULL;
  }
  entry->rb = rb;
  entry->csaPtr = rb->rdPtr;
  entry->avg = cluster_size * 100;
  entry->avgcnt = 0;
  notalign = (entry->csaPtr - rb->buffer) % TSPacketSIZE;
  if(notalign) {
    //The read pointer is not aligned, but the wrptr is required to be
    entry->csaPtr += TSPacketSIZE - notalign;
    if(entry->csaPtr > rb->end)
      entry->csaPtr = rb->buffer + (entry->csaPtr - rb->end);
    if((rb->rdPtr > rb->wrPtr && entry->csaPtr > rb->wrPtr && entry->csaPtr < rb->rdPtr) || (rb->rdPtr <= rb->wrPtr && entry->csaPtr > rb->wrPtr)) { 
      dprintf0("INIT buf: %p rd: %p csa: %p wr: %p end: %p\n",
          rb->buffer, rb->rdPtr, entry->csaPtr, rb->wrPtr, rb->end);
      assert(0);
    }
  }
  dprintf1("INIT buf: %p rd: %p csa: %p wr: %p end: %p\n",
         rb->buffer, rb->rdPtr, entry->csaPtr, rb->wrPtr, rb->end);
  pthread_mutex_unlock(&rb->rw_lock);
  pthread_mutex_unlock(&list_lock);
  return entry;
}

/*Check and/or set state */
/*MUST lock state_lock before calling!*/
int check_state(struct csastruct *csa, struct ringbuffer *rb) {
  if(csa->state == NOT_ENCRYPTED)
    return NOT_ENCRYPTED;
  if(csa->state == ENCRYPTED_READY)
    return ENCRYPTED_READY;
  if(csa->state == ENCRYPTED_NOT_READY) {
    if(list_empty(&csa->pid_map))
      return NOT_ENCRYPTED;
    find_csa_from_rb(rb, 1);
    csa->state = ENCRYPTED_READY;
    dprintf1("Setting state: ENCRYPTED_READY\n");
    return ENCRYPTED_READY;
  }
  return NOT_ENCRYPTED;
}
 
/*If need_bytes >0 we must block until some bytes can be read */
int check_encrypted(struct ringbuffer *rb, int need_bytes)
{
  int processed_bytes = 0;
  int avail_bytes;
  int state;
  struct csastruct *csa = find_csa_from_adpt(rb->num);

  assert(csa);
  if(csa) {
    pthread_mutex_lock(&csa->state_lock);
    state = check_state(csa, rb);
  } else {
    state = NOT_ENCRYPTED;
  }

  if(state == NOT_ENCRYPTED) {
#if 0
    unsigned char *ptr = rb->buffer;
    while(ptr < rb->end) {
      ptr[3] &= 0x3f;
      ptr += TSPacketSIZE;
    }
#endif
    if(rb->rdPtr > rb->wrPtr)
      avail_bytes = rb->end - rb->rdPtr + rb->wrPtr - rb->buffer - TSPacketSIZE;
    else
      avail_bytes = rb->wrPtr - rb->rdPtr - TSPacketSIZE;
    if(csa) {
      pthread_mutex_unlock(&csa->state_lock);
    }
    //printf("Unencrypted.  Returning %d bytes\n", avail_bytes);
    return avail_bytes;
  }
  pthread_mutex_lock(&rb->rw_lock); // protect csaPtr
  pthread_mutex_unlock(&csa->state_lock);

  //There should be no way for csa not to be set here
  assert(csa->rb);

  if((rb->rdPtr <= csa->csaPtr  && csa->csaPtr <= rb->wrPtr) ||
     (rb->wrPtr < rb->rdPtr && rb->rdPtr <= csa->csaPtr)) {
    processed_bytes = csa->csaPtr -  rb->rdPtr;
  } else if(csa->csaPtr <= rb->wrPtr && rb->wrPtr < rb->rdPtr) {
    processed_bytes = rb->end - rb->rdPtr + csa->csaPtr - rb->buffer ;
  } else {
    //rd == wr && csa != rd
    //     OR
    //wr < csa < rd
    //We used to get here when switching between encrypted and clear
    //But no more!
    //csa->csaPtr = rb->rdPtr;
    //processed_bytes = 0;
    dprintf0("buf: %p rd: %p csa: %p wr: %p end: %p\n",
           rb->buffer, rb->rdPtr, csa->csaPtr, rb->wrPtr, rb->end);
    assert(0);
  }
  if(need_bytes > 0 && processed_bytes < need_bytes) {
    //ring-read is waiting for decrypted data.  we will stall until we get some
    //NOTE: We don't care about the return in this case as we need to call again
    //      to compute processed_bytes
    dprintf0("buf: %p rd: %p csa: %p wr: %p end: %p\n",
           rb->buffer, rb->rdPtr, csa->csaPtr, rb->wrPtr, rb->end);
    msg_replace(MSG_HIGH_PRIORITY, MSG_RINGBUF, rb->num, rb);
    pthread_cond_wait(&csa->csa_cond, &rb->rw_lock);
  }
  //printf("buf: %p rd: %p csa: %p wr: %p end: %p processed: %d\n",
  //       rb->buffer, rb->rdPtr, csa->csaPtr, rb->wrPtr, rb->end,
  //       processed_bytes);
  pthread_mutex_unlock(&rb->rw_lock);
  return processed_bytes;
}

static void process_ffd(struct msg *msg, unsigned int priority)
{
  struct csastruct *csa;
  int ret;
  int end = 0;
  unsigned char *wrPtr;
  if(msg->type == MSG_RINGBUF) {
    struct ringbuffer *rb = (struct ringbuffer *)msg->data;
    int bytes;
    msg->type = MSG_PROCESSED;
#ifdef NO_RINGBUF
    return;
#endif
    csa = find_csa_from_adpt(rb->num);
    assert(csa);
    if(! csa)
      return;

    pthread_mutex_lock(&csa->state_lock);
    if(check_state(csa, rb) == NOT_ENCRYPTED) {
      pthread_mutex_unlock(&csa->state_lock);
      return;
    }

    assert(csa->rb);

    pthread_mutex_lock(&rb->rw_lock);
    pthread_mutex_unlock(&csa->state_lock);

    wrPtr = rb->wrPtr;

    if (csa->csaPtr > wrPtr) {
      bytes = rb->end - csa->csaPtr;
      end = 1;
    } else
      bytes = wrPtr - csa->csaPtr;
    pthread_mutex_unlock(&rb->rw_lock);
    ret = process_ts(csa, csa->csaPtr, bytes, end);
    pthread_mutex_lock(&rb->rw_lock);
    if (csa->csaPtr + ret > rb->end) {
      dprintf0("rdPtr: %lu csaPtr: %lu wrPtr: %lu end: %lu bytes: %d end: %d\n",
             (unsigned long) (rb->rdPtr - rb->buffer),
             (unsigned long)(csa->csaPtr - rb->buffer),
             (unsigned long)(rb->wrPtr - rb->buffer),
             (unsigned long)(rb->end - rb->buffer), bytes, ret);
      assert(csa->csaPtr + ret <= rb->end);
    }
    csa->csaPtr += ret;
    if(csa->csaPtr == rb->end)
      csa->csaPtr = rb->buffer;

    if(ret) {
      if (rb->flags & O_NONBLOCK) {
        struct dvblb_pollmsg msg;
        msg.count = 0;
        //Is this thread safe?
        pthread_mutex_lock(&csa->dvr->poll_mutex);
        ioctl(csa->dvr->virtfd, DVBLB_CMD_ASYNC, &msg);
        pthread_mutex_unlock(&csa->dvr->poll_mutex);
      } else
        pthread_cond_signal(&csa->csa_cond);
    }
    pthread_mutex_unlock(&rb->rw_lock);
//    dprintf0("decoded: %d - %d\n", ret, bytes);
    if(ret) {
//      dprintf0("Sending RB1: %p\n", rb);
      msg_replace(MSG_HIGH_PRIORITY, MSG_RINGBUF, rb->num, rb);
    }
    sched_yield();
  }
  else if(msg->type == MSG_RINGCLOSE) {
    msg->type = MSG_PROCESSED;
    struct ringbuffer *rb = (struct ringbuffer *)msg->data;
    csa = find_csa_from_adpt(rb->num);
    msg_remove_type_from_list(MSG_HIGH_PRIORITY, MSG_RINGBUF, rb->num, NULL);
    msg_remove_type_from_list(MSG_HIGH_PRIORITY, MSG_RINGCLOSE, rb->num, NULL);
    assert(csa);
    if(! csa)
      return;
    dprintf0("Removing csa for rb: %d\n", rb->num);
    pthread_mutex_lock(&csa->state_lock);
    rb->release(rb);
    pthread_mutex_lock(&list_lock);
    csa->rb = NULL;
    pthread_mutex_unlock(&list_lock);
    if(csa->state == ENCRYPTED_READY) {
      csa->state = ENCRYPTED_NOT_READY;
      dprintf1("Setting state: ENCRYPTED_NOT_READY\n");
    }
    pthread_mutex_unlock(&csa->state_lock);
  }
}

//static void launch_ffd() {
//  pthread_create(&thread, NULL, ffd_loop, NULL);
//}

#ifdef NO_RINGBUF
static void preread_call(struct parser_cmds *pc, struct poll_ll *fdptr,
                      cmdret_t *result, int *ret, 
                      unsigned long int cmd, unsigned char *data)
{
  struct dvblb_custommsg *ci = (struct dvblb_custommsg *)data;
  ci->u.count -= ci->u.count % TSPacketSIZE;
}
  
static void read_call(struct parser_cmds *pc, struct poll_ll *fdptr,
                      cmdret_t *result, int *ret, 
                      unsigned long int cmd, unsigned char *data)
{
//  struct dvblb_custommsg *ci = (struct dvblb_custommsg *)data;
  int bytes_left = *ret, tmp;
  do {
//    dprintf0("Decrypting %d bytes (%d done so far)\n", bytes_left, (*ret - bytes_left));
    tmp = process_ts(pc->mmap + (*ret - bytes_left), bytes_left);
    bytes_left -= tmp;
  } while(bytes_left);
  *result = CMD_SKIPCALL;
}

#endif

static void connect_ffd(struct parser_adpt *pc_all)
{
  struct list_head *ptr;
  struct csastruct *csa;
#ifdef NO_RINGBUF
  struct cmd_list *dvr_preread = register_cmd(preread_call);
  struct cmd_list *dvr_postread = register_cmd(read_call);
  list_add(&dvr_preread.list, &pc_all->dvr->pre_read);
  list_add(&dvr_postread.list, &pc_all->dvr->post_read);
#else
  
  range = (unsigned char **)malloc(pc_all->dvr->common->buffersize /
                                   TSPacketSIZE + 1);
  csa = (struct csastruct *)malloc(sizeof(struct csastruct));
  memset(csa, 0, sizeof(struct csastruct));
  pthread_mutex_init(&csa->keylock, NULL);
  pthread_mutex_init(&csa->state_lock, NULL);
  pthread_cond_init(&csa->csa_cond, NULL);
  INIT_LIST_HEAD(&csa->pid_map);

  csa->adapter = pc_all->dvr->common->virt_adapt;
  csa->dvr = pc_all->dvr;
  csa->state = NOT_ENCRYPTED;
  dprintf1("Setting state: NOT_ENCRYPTED\n");
  for(int i = 0; i < FF_MAX_IDX; i++) {
    csa->keyindex[i].valid = 0;
    csa->keyindex[i].keys =  get_key_struct();
  }
  list_add_tail(&csa->list, &csalist);
  cluster_size = get_suggested_cluster_size();
  cluster_size_bytes = (uint)cluster_size * TSPacketSIZE;
  ringbuf_register_callback(check_encrypted);
  list_for_each(ptr, &plugin_cmdlist) {
    struct plugin_cmd *cmd = list_entry(ptr, struct plugin_cmd);
    if(cmd->plugin == PLUGIN_RINGBUF) {
      cmd->send_msg(pc_all->dvr, 1);
      break;
    }
  }
#endif
}


//list, plugin_id, name, parse_args, connect, launch, message, send_msg
static struct plugin_cmd plugin_cmds = {{NULL, NULL}, PLUGIN_ID, "ffdecsa", 
                 NULL, connect_ffd, NULL, process_ffd, NULL, NULL};
int __attribute__((constructor)) __ffdecsa_init(void)
{
  list_add_tail(&plugin_cmds.list, &plugin_cmdlist);
  return 0;
}

