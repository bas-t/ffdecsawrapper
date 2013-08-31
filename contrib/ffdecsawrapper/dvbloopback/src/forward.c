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
#include <dirent.h>

#include <signal.h>
#include <errno.h>

#include <sys/time.h>
#include <sys/resource.h>
#include <sys/ioctl.h>

#include <sched.h>
#include <linux/dvb/frontend.h>
#include <linux/dvb/dmx.h>

#include <syslog.h>
#include <stdarg.h>
#include <time.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "process_req.h"
#include "plugin_getsid.h"
#include "plugin_ringbuf.h"
#include "msg_passing.h"

struct t_adaptermap {
  char name[128];
  int adapter;
  int used;
};

LIST_HEAD(plugin_cmdlist);
unsigned int _dbglvl;
void * listen_loop(void * arg);
extern const char *source_version;
pthread_attr_t default_attr;
static pthread_t main_thread;
static pthread_mutex_t tmprintf_mutex = PTHREAD_MUTEX_INITIALIZER;

static char logfile[512] = "";
static char pidfile[512] = "";
enum {
  LOGMODE_STDOUT = 0x01,
  LOGMODE_SYSLOG = 0x02,
  LOGMODE_LOGFILE = 0x04,
};
static struct {
  unsigned int logmode;
} logcfg = {LOGMODE_STDOUT};

int tmprintf(const char *plugin, const char *fmt, ...)
{
  va_list args;
  struct timeval tv;
  struct tm tm;
  unsigned long usec;
  char stamp[32], logmsg[1024];
  gettimeofday(&tv, NULL);
  localtime_r(&tv.tv_sec, &tm);
  strftime(stamp,sizeof(stamp),"%b %e %T",localtime_r(&tv.tv_sec, &tm));
  usec = tv.tv_usec;
  pthread_mutex_lock(&tmprintf_mutex);
  /*
   ** Display the remainder of the message
   */
  va_start(args,fmt);
  vsnprintf(logmsg,sizeof(logmsg),fmt,args);
  va_end(args); 
  if(logcfg.logmode & LOGMODE_STDOUT)
    fprintf(stdout, "%s.%03lu %s: %s", stamp, usec/1000, plugin, logmsg);
  if(logcfg.logmode & LOGMODE_SYSLOG)
    syslog(LOG_INFO, "%s: %s", plugin, logmsg);
  pthread_mutex_unlock(&tmprintf_mutex);
   return 0;
}

int get_adapters(struct t_adaptermap **adaptermap)
{
  int count = 0;
  struct dirent **adptrlist;
  int i, n, fd, adapter;
  char frontend[30];
  struct dvb_frontend_info fe;

  n = scandir("/dev/dvb/", &adptrlist, NULL, alphasort);
  if(n < 0) {
    perror("scandir");
    return 0;
  }
  *adaptermap = (struct t_adaptermap *)malloc(n * sizeof(struct t_adaptermap));
  for(i = 0; i < n; i++) {
    if(1 == sscanf(adptrlist[i]->d_name, "adapter%d", &adapter)) {
      snprintf(frontend, 30, "/dev/dvb/adapter%d/frontend0", adapter);
      fd = open(frontend, O_RDONLY);
      if(fd >= 0) {
        if(ioctl(fd, FE_GET_INFO, &fe) >=0) {
          (*adaptermap)[count].adapter = adapter;
          snprintf((*adaptermap)[count].name, 128, "%s", fe.name);
          (*adaptermap)[count].used = 0;
          count++;
        }
        close(fd);
      }
    }
    free(adptrlist[i]);
  }
  free(adptrlist);
  if(count == 0)
    free(*adaptermap);
  return count;
}

