/*
 * Softcam plugin to VDR (C++)
 *
 * This code is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This code is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 */

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/un.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <signal.h>
#include <pthread.h>

#include <vdr/pat.h>

#include "cc.h"
#include "network.h"
#include "parse.h"

// START (C) 2002 by Andreas Oberritter <obi@tuxbox.org>

#define MAX_PIDS	4

typedef struct descrambleservice_t
{
	unsigned char valid;
	unsigned char started;
	unsigned short status;
	unsigned short onID;
	unsigned short sID;
	unsigned short Unkwn;
	unsigned short caID;
	unsigned short ecmPID;
	unsigned char numpids;
	unsigned short pid[MAX_PIDS];

} descrambleservice_s;

typedef struct ca_descriptor_s
{
	unsigned char descriptor_tag		: 8;
	unsigned char descriptor_length		: 8;
	unsigned short ca_system_id		: 16;
	unsigned char reserved			: 3;
	unsigned short ca_pid			: 13;
	unsigned char * private_data_byte;
} __attribute__ ((packed)) ca_descriptor;

typedef struct ca_pmt_program_info_s
{
	unsigned char ca_pmt_cmd_id		: 8;
	ca_descriptor * descriptor;
} __attribute__ ((packed)) ca_pmt_program_info;

typedef struct ca_pmt_es_info_s
{
	unsigned char stream_type		: 8;
	unsigned char reserved			: 3;
	unsigned short elementary_pid		: 13;
	unsigned char reserved2			: 4;
	unsigned short es_info_length		: 12;
	ca_pmt_program_info * program_info;
} __attribute__ ((packed)) ca_pmt_es_info;

typedef struct ca_pmt_s
{
	unsigned char ca_pmt_list_management	: 8;
	unsigned short program_number		: 16;
	unsigned char reserved1			: 2;
	unsigned char version_number		: 5;
	unsigned char current_next_indicator	: 1;
	unsigned char reserved2			: 4;
	unsigned short program_info_length	: 12;
	ca_pmt_program_info * program_info;
	ca_pmt_es_info * es_info;
} __attribute__ ((packed)) ca_pmt;
// END (C) 2002 by Andreas Oberritter <obi@tuxbox.org>

#define LIST_MORE 0x00   /* CA application should append a 'MORE' CAPMT object the list, and start receiving the next object */
#define LIST_FIRST 0x01  /* CA application should clear the list when a 'FIRST' CAPMT object is received, and start receiving the next object */
#define LIST_LAST 0x02   /* CA application should append a 'LAST' CAPMT object to the list, and start working with the list */
#define LIST_ONLY 0x03   /* CA application should clear the list when an 'ONLY' CAPMT object is received, and start working with the object */
#define LIST_ADD 0x04  /* CA application should append an 'ADD' CAPMT object to the current list, and start working with the updated list */
#define LIST_UPDATE 0x05 /* CA application should replace an entry in the list with an 'UPDATE' CAPMT object, and start working with the updated list */
/* ca_pmt_cmd_id's: */
#define CMD_OK_DESCRAMBLING 0x01 /* CA application should start descrambling the service in this CAPMT object, as soon as the list of CAPMT objects is complete */

int parse_ca_pmt ( unsigned char * buffer,  unsigned int length);

// -- cCCcamClient -----------------------------------------------------------

class cCardClientCCcam : public cCardClient , private cThread {
private:
  int pmtversion;
  unsigned char sacapmt[4][2048];
  unsigned char savedcw[4][16];
  int pids[4];
  int newcw[4];
  uint64_t timecw[4];
  int ccam_fd[4];
  int cwccam_fd;
  int lastcard;
  int lastpid;
  int failedcw;
  //
  void Writecapmt(int j);
  int CCSelect(int sd, bool forRead, int s);
  bool GetCCamCw(unsigned char *cw, int s);
  int startportlistener();
protected:
  virtual bool lLogin(int cardnum);
  virtual void Action(void);
public:
  cCardClientCCcam(const char *Name);
  virtual bool Init(const char *CfgDir);
  virtual bool ProcessECM(const cEcmInfo *ecm, const unsigned char *data, unsigned char *Cw, int cardnum);
  bool CanHandle(unsigned short SysId)
{
if ((SysId & 0xf000) != 0x5000)
  return true;
else
  return false;
}

};

