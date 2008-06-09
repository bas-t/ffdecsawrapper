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
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

#include <vdr/pat.h>

#include "cc.h"
#include "network.h"
//#include "parse.h"

#define LIST_ONLY 0x03   /* CA application should clear the list when an 'ONLY' CAPMT object is received, and start working with the object */

// -- cCardClientCCcam ---------------------------------------------------------

class cCardClientCCcam : public cCardClient , private cThread {
private:
  cNetSocket so;
  //
  int pmtversion;
  unsigned char sacapmt[4][2048];
  unsigned char savedcw[4][16];
  int pids[4];
  int newcw[4];
  uint64_t timecw[4];
  int ccam_fd[4];
  int lastcard;
  int lastpid;
  int failedcw;
  //
  void Writecapmt(int j);
protected:
  virtual bool lLogin(int cardnum);
  virtual void Action(void);
public:
  cCardClientCCcam(const char *Name);
  virtual bool Init(const char *CfgDir);
  virtual bool ProcessECM(const cEcmInfo *ecm, const unsigned char *data, unsigned char *Cw, int cardnum);
  virtual bool CanHandle(unsigned short SysId);
  };

static cCardClientLinkReg<cCardClientCCcam> __ncd("CCcam");

cCardClientCCcam::cCardClientCCcam(const char *Name)
:cCardClient(Name)
,cThread("CCcam listener")
,so(DEFAULT_CONNECT_TIMEOUT,2,3600,true)
{
  pmtversion=0;
  ccam_fd[0]=-1; ccam_fd[1]=-1; ccam_fd[2]=-1; ccam_fd[3]=-1;
}

bool cCardClientCCcam::Init(const char *config)
{
  cMutexLock lock(this);
  int num=0;
  signal(SIGPIPE,SIG_IGN);
  if(!ParseStdConfig(config,&num)) return false;
  return true;
}

bool cCardClientCCcam::CanHandle(unsigned short SysId)
{
  if((SysId & 0xf000) != 0x5000) return true;
  return false;
}

bool cCardClientCCcam::lLogin(int cardnum)
{
  sockaddr_un serv_addr_un;
  char camdsock[256];
  int res;

  close(ccam_fd[cardnum]);
  sprintf(camdsock,"/var/emu/chroot%d/tmp/camd.socket",cardnum);
  printf(" socket = %s\n",camdsock);
  ccam_fd[cardnum]=socket(AF_LOCAL, SOCK_STREAM, 0);
  bzero(&serv_addr_un, sizeof(serv_addr_un));
  serv_addr_un.sun_family = AF_LOCAL;
  strcpy(serv_addr_un.sun_path, camdsock);
  res=connect(ccam_fd[cardnum], (const sockaddr*)&serv_addr_un, sizeof(serv_addr_un));
  if (res !=0) {
    PRINTF(L_CC_CCCAM,"Couldnt open camd.socket..... errno = %d Aggg",errno);
    close(ccam_fd[cardnum]);
    ccam_fd[cardnum] = -1;
    }
  PRINTF(L_CC_CCCAM,"Opened camd.socket..... ccamd_fd  = %d ",ccam_fd[cardnum]);

  if(!so.Connected()) {
    so.Disconnect();
    if(!so.Bind("127.0.0.1",port)) return false;
    PRINTF(L_CC_CCCAM,"Bound to port %d, starting UDP listener",port);
    Start();
    }
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
  capmt[6]=0x01; //prg-nr
  capmt[7]=(unsigned char)((ecm->prgId>>8) & 0xff); //prg-nr
  capmt[8]=(unsigned char)(ecm->prgId & 0xff);
  capmt[9]=pmtversion;     //reserved - version - current/next
  pmtversion++;
  pmtversion%=32; //0x00;  reserved - prg-info len
  capmt[10]=(unsigned char)((ecm->prgId>>8) & 0xff); //0x00;  prg-info len
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
  if (newcw[cardnum]) {
    if ((cTimeMs::Now() - timecw[cardnum]) > 3000)
      // just too old lets bin em...
      newcw[cardnum] = 0;
    }
  if ((pid != pids[cardnum]) || (ccam_fd[cardnum] == -1)) { // channel change
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
  if (newcw[cardnum] == 0) {
    // somethings up, so we will send capmt again.
    pids[cardnum] = pid;
    //  memcpy(capmt,sacapmt[cardnum],2048);
    Writecapmt(cardnum);
    }
  u=0;
  while ((newcw[cardnum] == 0) && (u++<1000))
    usleep(1000);
  if (newcw[cardnum] == 0) {
    PRINTF(L_CC_CCCAM,"FAILED ECM FOR CARD %d !!!!!!!!!!!!!!!!!!",cardnum);
    sacapmt[cardnum][9]=pmtversion;     //reserved - version - current/next
    pmtversion++;
    pmtversion%=32;
    failedcw++;
    if (failedcw == 10) {
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
  while (retry) {
    PRINTF(L_CC_CCCAM,"Writing capmt for card %d %d =================================================",n, sacapmt[n][6]);
    if (ccam_fd[n]==-1)
      lLogin(n);
    if (ccam_fd[n]==-1)
      return ;

    int r = write(ccam_fd[n], &sacapmt[n],len );
    if (r != len) {
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

void cCardClientCCcam::Action()
{
  unsigned char cw[18];
  while(Running()) {
    if(so.Read(cw,sizeof(cw))==sizeof(cw)) {
      PRINTF(L_CC_CCCAM," Got: %02hhx%02hhx  %02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx  %02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx",
        cw[0],cw[1],
        cw[2],cw[3],cw[4],cw[5],cw[6],cw[7],cw[8],cw[9],
        cw[10],cw[11],cw[12],cw[13],cw[14],cw[15],cw[16],cw[17]);

      if (cw[1] == 0x0f) {
        int n = cw[0];
        if (memcmp(savedcw[n],cw+2,16)) {
          if (newcw[n] == 1) {
            close(ccam_fd[n]);
            ccam_fd[n]=-1;
            }
          memcpy(savedcw[n],cw+2,16);
          newcw[n] =1;
          timecw[n]= cTimeMs::Now();
          PRINTF(L_CC_CCCAM,"SAVING KEYS USING PID FROM CCCAM  %d !!!!!!!!!!!", n);
          }
        }
      }
    }
}