void show_help()
{
  struct list_head *ptr;
  int i;
  printf("sasc-ng [options] -j <real>:<virtual> ...\n"); 
  printf("Version: %s-%s\n\n", RELEASE_VERSION, source_version);
  printf("   -j//--join <real>:<virt>\n");
  printf("                     : Connect real and loopback dvb adapters\n");
  printf("                       This option can be specified multiple times\n");
  printf("                       to support multiple loopback adapters.\n");
  printf("                       NOTE: <real> can be either the adapter number\n");
  printf("                       or the adapter name (see --identify)\n");
  printf("Optional args:\n");
  printf("   -h/--help         : This help message\n");
  printf("   -i/--identify     : List all available adpaters\n");
  printf("   -b/--buffer <num> : Set size of read buffer (default: 2M)\n");
  printf("   -d/--debug <num>  : Set debug level (this is a 32bit bitmask)\n");
  printf("   -l/--log          : Set log file for output\n");
  printf("   -n/--noload <num> : Don't load module <num>. Use with care!\n");
  printf("   -o/--osd          : Enable passthrough for cards with mpeg2 decoders\n");
  printf("   -p/--port <num>   : Set debug port to listen on (default 5456)\n");
  printf("   -D/--daemon       : Enable daemon/background mode (logs to syslog unless -l is set)\n");
  printf("   -P/--pidfile      : write pid to file\n");
  list_for_each(ptr, &plugin_cmdlist) {
    struct plugin_cmd *cmd = list_entry(ptr, struct plugin_cmd);
    if(cmd->parse_args) {
      printf("\nOptions for %s module:\n", cmd->name);
      cmd->parse_args(ARG_HELP);
    }
  }
  printf("\nDebug bitmask:\n");
  printf("   0x%08x        : %s\n", 3, dnames[3]);
  printf("   0x%08x        : %s\n", 3<<2, dnames[4]);
  printf("   0x%08x        : %s\n", 3<<4, dnames[5]);
  printf("   0x%08x        : %s\n", 3<<6, dnames[6]);
  for(i = 8; i < 32; i+=2) {
    list_for_each(ptr, &plugin_cmdlist) {
      struct plugin_cmd *cmd = list_entry(ptr, struct plugin_cmd);
      if(cmd->plugin == i) {
        printf("   0x%08x        : %s\n", 3<<i, cmd->name);
        break;
      }
    }
  }
}

int init_osd(int real, int virt) {
  FILE *FH;
  char str[256];
  int tmp;
  int link[] = {0, 1, 8}; //video, audio, osd
  //Link real adapter to virtual adapter
  sprintf(str, "/proc/dvbloopback/adapter%d/adapter", virt);
  FH = fopen(str, "w");
  if(! FH)
    return 0;
  fprintf(FH, "%d", real);
  fclose(FH);
  FH = fopen(str, "r");
  fscanf(FH,"%d", &tmp);
  fclose(FH);
  if(tmp != real) {
    dprintf("Could not setup adapter link: %d != %d\n", real, tmp);
    return 0;
  }
  for(int i=0; i < 3; i++) {
    sprintf(str, "/proc/dvbloopback/adapter%d/%s", virt, dnames[link[i]]);
    FH = fopen(str, "w");
    if(! FH)
      return 0;
    fprintf(FH, "1");
    fclose(FH);
    FH = fopen(str, "r");
    fscanf(FH,"%d", &tmp);
    fclose(FH);
    if(tmp != 1) {
      dprintf("Could not setup device %s: %d != 1\n", dnames[link[i]], tmp);
      return 0;
    }
  }
  return 1;
}

int log_rotate(int report_error)
{
  /* http://www.gossamer-threads.com/lists/mythtv/dev/110113 */

  int new_logfd = open(logfile, O_WRONLY|O_CREAT|O_APPEND|O_SYNC|O_LARGEFILE, 0664);
  if (new_logfd < 0) {
    // If we can't open the new logfile, send data to /dev/null
    if (report_error) {
      fprintf(stderr, "cannot open logfile %s\n",logfile);
      return 0;
    }
    new_logfd = open("/dev/null", O_WRONLY);
    if (new_logfd < 0) {
      // There's not much we can do, so punt.
      return 0;
    }
  }
  while (dup2(new_logfd, 1) < 0 && errno == EINTR)
    ;
  while (dup2(new_logfd, 2) < 0 && errno == EINTR)
    ;
  while (close(new_logfd) < 0 && errno == EINTR)
    ;
  return 1;
}

void log_rotate_handler(int)
{
  log_rotate(0);
}