static cCardClientLinkReg<cCardClientCCcam> __ncd("CCcam");

cCardClientCCcam::cCardClientCCcam(const char *Name)
:cCardClient(Name)
{
  cwccam_fd=-1;
  pmtversion=0;
  ccam_fd[0] = -1;
  ccam_fd[1] = -1;
  ccam_fd[2] = -1;
  ccam_fd[3] = -1;
}


bool cCardClientCCcam::Init(const char *config)
{
  cMutexLock lock(this);
  int num = 0;
  signal(SIGPIPE,SIG_IGN);
  if(!ParseStdConfig(config,&num)) return false;
  return true;
}


bool cCardClientCCcam::lLogin(int cardnum)
{
  int rc;
  int so =0;
  sockaddr_un serv_addr_un;
  char camdsock[256];
  int res;

  struct sockaddr_in servAddr;
  fflush(NULL);
  close(ccam_fd[cardnum]);
  sprintf(camdsock,"/var/emu/chroot%d/tmp/camd.socket",cardnum);
  printf(" socket = %s\n",camdsock);
  ccam_fd[cardnum]=socket(AF_LOCAL, SOCK_STREAM, 0);
  bzero(&serv_addr_un, sizeof(serv_addr_un));
  serv_addr_un.sun_family = AF_LOCAL;
  strcpy(serv_addr_un.sun_path, camdsock);
  res=connect(ccam_fd[cardnum], (const sockaddr*)&serv_addr_un, sizeof(serv_addr_un));
  if (res !=0)
  {
    PRINTF(L_CC_CCCAM,"Couldnt open camd.socket..... errno = %d Aggg",errno);
    close(ccam_fd[cardnum]);
    ccam_fd[cardnum] = -1;
  }
  PRINTF(L_CC_CCCAM,"Opened camd.socket..... ccamd_fd  = %d ",ccam_fd[cardnum]);
  if (cwccam_fd!=-1) return 1;
  PRINTF(L_CC_CCCAM,"logging in");
  so=socket(AF_INET, SOCK_DGRAM, 0);
  if(so<0)
  {
    close(so);
    return false;
  }
  bzero(&servAddr, sizeof(servAddr));
  servAddr.sin_family = AF_INET;
  servAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
  servAddr.sin_port = htons(port);
  rc = bind (so, (struct sockaddr *) &servAddr,sizeof(servAddr));
  if(rc<0)
  {
    close(so);
    PRINTF(L_CC_CCCAM,"not logged in");
    return false;
  }
  cwccam_fd =so;
  PRINTF(L_CC_CCCAM,"logged in");
  startportlistener();

  return true;
}


