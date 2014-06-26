#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>

#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <linux/dvb/ca.h>

void _SetCaPid(ca_pid_t * ca_pid);
void _SetCaDescr(ca_descr_t * ca_descr);
void _ForceIndex(int index);

bool is_file(char *path) {
  struct stat st;
  if (stat(path, &st) != 0) {
    return false;
  }
  return true;
}

/* this function finds all pids that are attached to the unix socket 'origstr'.
   It does this by sniffing through /proc, so it is definitely non-portable */
int find_pid_from_socket(char *origstr) {
  int found = 0, pid = 0;
  char path[256], buf[512];
  char path1[256], buf1[512];
  DIR *DH, *DH1;
  struct dirent *dirent, *dirent1;
  DH = opendir("/proc");
  while ((dirent = readdir(DH))) {
     if ((pid = strtol(dirent->d_name, NULL, 10)) == 0) {
        continue;
     }
     snprintf(path, 256, "/proc/%s/root", dirent->d_name);
     memset(buf, 0, sizeof(buf));
     if (readlink(path, buf, 512) == -1)
        continue;
     if (strstr(origstr, buf) != origstr)
        continue;
     snprintf(path, 256, "/proc/%s/fd", dirent->d_name);
     DH1 = opendir(path);
     if(! DH1)
        continue;
     if(buf[strlen(buf)-1] == '/')
       buf[strlen(buf)-1] = 0;
     while ((dirent1 = readdir(DH1))) {
        int sock;
        memset(buf1, 0, sizeof(buf1));
        snprintf(path1, 256, "%s/%s", path, dirent1->d_name);
        if (readlink(path1, buf1, 512) == -1)
          continue;
        if (strstr(buf1, "socket") != buf1)
          continue;
        sock = atoi(buf1+8);
        if (sock == 0)
           continue;
        {
          FILE *FH;
          char buf2[256];
          char *tok;
          FH=fopen("/proc/net/unix", "r");
          while (fgets(buf2, 256, FH) ) {
            int i;
            char delim[] =" \t";
            char *ptr = buf2;
            int trysock;
            for(i = 0; i < 7; i++) {
              tok = strtok(ptr, delim);
              if(! tok)
                 break;
              ptr = NULL;
            }
            if (! tok)
              continue;
            trysock = atoi(tok);
            if (trysock == 0 || sock != trysock)
              continue;
             tok = strtok(NULL, delim);
             if (! tok)
               continue;
             char newstr[256];
             snprintf(newstr, 256, "%s%s", buf, tok);
             if (strncmp(origstr, newstr, strlen(origstr)) == 0) {
                found = 1;
                break;
             }
           }
           fclose(FH);
           break;
         }
     }
     closedir(DH1);
     if (found)
       break;
   }
   closedir(DH);
   if (found)
     return pid;
   return 0;
}

bool shutdown_evocamd(int cardnum) {
  char path[256], line[256];
  int pid = 0;
  snprintf(path, 256, "/tmp/sasc-chroot-%d/tmp/camd.socket", cardnum);
  if (is_file(path)) {
    while((pid = find_pid_from_socket(path))) {
      printf("Killing evocamd at pid %d\n", pid);
      kill(pid, SIGTERM);
      sleep(1);
    }
  }
  snprintf(path, 256, "/tmp/sasc-chroot-%d", cardnum);
  if (is_file(path)) {
    snprintf(line, 256, "rm -rf %s", path);
    system(line);
  }
  return true;
}

int send_evocamd(int cardnum, unsigned short servid, unsigned short msg) {
    int evocamdSocket;
    char sockBuffer[3];
    char sockname[256];
    struct sockaddr_un servaddr;
    int clilen;

    snprintf(sockname, 256, "/tmp/sasc-chroot-%d/tmp/camd.socket", cardnum);
    servaddr.sun_family = AF_UNIX;
    strcpy(servaddr.sun_path, sockname);
    clilen = sizeof(servaddr.sun_family) + strlen(servaddr.sun_path);
    if ((evocamdSocket = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
    {
      fprintf(stderr, "[Evocamd] socket creation failed\n");
      return 0;
    }
    if (connect(evocamdSocket, (struct sockaddr*) &servaddr, clilen) < 0)
    {
      fprintf(stderr, "[Evocamd] socket connection failed\n");
      return 0;
    }
    sockBuffer[0] = servid >> 8;
    sockBuffer[1] = servid & 0xff;
    sockBuffer[2] = msg;
    //printf("Sending %d/%d to evocamd\n", servid, msg);
    if (write(evocamdSocket, sockBuffer, 3) < 0)
    {
      fprintf(stderr, "[Evocamd] write failed\n");
      return 0;
    }
    close(evocamdSocket);
    return 1;
}

bool init_evocamd(int cardnum, char *scDir) {
  char path[512];
  char cmd[512];

  //shutdown any evocamd daemons that are already running
  shutdown_evocamd(cardnum);
  snprintf(path, 512, "/tmp/sasc-chroot-%d", cardnum);
  mkdir(path, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP);
  chdir(path);
  snprintf(path, 512, "%s/camd.tar.gz", scDir);
  if (! is_file(path)) {
    fprintf(stderr, "Failed to find camd.tar.gz.  Aborting.");
    return false; 
  }
  snprintf(cmd, 512, "tar -xvzf %s", path);
  if(system(cmd)) {
    fprintf(stderr, "Evocamd extract failed\n");
    return false;
  }
  snprintf(cmd, 512, "sasc-chroot-dvb %d evocamd", cardnum);
  if(system(cmd)) {
    fprintf(stderr, "Evocamd call failed\n");
    return false;
  }
  return true;
}

int start_loopbackca(int cardnum) {
  char path[256];
  int fd;

  snprintf(path, 256, "/proc/ca_emu_%d", cardnum);

  fd = open(path, O_RDONLY);
  if(fd < 0) {
    perror("Failed to open loopback-ca\n");
    return 0;
  }
  pid_t pid = fork();
   if (pid == 0) // child
   {
      //ca_emu_* is processed as follows:
      //  data[0] : number of records to read (<=10)
      //  data[i*100 + 1]  : ioctl command
      //  &data[i*100 + 2] : data record from ioctl
      unsigned char data[1024], *dataptr;
      _ForceIndex(1);
      while(1) {
        int bytes = read(fd, data, 1024);
        lseek(fd, SEEK_SET, 0);
        //printf("Read %d bytes from ca-loopback (%d cmds)\n", bytes, data[0]);
        if (bytes != 1024) {
            continue;
        }
        int count = data[0];
        for (int i=0; i < count; i++) {
          unsigned int cmd;
          memcpy(&cmd, data+ i*100 + 1, sizeof(cmd));
          //printf("Parsing command: %d\n", cmd);
          dataptr =  data+ i*100 + 1 + sizeof(cmd);
          switch(cmd) {
	  case 135: //CA_SET_PID
            {
              ca_pid_t ca_pid;
              memcpy(&ca_pid, dataptr, sizeof(ca_pid));
              ca_pid.index = 1;
              _SetCaPid(&ca_pid);
              break;
            }
          case 134: //CA_SET_DESCR
            {
              ca_descr_t ca_descr;
              memcpy(&ca_descr, dataptr, sizeof(ca_descr));
              ca_descr.index = 1;
              _SetCaDescr(&ca_descr);
              break;
            }
          default:
              fprintf(stderr, "CA Loopback unknown command: %d\n", cmd);
              break;
          }
        }
      }
      close(fd);
      exit(0);
   }
   close(fd);
   return(pid);
}