static void exit_handler(int type)
{
  if(type == SIGTERM && ! pthread_equal(pthread_self(), main_thread)) {
    pthread_exit(NULL);
    return;
  }
  //take care of main message handler.  Main thread will do the rest
  msg_terminate(MSG_LOW_PRIORITY);
}

int main(int argc, char *argv[])
{
  unsigned long bufsize = 2000000;
  struct parser_adpt pc_all[8];
  struct common_data common[8];
  struct list_head *ptr;
  pthread_t msg_highpri_thread;
  pthread_t socket_thread;
  int virt_adapt[8], real_adapt[8], adapter_cnt=0;
  unsigned long debug_port = 5456; 
  int longopt = 0;
  int c, Option_Index = 0;
  int illegal_opt = 0;
  int use_osd = 0;
  struct t_adaptermap *adptrmap;
  int adptrmap_count;
  struct option *LongOpts;
  int optcount;
  int use_daemon = 0;
  static struct option Long_Options[] = {
    {"buffer", 1, 0, 'b'},
    {"debug", 1, 0, 'd'},
    {"help", 0, 0, 'h'},
    {"identify", 0, 0, 'i'},
    {"join", 1, 0, 'j'},
    {"noload", 1, 0, 'n'},
    {"osd", 0, 0, 'o'},
    {"port", 0, 0, 'p'},
    {"daemon", 0, 0, 'D'},
    {"log", 1, 0, 'l'},
    {"pidfile", 1, 0, 'P'},
  };

  pthread_attr_init(&default_attr);
  pthread_attr_setstacksize(&default_attr, PTHREAD_STACK_MIN + 0x4000);
  main_thread = pthread_self();

  _dbglvl = 0;
  //enable unbuffered stdout 
  setbuf(stdout, 0); 

  //enable a core dump file
  struct rlimit rl = {RLIM_INFINITY, RLIM_INFINITY};
  setrlimit(RLIMIT_CORE, &rl);

  adptrmap_count = get_adapters(&adptrmap);
  if(adptrmap_count == 0) {
    dprintf("Didn't find any DVB adapters!\n");
    return(-1);
  }

  //get all plugin args
  optcount = sizeof(Long_Options) / sizeof(struct option);
  LongOpts = (struct option *)malloc(sizeof(struct option) * (optcount+1));
  memcpy(LongOpts, Long_Options, sizeof(Long_Options));
  
  list_for_each(ptr, &plugin_cmdlist) {
    struct plugin_cmd *cmd = list_entry(ptr, struct plugin_cmd);
    if(cmd->parse_args) {
      struct option *optstr;
      optstr = cmd->parse_args(ARG_INIT);
      if(! optstr)
        continue;
      while(optstr->name) {
        LongOpts = (struct option *)realloc(LongOpts,
                                         sizeof(struct option) * (optcount+2));
        memcpy(LongOpts+optcount, optstr, sizeof(struct option));
        optstr++;
        optcount++;
      }
    }
  }
  memset(LongOpts+optcount, 0, sizeof(struct option));

  while (1) {

    c = getopt_long(argc, argv, "b:d:hij:l:n:op:DP:", LongOpts, &Option_Index); 
    if (c == EOF)
      break;
    switch (c) {
      case 'b':
        {
          char type;
          int l = sscanf(optarg, "%lu%c", &bufsize,&type);
          if(l == 2) {
            if (type == 'M') {
              bufsize *= 1000000;
            } else if (type == 'k') {
              bufsize *= 1024;
            }
            break;
          }
        }
      case 'd':
        _dbglvl = strtol(optarg, NULL, 0);
        break;
      case 'D':
        use_daemon = 1;
        break;
      case 'h':
        show_help();
        return(0);
        break;
      case 'i':
        {
          int i;
          for(i = 0; i < adptrmap_count; i++)
            printf("%d: %s\n", adptrmap[i].adapter, adptrmap[i].name);
          return(0);
        }
      case 'j':
        {
          int r, v, len, i;
          char *v_chr, *r_end;
          if((v_chr = strrchr(optarg, ':')) == NULL) {
            illegal_opt++;
            break;
          }
          v = strtol(v_chr+1, NULL, 0);
          r = strtol(optarg, &r_end, 0);
          len = v_chr - optarg;
          if (len > 128)
            len = 128;
          for(i = 0; i < adptrmap_count; i++) {
            if((r_end != v_chr && strncmp(optarg, adptrmap[i].name, len) == 0) || (r_end == v_chr && adptrmap[i].adapter == r)) { 
              if(adptrmap[i].used) {
                dprintf("Adapter %d is already in use\n", r);
                return(-1);
              }
              adptrmap[i].used = 1;
              real_adapt[adapter_cnt] = adptrmap[i].adapter;
              virt_adapt[adapter_cnt] = v;
              adapter_cnt++;
              break;
            }
          }
          if(i == adptrmap_count) {
            dprintf("Could not find adapter for %s\n", optarg);
            return(-1);
          }
          break;
        }
      case 'l':
        strncpy(logfile, optarg, sizeof(logfile)-1);
        logfile[sizeof(logfile)-1] = 0;
        break;
      case 'n':
        {
          int val = atoi(optarg);
          list_for_each(ptr, &plugin_cmdlist) {
            struct plugin_cmd *cmd = list_entry(ptr, struct plugin_cmd);
            if(cmd->plugin == val) {
              list_del(&cmd->list);
              break;
            }
          }
          break;
        }
      case 'o':
        use_osd = 1;
        break;
      case 'p': 
        debug_port = atoi(optarg); 
        break; 
      case 'P':
        strncpy(pidfile, optarg, sizeof(pidfile)-1);
        pidfile[sizeof(pidfile)-1] = 0;
        break;
      case 0:
        if(! longopt) {
          list_for_each(ptr, &plugin_cmdlist) {
            struct plugin_cmd *cmd = list_entry(ptr, struct plugin_cmd);
            if(cmd->parse_args) {
              cmd->parse_args(ARG_PARSE);
            }
          }
          break;
        }
        break;
      case '?':
        illegal_opt++;
        break;
    }
  }
  free(LongOpts);
  free(adptrmap);

  if (illegal_opt) {
    dprintf("Illegal options specified.  Aborting!\n");
    return(-1);
  }
  if (adapter_cnt == 0) {
    show_help();
    return(-1);
  }

  if(bufsize < 1000000 || bufsize > 20000000) {
    dprintf("Buffer size is out of range.  1000000 <= size <= 20000000\n");
    return(-1);
  }

  if (use_daemon) {
    if(logfile[0] == 0) {
      logcfg.logmode = LOGMODE_SYSLOG;
      openlog(argv[0],LOG_PID,LOG_LOCAL0);
    }
    if (daemon(0,1) < -1) {
      dprintf("Couldnt go into background.  Aborting!\n");
      return(-1);
    }
  }

  if (pidfile[0] != 0 ) {
    FILE *f = fopen(pidfile,"wt");
    if (f) {
      fprintf(f,"%d\n",getpid());
      fclose(f);
    }
  }

  if (logfile[0] != 0 ) {
    if (! log_rotate(1))
      fprintf(stderr, "cannot open logfile; using stdout/stderr\n");
    else
      signal(SIGHUP, &log_rotate_handler);
  }

  dprintf("Version: %s-%s\n", RELEASE_VERSION, source_version);

  //Need to init msg_loop early in case some plugins setup messages during init
  msg_loop_init();

  //setup exit_handler afer msg_loop_init!
  signal(SIGTERM, &exit_handler); 
  signal(SIGQUIT, &exit_handler); 
  signal(SIGINT, &exit_handler);

  for(int i = 0; i < adapter_cnt; i++) {
    if (use_osd) {
      if(init_osd(real_adapt[i], virt_adapt[i])) {
        dprintf("Initiailized osd on adapter pair %d:%d\n",
                 real_adapt[i], virt_adapt[i]);
      }
    }
    common[i].virt_adapt = virt_adapt[i];
    common[i].real_adapt = real_adapt[i];
    common[i].private_data = NULL;
    common[i].buffersize = bufsize;
    pthread_mutex_init(&common[i].cond_lock, NULL);
    pthread_cond_init(&common[i].cond, NULL);
    common[i].cond_count = 0;
    init_parser(&pc_all[i], &common[i]);
    if(! pc_all[i].frontend || ! pc_all[i].demux || ! pc_all[i].dvr) {
      dprintf("Could not connect to loopback device %d\n", virt_adapt[i]);
      dprintf("Are you sure you have loaded the dvbloopback module\n");
      dprintf("properly and/or used the correct values to the '-j' switch\n");
      return(-1);
    }
    list_for_each(ptr, &plugin_cmdlist) {
      struct plugin_cmd *cmd = list_entry(ptr, struct plugin_cmd);
      if(cmd->connect)
        cmd->connect(&pc_all[i]);
    }

  }

  for(int i = 0; i < adapter_cnt; i++) {
    launch_processors(&pc_all[i]);
    pthread_mutex_lock(&common[i].cond_lock);
    while (common[i].cond_count > 0) {
      pthread_cond_wait(&common[i].cond, &common[i].cond_lock);
    }
    pthread_mutex_unlock(&common[i].cond_lock);
  }

  list_for_each(ptr, &plugin_cmdlist) {
    struct plugin_cmd *cmd = list_entry(ptr, struct plugin_cmd);
    if(cmd->launch)
      cmd->launch();
  }
  pthread_create(&socket_thread, &default_attr, listen_loop,
                 (void *)debug_port); 
  pthread_create(&msg_highpri_thread, &default_attr, msg_loop,
                 (void *) MSG_HIGH_PRIORITY);

  msg_loop((void *)MSG_LOW_PRIORITY);
  //Shutting down
  msg_terminate(MSG_HIGH_PRIORITY);
  pthread_join(msg_highpri_thread, NULL);
  pthread_kill(socket_thread, SIGTERM);
  pthread_join(socket_thread, NULL);
  //Now kill the device handlers
  for(int i = 0; i < adapter_cnt; i++)
    shutdown_parser(&pc_all[i]);
  //Lastly kill the plugins
  list_for_each(ptr, &plugin_cmdlist) { 
    struct plugin_cmd *cmd = list_entry(ptr, struct plugin_cmd); 
    if(cmd->shutdown) 
      cmd->shutdown(); 
  }
  dprintf("Exiting...\n");
  fflush(stdout);
  fflush(stderr);
  return 0;
}