bool cCardClientCCcam::ProcessECM(const cEcmInfo *ecm, const unsigned char *data, unsigned char *cw, int cardnum)
{

  // if(((ccam_fd ==0) && !Login()) || !CanHandle(ecm->caId)) return false;
  //so.Flush();
//  cMutexLock lock(this);
  //newcw[cardnum] =0;
  unsigned char capmt[2048];
  PRINTF(L_CC_CCCAM,"Processing ECM.... for card %d",cardnum);
  const int caid=ecm->caId;
  const int pid =ecm->ecm_pid;
  int lenpos=10;
  int len=19;
  int wp=31;
  int casys[2];
  casys[0]=caid;
  casys[1]=0;

  //PRINTF(L_CC_CCCAM,"Card num beign used... - %d, pid %d progid %d provid %d caid %d",cardnum, pid,ecm->prgId, ecm->provId, ecm->caId );
  memcpy(capmt,"\x9f\x80\x32\x82\x00\x00", 6);

  capmt[6]=0x01;
                 //prg-nr
  capmt[7]=(unsigned char)((ecm->prgId>>8) & 0xff);
                 //prg-nr
  capmt[8]=(unsigned char)(ecm->prgId & 0xff);

  capmt[9]=pmtversion;     //reserved - version - current/next
  pmtversion++;
  pmtversion%=32;
                 //0x00;  reserved - prg-info len
  capmt[10]=(unsigned char)((ecm->prgId>>8) & 0xff);
                 //0x00;  prg-info len
  capmt[11]=(unsigned char)(ecm->prgId & 0xff);
  // this is ASN.1 this is NOT the way you do BER...
  capmt[12]=0x01;        //CMD_OK_DESCRAMBLING;   ca pmt command id
  capmt[13]=0x81;        // private descr.. dvbnamespace
  capmt[14]=0x08;
  {
    int dvb_namespace = cardnum<<8;
    capmt[15]=dvb_namespace>>24;
    capmt[16]=(dvb_namespace>>16)&0xFF;
    capmt[17]=(dvb_namespace>>8)&0xFF;
    capmt[18]=dvb_namespace&0xFF;
  }
  capmt[19]=ecm->transponder>>8;
  capmt[20]=ecm->transponder&0xFF;
  capmt[21]=ecm->provId>>8;
  capmt[22]=ecm->provId&0xFF;

  capmt[23]=0x82;        // demuxer kram..
  capmt[24]=0x02;
  capmt[25]= 1<<cardnum ;
  capmt[26]= cardnum ;
  capmt[27]=0x84;        // pmt pid
  capmt[28]=0x02;
  capmt[29]=pid>>8;
  capmt[30]=pid&0xFF;
  bool streamflag = 1;
  int n=GetCaDescriptors(ecm->source,ecm->transponder,ecm->prgId,casys,sizeof(capmt)-31,&capmt[31],streamflag);
  len+=n;
  wp+=n;
  capmt[wp++] = 0x01;
  capmt[wp++] = 0x0f;
                 // cccam uses this one as PID to program ca0..
  capmt[wp++] = cardnum&0xFF;
  capmt[wp++] = 0x00;      //es_length
  capmt[wp++] = 0x06;      //es ca_pmt_cmd_id

  capmt[lenpos]=((len & 0xf00)>>8);
  capmt[lenpos+1]=(len & 0xff);
  capmt[4]=((wp-6)>>8) & 0xff;
  capmt[5]=(wp-6) & 0xff;
  if (newcw[cardnum])
  {
    cTimeMs k;
    if ((k.Now() - timecw[cardnum]) > 3000)
                 // just too old lets bin em...
        newcw[cardnum] = 0;
  }
  if ((pid != pids[cardnum]) || (ccam_fd[cardnum] == -1))
  {              // channel change
    PRINTF(L_CC_CCCAM,"sending capmts ");
    pids[cardnum] = pid;
    newcw[cardnum] =0;
    memcpy(sacapmt[cardnum],capmt,2048);
    lLogin(cardnum);
    Writecapmt(cardnum);
    usleep(300000);
  }
  lastcard = cardnum;
  lastpid = pid;
  failedcw=0;
  int u=0;
  while ((newcw[cardnum] == 0) && (u++<700))
    usleep(1000);      // give the card a chance to decode it...
  if (newcw[cardnum] == 0)
  {
    // somethings up, so we will send capmt again.
    pids[cardnum] = pid;
  //  memcpy(capmt,sacapmt[cardnum],2048);
    Writecapmt(cardnum);
  }
  u=0;
  while ((newcw[cardnum] == 0) && (u++<1000))
    usleep(1000);
  if (newcw[cardnum] == 0)
  {
    PRINTF(L_CC_CCCAM,"FAILED ECM FOR CARD %d !!!!!!!!!!!!!!!!!!",cardnum);
      sacapmt[cardnum][9]=pmtversion;     //reserved - version - current/next
  pmtversion++;
  pmtversion%=32;
    failedcw++;
    if (failedcw == 10)
      {
      // CCcam is having problems lets mark it for a restart....
       FILE *i = fopen("/tmp/killCCcam","w+");
             fclose(i);


      }
    return false;
  }
  memcpy(cw,savedcw[cardnum],16);
  newcw[cardnum] =0;
  PRINTF(L_CC_CCCAM," GOT CW FOR CARD %d !!!!!!!!!!!!!!!!!!",cardnum);
  sacapmt[cardnum][9]=pmtversion;     //reserved - version - current/next
  pmtversion++;
  pmtversion%=32;
  failedcw=0;
    Writecapmt(cardnum);
  return true;
}