void * listen_loop(void * arg)
{
    unsigned long port = (unsigned long)arg;
    struct list_head *ptr;
    int sockfd, opt = 0;
    unsigned int len;
    char buf[256];
    struct sockaddr_in serv_addr, cli_addr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0) {
      perror("Socket creation failed");
      return NULL;
    }
 
    dprintf("Listening on port %d\n", port); 
 
    len = sizeof(opt);
    getsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (char *)&opt,
               (socklen_t *)&len);
    opt |= 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);

    if(bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
      perror("Failed to bind to debug port:");
      tmprintf("DEBUG","The debug port will not be available");
      return NULL;
    }
    if(listen(sockfd, 1) < 0) {
      perror("Listen failed:");
      return NULL;
    }
    while(1) {
      int connfd;
      int found;
      len =  sizeof(cli_addr);
      connfd = accept(sockfd, (struct sockaddr *)&cli_addr, (socklen_t *)&len);
      if(connfd < 0) {
        perror("Connection failed:");
        continue;
      }
      len = read(connfd, buf, 256);
      found = 0;
      list_for_each(ptr, &plugin_cmdlist) {
        struct plugin_cmd *cmd = list_entry(ptr, struct plugin_cmd);
        if(cmd->user_msg) {
          if(strncasecmp(buf, cmd->name, strlen(cmd->name)) == 0 && buf[strlen(cmd->name)] == ' ') {
            char *p;
            printf("xx %s yy\n", buf);
            if((p = strchr(buf, '\r')) || (p = strchr(buf, '\n'))) { *p = '\0'; }
            printf("XX %s YY\n", buf);
            cmd->user_msg(&buf[strlen(cmd->name)+1]);
            found = 1;
            break;
          }
        }
      }
      if(! found) {
        if(len < sizeof(int)) {
          tmprintf("DEBUG","Got bad command\n");
        } else {
          /* This isn't thread safe, but that's ok*/
          _dbglvl = *(unsigned int *)&buf;
          tmprintf("DEBUG","Got debug value: %u\n", _dbglvl);
        }
      }
      close(connfd);
    }
}