void cCardClientCCcam::Writecapmt(int n)
{
  int len;
  int list_management ;
  list_management = LIST_ONLY;
  sacapmt[n][6] = list_management;
  len = sacapmt[n][4] << 8;
  len |= sacapmt[n][5];
  len += 6;
  int retry = 1;
  while (retry)
  {
  PRINTF(L_CC_CCCAM,"Writing capmt for card %d %d =================================================",n, sacapmt[n][6]);
  if (ccam_fd[n]==-1)
    lLogin(n);
  if (ccam_fd[n]==-1)
    return ;

  int r = write(ccam_fd[n], &sacapmt[n],len );
    if (r != len)
      {
      PRINTF(L_CC_CCCAM,"CCcam probably has crashed or been killed...");
      close(ccam_fd[n]);
      ccam_fd[n]=-1;
      retry++;
      if (retry > 2)
        retry =0;
      }
  else
  retry =0;
  }
}


int cCardClientCCcam::startportlistener()
{
  PRINTF(L_CC_CCCAM,"starting UDP listener");
  SetDescription("CCcamd listener...");
  Start();

  return 1;
}


void cCardClientCCcam::Action()
{
  unsigned char cw[18];
  while (1)
  {
    if (GetCCamCw(cw,0))
    {
      int pid;
      cTimeMs k;
      pid = cw[0] + (cw[1]<<8);
      PRINTF(L_CC_CCCAM," Got: %02hhx%02hhx  %02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx  %02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx",
        cw[0], cw[1], cw[2], cw[3],
        cw[4], cw[5], cw[6], cw[7],
        cw[8], cw[9], cw[10], cw[11],
        cw[12], cw[13], cw[14], cw[15], cw[16], cw[17]);

      if (cw[1] == 0x0f)
      {
        int n = cw[0];
        if (memcmp(savedcw[n],cw+2,16))
        {
          if (newcw[n] == 1)
          {
            close(ccam_fd[n]);
            ccam_fd[n]=-1;
          }
          memcpy(savedcw[n],cw+2,16);
          newcw[n] =1;
          timecw[n]= k.Now();
          PRINTF(L_CC_CCCAM,"SAVING KEYS USING PID FROM CCCAM  %d !!!!!!!!!!!", n);
        }
      }
    }
  }
}


bool cCardClientCCcam::GetCCamCw(unsigned char *cw, int s)
{
  unsigned char msg[18];
  int n;
  struct sockaddr cliAddr;
  socklen_t cliLen;
  cliLen = sizeof(cliAddr);
  if (CCSelect(cwccam_fd, true,100))
    n = ::recvfrom(cwccam_fd, msg, 18, 0, (struct sockaddr *) &cliAddr, &cliLen);
  else
    return false;
  if(n<0)
    return false;
  memcpy(cw, msg, 18);
  return(true);
}


int cCardClientCCcam::CCSelect(int sd, bool forRead, int s)
{
  if(sd>=0)
  {
    fd_set fds;
    FD_ZERO(&fds); FD_SET(sd,&fds);
    struct timeval tv;
    tv.tv_sec=4; tv.tv_usec=000;
    int r=::select(sd+1,forRead ? &fds:0,forRead ? 0:&fds,0,&tv);
    if(r>0) return 1;
    else if(r<0)
    {
      return 0;
    }
    else
    {
      return 0;
    }
  }
  return 0;
}


int caid_count =1 ;
int parse_ca_pmt ( unsigned char * buffer,  unsigned int length)
{
  unsigned short i, j;
  ca_pmt * pmt;
  descrambleservice_s service;

  memset(&service, 0, sizeof(descrambleservice_s));

  pmt = (ca_pmt *) malloc(sizeof(ca_pmt));

  pmt->ca_pmt_list_management = buffer[0];
  pmt->program_number = (buffer[1] << 8) | buffer[2];
  pmt->program_info_length = ((buffer[4] & 0x0F) << 8) | buffer[5];

  printf("ca_pmt_list_management: %02x\n", pmt->ca_pmt_list_management);
  printf("prugram number: %04x\n", pmt->program_number);
  printf("program_info_length: %04x\n", pmt->program_info_length);
  switch (pmt->ca_pmt_list_management)
  {
    case 0x01:         /* first */
      printf("first\n");
      break;
    case 0x03:         /* only */
      printf("only\n");
      break;

    default:
      printf("unknown\n");
      break;
  }

  if (pmt->program_info_length != 0)
  {
    pmt->program_info = (ca_pmt_program_info *) malloc(sizeof(ca_pmt_program_info));
    pmt->program_info->ca_pmt_cmd_id = buffer[6];
    printf("ca_pmt_id: %04x\n", pmt->program_info->ca_pmt_cmd_id);
    pmt->program_info->descriptor = (ca_descriptor *) malloc(sizeof(ca_descriptor));

    for (i = 0; i < pmt->program_info_length - 1; i += pmt->program_info->descriptor->descriptor_length + 2)
    {
      pmt->program_info->descriptor->descriptor_length = buffer[i + 8];
      pmt->program_info->descriptor->ca_system_id = (buffer[i + 9] << 8) | buffer[i + 10];
      pmt->program_info->descriptor->ca_pid = ((buffer[i + 11] & 0x1F) << 8)| buffer[i + 12];
      printf("ca_descriptor_length %d\n",pmt->program_info->descriptor->descriptor_length);
      printf("ca_system_id: %04x\n", pmt->program_info->descriptor->ca_system_id);
      printf("ca_pid: %04x\n", pmt->program_info->descriptor->ca_pid);

    }

    free(pmt->program_info->descriptor);
    free(pmt->program_info);
  }

  pmt->es_info = (ca_pmt_es_info *) malloc(sizeof(ca_pmt_es_info));

  for (i = pmt->program_info_length + 6; i < length; i += pmt->es_info->es_info_length + 5)
  {
    if (service.numpids == MAX_PIDS)
    {
      break;
    }

    pmt->es_info->stream_type = buffer[i];
    pmt->es_info->elementary_pid = ((buffer[i + 1] & 0x1F) << 8) | buffer[i + 2];
    pmt->es_info->es_info_length = ((buffer[i + 3] & 0x0F) << 8) | buffer[i + 4];

    printf("stream_type: %02x\n", pmt->es_info->stream_type);
    printf("elementary_pid: %04x\n", pmt->es_info->elementary_pid);
    printf("es_info_length: %04x\n", pmt->es_info->es_info_length);

    service.pid[service.numpids++] = pmt->es_info->elementary_pid;

    if (pmt->es_info->es_info_length != 0)
    {
      pmt->es_info->program_info = (ca_pmt_program_info *) malloc(sizeof(ca_pmt_program_info));

      pmt->es_info->program_info->ca_pmt_cmd_id = buffer[i + 5];
      pmt->es_info->program_info->descriptor = (ca_descriptor *)malloc(sizeof(ca_descriptor));

      for (j = 0; j < pmt->es_info->es_info_length - 1; j += pmt->es_info->program_info->descriptor->descriptor_length + 2)
      {
        pmt->es_info->program_info->descriptor->descriptor_length = buffer[i + j + 7];
        pmt->es_info->program_info->descriptor->ca_system_id = (buffer[i + j + 8] << 8) | buffer[i + j + 9];
        pmt->es_info->program_info->descriptor->ca_pid = ((buffer[i + j + 10] & 0x1F) << 8) | buffer[i + j + 11];

        printf("ca_system_id: %04x\n", pmt->es_info->program_info->descriptor->ca_system_id);
        printf("ca_pid: %04x\n", pmt->es_info->program_info->descriptor->ca_pid);

        if (service.caID != 0)
        {
          break;
        }
      }

      free(pmt->es_info->program_info->descriptor);
      free(pmt->es_info->program_info);
    }
  }

  service.sID = pmt->program_number;

  free(pmt->es_info);
  free(pmt);

  if ((service.numpids != 0) && (service.caID != 0))
  {
    service.onID = 0x0085;

    service.Unkwn = 0x0104;

    return 0;        //   return adddescrambleservicestruct(&service);
  }
  else
  {
    return -1;
  }
}


unsigned int parse_length_field (unsigned char * buffer)
{
  unsigned char size_indicator = (buffer[0] >> 7) & 0x01;
  unsigned int length_value = 0;

  if (size_indicator == 0)
  {
    length_value = buffer[0] & 0x7F;
  }

  else if (size_indicator == 1)
  {
    unsigned char length_field_size = buffer[0] & 0x7F;
    unsigned int i;

    for (i = 0; i < length_field_size; i++)
    {
      length_value = (length_value << 8) | buffer[i + 1];
    }
  }

  return length_value;
}


unsigned char get_length_field_size (unsigned int length)
{
  if (length < 0x80)
  {
    return 0x01;
  }

  if (length < 0x100)
  {
    return 0x02;
  }

  if (length < 0x10000)
  {
    return 0x03;
  }

  if (length < 0x1000000)
  {
    return 0x04;
  }

  else
  {
    return 0x05;
  }
}
