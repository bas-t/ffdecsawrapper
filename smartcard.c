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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <unistd.h>
#include <termios.h>
#include <linux/serial.h>
#include <ctype.h>

#include <vdr/tools.h>
#include <vdr/thread.h>

#include "smartcard.h"
#include "misc.h"
#include "log-core.h"

#define DATAFILE "smartcard.conf"
#define ISO_FREQ 3571200 // Hz

//#define SER_EMU   // use serial emulation (select one of the following)
//#define EMU_SECA  // fake Seca card
//#define EMU_IRD   // fake Irdeto/Beta card
//#define EMU_IRD_384 // fake ACS 384, if undefined ACS 383
//#define EMU_CRYPTO // fake Cryptoworks card
//#define NO_PTS_PROTO // disable PTS protocol (baudrate changes)

struct BaudRates {
  int real;
  speed_t apival;
  };

static const struct BaudRates BaudRateTab[] = {
  {   9600, B9600   },
  {  19200, B19200  },
  {  38400, B38400  },
  {  57600, B57600  },
  { 115200, B115200 },
  { 230400, B230400 }
  };

// -- cSerial ------------------------------------------------------------------

class cSerial {
private:
  char *devName;
  int fd;
  int currMode, statInv;
  bool invRST;
  //
  speed_t FindBaud(int baud);
#ifdef SER_EMU
  unsigned char devBuff[1024], nextSW[SB_LEN];
  int buffCount, dataLen, record;
  bool cmdStart, rts, dsr, flag;
  int remTime;
#endif
public:
  cSerial(const char *DevName, bool invCD, bool InvRST);
  ~cSerial();
  bool Open(void);
  bool SetMode(int mode, int baud=9600);
  int CurrentMode(void) const { return currMode; }
  void Flush(void);
  int Read(unsigned char *mem, int len, int timeout, int initialTimeout=0);
  int Write(const unsigned char *mem, int len, int delay=0);
  void ToggleRTS(void);
  bool CheckCAR(void);
  void Close(void);
  const char *DeviceName(void) const { return devName; }
  };

cSerial::cSerial(const char *DevName, bool invCD, bool InvRST)
{
  devName=strdup(DevName);
  statInv=invCD ? TIOCM_CAR:0;
  invRST=InvRST;
  fd=-1; currMode=SM_NONE;
}

cSerial::~cSerial()
{
  Close();
  free(devName);
}

#ifndef SER_EMU

bool cSerial::Open(void)
{
  PRINTF(L_CORE_SERIAL,"%s: open serial port",devName);
  fd=open(devName,O_RDWR|O_NONBLOCK|O_NOCTTY);
  if(fd>=0) {
    PRINTF(L_CORE_SERIAL,"%s: set DTR/RTS",devName);
    unsigned int modembits;
    modembits=TIOCM_DTR; CHECK(ioctl(fd, TIOCMBIS, &modembits));
    modembits=TIOCM_RTS; CHECK(ioctl(fd, invRST?TIOCMBIS:TIOCMBIC, &modembits));
    PRINTF(L_CORE_SERIAL,"%s: init done",devName);
    return true;
    }
  else PRINTF(L_GEN_ERROR,"%s: open failed: %s",devName,strerror(errno));
  return false;
}

void cSerial::Close(void)
{
  if(fd>=0) {
    PRINTF(L_CORE_SERIAL,"%s: shutting down",devName);
    Flush();
    unsigned int modembits=0;
    CHECK(ioctl(fd,TIOCMSET,&modembits));
    close(fd); fd=-1;
    PRINTF(L_CORE_SERIAL,"%s: shutdown done",devName);
    }
}

speed_t cSerial::FindBaud(int baud)
{
  for(int i=0; i<(int)(sizeof(BaudRateTab)/sizeof(struct BaudRates)); i++) {
    int b=BaudRateTab[i].real;
    int d=((b-baud)*10000)/b;
    if(abs(d)<=300) {
      PRINTF(L_CORE_SERIAL,"%s: requested baudrate %d -> %d (%+.2f%%)",devName,baud,b,(float)d/100.0);
      return BaudRateTab[i].apival;
      }
    }
  PRINTF(L_CORE_SERIAL,"%s: requested baudrate %d -> custom",devName,baud);
  return B0;
}

bool cSerial::SetMode(int mode, int baud)
{
  if(fd>=0) {
    speed_t bconst=FindBaud(baud);
    bool custom=false;
    if(bconst==B0) { custom=true; bconst=B38400; }
    
    struct termios tio;
    memset(&tio,0,sizeof(tio));
    LBSTARTF(L_CORE_SERIAL);
    LBPUT("%s: set serial options: %d,",devName,baud);
    tio.c_cflag = (CS8 | CREAD | HUPCL | CLOCAL);
    if(!(mode&SM_1SB)) tio.c_cflag |= CSTOPB;
    tio.c_iflag = (INPCK | BRKINT);
    tio.c_cc[VMIN] = 1;
    cfsetispeed(&tio,bconst);
    cfsetospeed(&tio,bconst);
    switch(mode&SM_MASK) {
      case SM_8E2: 
        LBPUT("8e%d",(mode&SM_1SB)?1:2);
        tio.c_cflag |= PARENB; break;
      case SM_8N2:
        LBPUT("8n%d",(mode&SM_1SB)?1:2);
        break;
      case SM_8O2:
        LBPUT("8o%d",(mode&SM_1SB)?1:2);
        tio.c_cflag |= (PARENB | PARODD); break;
      default:
        LBPUT("BAD MODE");
        return false;
      }
    LBEND();

    struct serial_struct s;
    if(ioctl(fd,TIOCGSERIAL,&s)<0) {
      PRINTF(L_GEN_ERROR,"%s: get serial failed: %s",devName,strerror(errno));
      return false;
      }
    if(!custom && ((s.flags&ASYNC_SPD_MASK)==ASYNC_SPD_CUST || s.custom_divisor!=0)) {
      s.custom_divisor=0;
      s.flags &= ~ASYNC_SPD_MASK;
      if(ioctl(fd,TIOCSSERIAL,&s)<0) {
        PRINTF(L_GEN_ERROR,"%s: set serial failed: %s",devName,strerror(errno));
        return false;
        }
      }
    if(!tcsetattr(fd,TCSANOW,&tio)) {
      if(custom) {
        s.custom_divisor=(s.baud_base+(baud/2))/baud;
        s.flags=(s.flags&~ASYNC_SPD_MASK) | ASYNC_SPD_CUST;
        PRINTF(L_CORE_SERIAL,"%s: custom: baud_base=%d baud=%d devisor=%d -> effective baudrate %d (%+.2f%% off)",devName,s.baud_base,baud,s.custom_divisor,s.baud_base/s.custom_divisor,(float)(s.baud_base/s.custom_divisor-baud)/(float)baud);
        if(ioctl(fd,TIOCSSERIAL,&s)<0) {
          PRINTF(L_GEN_ERROR,"%s: set serial failed: %s",devName,strerror(errno));
          return false;
          }
        }
      currMode=mode; Flush();
      return true;
      }
    else PRINTF(L_GEN_ERROR,"%s: tcsetattr failed: %s",devName,strerror(errno));
    }
  return false;
}

void cSerial::Flush(void)
{
  if(fd>=0)
    CHECK(tcflush(fd,TCIOFLUSH));
}

int cSerial::Read(unsigned char *mem, int len, int timeout, int initialTimeout)
{
  PRINTF(L_CORE_SERIAL,"%s: read len=%d timeout=%d:%d",devName,len,timeout,initialTimeout);
  bool incomplete=false;
  if(len<0) { len=-len; incomplete=true; }
  int to=initialTimeout>0 ? initialTimeout : timeout;
  int n=0;
  while(n<len) {
    int r=read(fd,mem+n,len-n);
    if(r<=0) {
      if(r==0) PRINTF(L_CORE_SERIAL,"%s: read bogus eof",devName);
      if(errno==EAGAIN) {
        struct pollfd u;
        u.fd=fd; u.events=POLLIN;
        r=poll(&u,1,to);
        if(r<0) {
          PRINTF(L_GEN_ERROR,"%s: read poll failed: %s",devName,strerror(errno));
          return -1;
          }
        else if(r==0) { // timeout
          PRINTF(L_CORE_SERIAL,"%s: read timeout (%d ms)",devName,to);
          if(n>0 && incomplete) break; // return bytes read so far
          return -2;
          }
        continue;
        }
      else {
        PRINTF(L_GEN_ERROR,"%s: read failed: %s",devName,strerror(errno));
        return -1;
        }
      }
    n+=r; to=timeout;
    }
  HEXDUMP(L_CORE_SERIAL,mem,n,"%s: read data",devName);
  return n;
}

int cSerial::Write(const unsigned char *mem, int len, int delay)
{
  PRINTF(L_CORE_SERIAL,"%s: write len=%d delay=%d",devName,len,delay);
  HEXDUMP(L_CORE_SERIAL,mem,len,"%s: write data",devName);
  Flush();
  int n=0;
  while(n<len) {
    struct pollfd u;
    u.fd=fd; u.events=POLLOUT;
    int r;
    do {
      r=poll(&u,1,500);
      if(r<0) {
        PRINTF(L_GEN_ERROR,"%s: write poll failed: %s",devName,strerror(errno));
        return -1;
        }
      else if(r==0) { // timeout
        PRINTF(L_CORE_SERIAL,"%s: write timeout",devName);
        return -2;
        }
      } while(r<1);
    r=write(fd,mem+n,delay>0?1:len-n);
    if(r<0 && errno!=EAGAIN) {
      PRINTF(L_GEN_ERROR,"%s: write failed: %s",devName,strerror(errno));
      return -1;
      }
    if(r>0) n+=r;
    if(delay>0) cCondWait::SleepMs(delay);
    }
  return n;
}

void cSerial::ToggleRTS(void)
{
  int mask=0;
  if(ioctl(fd,TIOCMGET,&mask)<0) { LOG_ERROR; return; }
  if(mask&TIOCM_RTS) { mask&=~TIOCM_RTS; PRINTF(L_CORE_SERIAL,"%s: toggle RTS, now off",devName); }
  else               { mask|= TIOCM_RTS; PRINTF(L_CORE_SERIAL,"%s: toggle RTS, now on",devName); }
  CHECK(ioctl(fd,TIOCMSET,&mask));
  Flush();
}

bool cSerial::CheckCAR(void)
{
  int status=0;
  if(ioctl(fd,TIOCMGET,&status)<0) { LOG_ERROR; return false; }
  PRINTF(L_CORE_SERIAL,"%s: CAR is %sactive (lines:%s%s%s%s%s%s%s%s%s)",
     devName,(status&TIOCM_CAR)?"in":"",
     (status&TIOCM_LE)?" LE":"",  (status&TIOCM_DTR)?" DTR":"",
     (status&TIOCM_RTS)?" RTS":"",(status&TIOCM_ST)?" ST":"",
     (status&TIOCM_SR)?" SR":"",  (status&TIOCM_CTS)?" CTS":"",
     (status&TIOCM_CAR)?" CAR":"",(status&TIOCM_RNG)?" RNG":"",
     (status&TIOCM_DSR)?" DSR":"" );
  status^=statInv; // invert status for broken cardreaders
  return !(status & TIOCM_CAR);
}

#else //SER_EMU

//
// emulator for a serial phoenix interface
// (activate in smartcard.h)
//

bool cSerial::Open(void)
{
  PRINTF(L_CORE_SERIAL,"serial: open serial port %s (emulator)",devName);
  PRINTF(L_CORE_SERIAL,"serial: init done");
  fd=1; buffCount=0; cmdStart=true; rts=false; flag=false;
  remTime=time(0); dsr=true;
  return true;
}

void cSerial::Close(void)
{
  if(fd>=0) {
    PRINTF(L_CORE_SERIAL,"serial: shutting down ... ");
    fd=-1;
    PRINTF(L_CORE_SERIAL,"done");
    }
}

speed_t cSerial::FindBaud(int baud)
{
  return B0;
}

bool cSerial::SetMode(int mode, int baud)
{
  currMode=mode;
  Flush();
  return true;
}

void cSerial::Flush(void)
{
  buffCount=0;
}

#define PUSH(mem,len) { memcpy(devBuff+buffCount,(mem),(len)); buffCount+=(len); }
#define PUSHB(b)      { devBuff[buffCount++]=(b); }

int cSerial::Read(unsigned char *mem, int len, int timeout, int initialTimeout)
{
  PRINTF(L_CORE_SERIAL,"serial: read len=%d timeout=%d",len,timeout);
  if(len<0) {
    len=-len;
    if(buffCount<len) len=buffCount;
    }
  if(buffCount<len) return -2; // timeout
  memcpy(mem,devBuff,len);
  memmove(devBuff,devBuff+len,buffCount-len);
  buffCount-=len;
  return len;
}

int cSerial::Write(const unsigned char *mem, int len, int delay)
{
  PRINTF(L_CORE_SERIAL,"serial: write len=%d delay=%d",len,delay);
  Flush();
  cCondWait::SleepMs(300);
  PUSH(mem,len); // echo back
#if defined(EMU_SECA)
  if(cmdStart && len==CMD_LEN) { // new INS
    PUSHB(mem[INS_IDX]); // ACK byte
    cmdStart=false;
    dataLen=mem[LEN_IDX];
    unsigned char data[256];
    memset(data,0,sizeof(data));
    nextSW[0]=0x90; nextSW[1]=0x00;
    bool readcmd=false;
    switch(mem[0]*256+mem[1]) {
      case 0xc13a:
      case 0xc10e:
        readcmd=true;
        break;
      case 0xc116:
        data[3]=0x04;
        readcmd=true;
        break;
      case 0xc112:
        data[1]=0x64;
        data[22]=0x1d; data[23]=0x84;
        readcmd=true;
        break;
      case 0xc132:
        data[1]=0xFF; data[2]=0xFF; data[3]=0xFF; data[4]=0xFF;
        data[5]=0xFF; data[6]=0xFF; data[7]=0xFF; data[8]=0xFF;
        readcmd=true;
        break;
      }
    if(readcmd) {
      PUSH(data,dataLen);
      PUSH(nextSW,2);
      cmdStart=true;
      }
    }
  else {
    dataLen-=len;
    if(dataLen<=0 && !cmdStart) {
      PUSH(nextSW,2);
      cmdStart=true;
      }
    }
#elif defined(EMU_CRYPTO)
  if(cmdStart && len==CMD_LEN) { // new INS
    cmdStart=false;
    dataLen=mem[LEN_IDX];
    unsigned char data[256];
    memset(data,0,sizeof(data));
    nextSW[0]=0x90; nextSW[1]=0x00;
    bool readcmd=false;
    switch(mem[0]*256+mem[1]) {
      case 0xa4a4:
        switch(mem[5]*256+mem[6]) {
          case 0x2f01: 
          case 0x0f00:
          case 0x0f20:
          //case 0x0f40:
          case 0x0f60:
          case 0x0e11:
          case 0x1f88:
          case 0x1f8c: nextSW[0]=0x9f; nextSW[1]=0x11; break;
          default:     nextSW[0]=0x94; nextSW[1]=0x04; break;
          }
        break;
      case 0xa4a2:
        if(mem[2]==1) {
          if(mem[4]==3) {
            record=0x01;
            nextSW[0]=0x9f; nextSW[1]=0x26;
            }
          else {
            record=0x02;
            nextSW[0]=0x9f; nextSW[1]=0x42;
            }
          }
        else {
          record=mem[5];
          switch(record) {
            case 0x80: nextSW[0]=0x9f; nextSW[1]=0x07; break;
            case 0xD1: nextSW[0]=0x9f; nextSW[1]=0x04; break;
            case 0x9F: nextSW[0]=0x9f; nextSW[1]=0x03; break;
            case 0x9E: nextSW[0]=0x9f; nextSW[1]=0x42; break;
            case 0xD6:
            case 0xC0: nextSW[0]=0x9f; nextSW[1]=0x12; break;
            default:   nextSW[0]=0x94; nextSW[1]=0x02; break;
            }
          }
        break;
      case 0xa4b2:
        readcmd=true;
        switch(record) {
          case 0x01:
            if(mem[3]==1) {
              nextSW[0]=0x94; nextSW[1]=0x02;
              dataLen=0;
              }
            else {
              dataLen=0x26;
              static const unsigned char resp[0x26] = {
                0x83,0x01,0x88,
                0x8c,0x03,0x40,0x30,0x40,
                0x8d,0x04,0x16,0xac,0x16,0xba,
                0xd5,0x10,'D','u','m','m','y',0,0,0,0,0,0,0,0x12,0x34,0x56,0x67,
                0x8f,0x01,0x55,
                0x91,0x01,0x40
                };
              memcpy(data,resp,sizeof(resp));
              }
            break;
          case 0x02:
            if(mem[3]==1) {
              nextSW[0]=0x94; nextSW[1]=0x02;
              dataLen=0;
              }
            else {
              dataLen=0x42;
              static const unsigned char resp[0x42] = {
                0x83,0x01,0x88,
                0x8c,0x03,0xA0,0x30,0x40,
                0x8d,0x04,0x16,0xac,0x16,0xba,
                0xd5,0x10,'D','u','m','m','y',0,0,0,0,0,0,0,0x12,0x34,0x56,0x67,
                0x92,0x20,0x00 // ...
                };
              memcpy(data,resp,sizeof(resp));
              }    
            break;
          case 0x80:
            dataLen=0x07;
            data[0]=0x80;
            data[1]=0x05;
            data[2]=0x5c;
            data[3]=0x0c;
            data[4]=0x00;
            data[5]=0xec;
            data[6]=0x0a;
            break;
          case 0xD1:
            dataLen=0x04;
            data[0]=0xd1;
            data[1]=0x02;
            data[2]=0x0d;
            data[3]=0x22;
            break;
          case 0x9F:
            dataLen=0x03;
            data[0]=0x9f;
            data[1]=0x01;
            data[2]=0x88;
            break;
          case 0x9E:
            {
            dataLen=0x42;
            static const unsigned char resp[0x42] = {
              0x9E,0x40,
              0x28,0xFE, /// ...
              };
            memcpy(data,resp,sizeof(resp));
            flag=true;
            break;
            }
          case 0xC0:
          case 0xD6:
            dataLen=0x12;
            data[0]=record;
            data[1]=0x10;
            memset(data+2,0,16);
            strcpy((char *)data+2,"Dum Issuer");
            break;
          }
        break;
      case 0xa4b8:
        readcmd=true;
        if(mem[2]==0) {
          dataLen=0x0C;
          memset(data,0,0x0c);
          data[0]=0xDF;
          data[1]=0x0A;
          data[4]=0xDF;
          data[5]=0x88;
          }
        else {
          dataLen=0;
          nextSW[0]=0x94; nextSW[1]=0x02;
          }
        break;
      case 0xa44c:
        nextSW[0]=0x9f; nextSW[1]=flag ? 0x42 : 0x1c;
        break;
      case 0xa4c0:
        readcmd=true;
        if(mem[4]==0x1c) {
          data[0]=0xdb;
          data[1]=0x10;
          memset(&data[2],0x45,16);
          data[18]=0xdf;
          data[19]=0x08;
          data[20]=0x50;
          data[23]=0x80;
          }
        else if(mem[4]==0x11) {
          memset(data,0,0x11);
          data[0]=0xdf;
          data[1]=0x0f;
          data[6]=0x3f;
          data[7]=0x6a;
          }
        else if(mem[4]==0x42) {
          static const unsigned char resp[0x42] = {
            0xdf,0x40,
            0x6d,0xbb,0x81, // ...
            };
          memcpy(data,resp,sizeof(resp));
          }
        break;
      }
    if(readcmd) {
      if(dataLen>0) {
        PUSHB(mem[INS_IDX]); // ACK byte
        PUSH(data,dataLen);
        }
      PUSH(nextSW,2);
      cmdStart=true;
      }
    else {
      PUSHB(mem[INS_IDX]); // ACK byte
      }
    }
  else {
    dataLen-=len;
    if(dataLen<=0 && !cmdStart) {
      PUSH(nextSW,2);
      cmdStart=true;
      }
    }
#elif defined(EMU_IRD)
  static const struct Resp {
    unsigned int cmd;
    unsigned char data[MAX_LEN];
    } resp[] = {
#ifdef EMU_IRD_384
  { 0x01020203,
    {0x01,0x02,0x00,0x00,0x02,0x00,0x00,0x10,0x03,0x84,0x00,0x00,0x00,0x17,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x47,0x45,0x52} },
  { 0x01020E02,
    {0x01,0x02,0x00,0x00,0x0e,0x02,0x00,0x40,0xa9,0x6d,0x73,0x97,0x9e,0xfc,0x9b,0x8e,0x5b,0x8c,0xfa,0xb2,0x0c,0xb2,0x57,0x0f,0xb2,0xf7,0x29,0x4e,0xa2,0xcb,0xbd,0x5b,0x52,0x74,0xb1,0x2a,0xb7,0xc5,0xd9,0x62,0x6d,0x37,0x6d,0x9d,0xa3,0xe9,0x61,0xbb,0x1b,0x2b,0x56,0xb7,0x86,0x0c,0xe6,0xb1,0x07,0x6f,0xe0,0xf8,0x8a,0xd3,0x05,0x83,0xf6,0x53,0x0e,0xd2,0x72,0xd5,0xc1,0x50} },
  { 0x01020E03,
    {0x01,0x02,0x00,0x00,0x0e,0x03,0x00,0x40,0xb6,0xde,0xa8,0xce,0x86,0x1c,0x42,0x72,0xa8,0x16,0x4b,0xf9,0xce,0x33,0xb5,0x43,0xd0,0x50,0xe6,0xa7,0xf1,0xcb,0x55,0x25,0x97,0x13,0xee,0x62,0x98,0xe7,0x17,0x50,0xeb,0x3b,0x59,0x10,0x0a,0xb6,0x2e,0x93,0x61,0x71,0x3c,0xe6,0xe2,0x2c,0x1e,0x7d,0xbd,0x6a,0x49,0xbb,0x04,0x5b,0xdf,0x2f,0xb7,0xa7,0x93,0xe0,0x3d,0x40,0x4e,0xd1} },
#else
  { 0x01020203,
    {0x01,0x02,0x00,0x00,0x02,0x00,0x00,0x10,0x03,0x83,0x00,0x00,0x00,0x17,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x47,0x45,0x52} },
  { 0x01020E02,
    {0x01,0x02,0x00,0x00,0x0E,0x02,0x00,0x40,0x65,0xEF,0xD0,0xA7,0xF3,0x80,0x48,0x0E,0xC1,0x4B,0x41,0x6C,0xDB,0x68,0x47,0xE4,0x23,0xAD,0x96,0x12,0x07,0x58,0x58,0x29,0xE9,0x14,0x27,0x7F,0x7D,0xF8,0xC2,0x65,0x76,0x4D,0x75,0x04,0x7C,0x9B,0xAA,0x99,0x58,0xEA,0xE2,0x43,0xB5,0x03,0x05,0xD6,0x62,0x99,0xF5,0x18,0x16,0x4E,0xCF,0x49,0x11,0xBD,0xF3,0xEE,0xC3,0xCD,0x90,0x3B} },
  { 0x01020E03,
    {0x01,0x02,0x00,0x00,0x0E,0x03,0x00,0x40,0x9B,0x06,0xB5,0x0A,0x98,0xC6,0x2E,0x1D,0x71,0xA1,0xE8,0x84,0xAE,0x98,0x57,0xE9,0xE6,0xC2,0x97,0x46,0x25,0x7A,0x2B,0xA1,0xD5,0x33,0x18,0xDE,0x16,0xC1,0xAB,0x22,0x2C,0xC2,0x11,0x24,0x81,0x11,0xA8,0x39,0xE3,0xB1,0xDB,0x33,0x1A,0x93,0x31,0xB0,0x61,0xD8,0xDE,0x92,0x1F,0x29,0x20,0xD0,0x9E,0x0F,0x6A,0xF0,0x7C,0xBA,0xCD,0xCC} },
#endif
  { 0x01020003,
    {0x01,0x02,0x00,0x00,0x00,0x03,0x00,0x14,0x37,0x30,0x31,0x32,0x30,0x36,0x39,0x33,0x34,0x37,0x32,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00} },
  { 0x01020103,
    {0x01,0x02,0x00,0x00,0x01,0x00,0x00,0x10,0x00,0x17,0x00,0x00,0x01,0x00,0x17,0x00,0x00,0x01,0x04,0x00,0xF3,0x86,0x01,0x1A} },
  { 0x01021100,
    {0x01,0x02,0x58,0x00,0x11,0x00,0x00,0x00} },
  { 0x01020903,
    {0x01,0x02,0x55,0x00,0x09,0x03,0x00,0x00} },
  { 0x01020303,
    {0x01,0x02,0x00,0x00,0x03,0x03,0x02,0x18,0x0F,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x31,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00} },
  { 0x01020400,
    {0x01,0x02,0x54,0x00,0x04,0x00,0x00,0x00} },
  { 0x01050000,
    {0x01,0x05,0x9D,0x00,0x38,0x00,0x02,0x16,0x00,0x01,0x00,0x13,0xFF,0xFF,0x22,0x88,0xBF,0x02,0x70,0xFA,0x5F,0x80,0xFD,0x1E,0xD4,0xD6,0xF0,0xF1,0x81,0xB3} },
  { 0x01010000,
    {0x01,0x01,0x00,0x00,0x00,0x00,0x00,0x03,0x00,0x00,0xFF} },
  { 0x00000000, // don't delete this one
    {} }
  };
  const unsigned int cmd=(mem[0]<<24) + (mem[1]<<16) + (mem[2]<<8) + mem[3];
  const struct Resp *r=&resp[0];
  while(1) {
    if(!r->cmd) {
      static const unsigned char fail[] = { 0x01,0x99,0x6f };
      PUSH(fail,sizeof(fail));
      break;
      }
    if(cmd==r->cmd) {
      const int len=r->data[7]+9-1;
      int cs=0x3f;
      for(int i=0 ; i<len ; i++) cs ^= r->data[i];
      PUSH(r->data,len);
      PUSHB(cs);
      break;
      }
    r++;
    }
#else
#error No serial emulation mode selected
#endif
  return len;
}

void cSerial::ToggleRTS(void)
{
  rts=!rts;
  if(!rts) { 
#if defined(EMU_SECA)
    static const unsigned char atr[] = { 0x3b,0x77,0x12,0x00,0x00,
                                         0x60,0x60,0x03,0x0E,0x6C,0xB6,0xD6 };
#elif defined(EMU_IRD)
    static const unsigned char atr[] = { 0x3B,0x9F,0x21,0x0E,
#ifdef EMU_IRD_384
                                         0x49,0x52,0x44,0x45,0x54,0x4f,0x20,0x41,0x43,0x53,0x03,0x84,0x55,0xff,0x80,0x6d };
#else
                                         0x49,0x52,0x44,0x45,0x54,0x4F,0x20,0x41,0x43,0x53,0x03,0x83,0x95,0x00,0x80,0x55 };
#endif
#elif defined(EMU_CRYPTO)
    static const unsigned char atr[] = { 0x3B,0x78,0x12,0x00,0x00,
                                         0x65,0xC4,0x05,0x05,0x8F,0xF1,0x90,0x00 };
#else
#error No serial emulation mode selected
#endif
    PUSH(atr,sizeof(atr));
    PRINTF(L_CORE_SERIAL,"serial: toggle RTS, now off");
    }
  else { PRINTF(L_CORE_SERIAL,"serial: toggle RTS, now on"); }
}

bool cSerial::CheckCAR(void)
{
#if 0
  if(time(0)>=remTime+30) {
    dsr=!dsr;
    remTime=time(0);
    }
#endif
  return dsr;
}

#endif //SER_EMU

// -- cInfoStr -----------------------------------------------------------------

cInfoStr::cInfoStr(void)
:cLineBuff(256)
{
  current=0;
}

cInfoStr::~cInfoStr()
{
  free(current);
}

bool cInfoStr::Get(char *buff, int len)
{
  cMutexLock lock(&mutex);
  if(current && current[0]) {
    strn0cpy(buff,current,len);
    return true;
    }
  return false;
}

void cInfoStr::Begin(void)
{
  Flush();
}

void cInfoStr::Finish()
{
  cMutexLock lock(&mutex);
  free(current);
  current=Grab();
}

// -- cSmartCard ---------------------------------------------------------------

static const int Ftable[16] = {
  372,372,558,744,1116,1488,1860,0,0,512,768,1024,1536,2048,0,0
  };

static const float Dtable[16] = {
  0.0,1.0,2.0,4.0,8.0,16.0,0.0,0.0,
  0.0,0.0,0.5,0.25,0.125,0.0625,0.03125,0.015625
  };
  
cSmartCard::cSmartCard(const struct CardConfig *Cfg, const struct StatusMsg *Msg)
{
  cfg=Cfg; msg=Msg;
  ser=0; atr=0; idStr[0]=0; cardUp=false; needsReset=true;
}

bool cSmartCard::GetCardIdStr(char *str, int len)
{
  strn0cpy(str,idStr,len);
  return (str[0]!=0);
}

bool cSmartCard::GetCardInfoStr(char *str, int len)
{
  return infoStr.Get(str,len);
}

bool cSmartCard::Setup(cSerial *Ser, const struct Atr *Atr, int Id)
{
  ser=Ser; atr=Atr; id=Id;
  if(cfg->SerMode==(ser->CurrentMode()&SM_MASK) && Init()) {
    cardUp=true; needsReset=false;
    return true;
    }
  return false;
}

#define NEED(x) { \
                if(len+(x)>Atr->atrLen) { \
                    LBPUT("SHORT ATR"); \
                    return false; \
                    } \
                }

bool cSmartCard::ParseAtr(struct Atr *Atr, int id, int clock)
{
  // default values
  Atr->histLen=0; Atr->T=0; Atr->F=372; Atr->D=1.0; Atr->N=0; Atr->WI=10;
  Atr->BWI=4; Atr->CWI=0; Atr->Tspec=-1;

  const unsigned char *atr=Atr->atr;
  if(atr[0]==0x03) {
    PRINTF(L_CORE_SC,"%d: indirect convention detected",id);
    cSmartCard::Invert(Atr->atr,Atr->atrLen);
    Atr->convention=SM_INDIRECT;
    }
  else if(atr[0]==0x3B) {
    PRINTF(L_CORE_SC,"%d: direct convention detected",id);
    Atr->convention=SM_DIRECT;
    }
  else {
    PRINTF(L_CORE_SC,"%d: byte mode not supported 0x%02x",id,atr[0]);
    return false;
    }

  // TS TO
  Atr->histLen=atr[1]&0x0F;
  int Y=atr[1]&0xF0, i=1, len=2;
  LBSTARTF(L_CORE_SC);
  LBPUT("%d: atr decoding TS=%02x hist=%d Y%d=%02x ",id,atr[0],Atr->histLen,i,Y);
  do {
    if(Y&0x10) { // TAi
      NEED(1);
      LBPUT("TA%d=%02x ",i,atr[len]);
      if(i==1) {
        Atr->TA1=atr[len];
        Atr->F=Ftable[(atr[len]>>4)&0x0F];
        Atr->D=Dtable[ atr[len]    &0x0F];
        LBPUT("F=%d D=%f ",Atr->F,Atr->D);
        }
      else if(i==2) {
        Atr->Tspec=atr[len]&0x0F;
        LBPUT("Tspec=%d ",Atr->Tspec);
        }
      else if(i==3) {
        LBPUT("IFSC=%d ",atr[len]);
        }
      len++;
      }
    if(Y&0x20) { // TBi
      NEED(1);
      LBPUT("TB%d=%02x ",i,atr[len]);
      if(i==3) {
        Atr->BWI=(atr[len]>>4)&0x0F;
        Atr->CWI=atr[len]&0x0F;
        LBPUT("BWI=%d CWI=%d ",Atr->BWI,Atr->CWI);
        }
      len++;
      }
    if(Y&0x40) { // TCi
      NEED(1);
      LBPUT("TC%d=%02x ",i,atr[len]);
      if(i==1) {
        Atr->N=atr[len];
        LBPUT("N=%d ",Atr->N);
        }
      else if(i==2) {
        Atr->WI=atr[len];
        LBPUT("WI=%d ",Atr->WI);
        }
      else if(i==3) {
        LBPUT("CHK=%s ",atr[len]&1 ? "CRC16":"LRC");
        }
      len++;
      }
    if(Y&0x80) { // TDi
      NEED(1);
      LBPUT("TD%d=%02x ",i,atr[len]);
      if(i==1) {
        Atr->T=atr[len]&0x0F;
        LBPUT("T=%d ",Atr->T);
        }
      else {
        LBPUT("Tn=%d ",atr[len]&0x0F);
        }
      Y=atr[len]&0xF0;
      len++;
      }
    else Y=0;
    i++;
    LBPUT("Y%d=%02x ",i,Y);
    } while(Y);
  NEED(Atr->histLen);
  LBEND();

  LBSTART(L_CORE_SC);
  LBPUT("%d: historical:",id);
  for(int i=0 ; i<Atr->histLen ; i++) LBPUT(" %02x",atr[len+i]);
  LBFLUSH();
  LBPUT("%d: historical: '",id);
  for(int i=0 ; i<Atr->histLen ; i++) LBPUT("%c",isprint(atr[len+i]) ? atr[len+i] : '.');
  LBEND();

  memcpy(Atr->hist,&atr[len],Atr->histLen);
  len+=Atr->histLen;

  // TCK
  if(Atr->T!=0 && len+1<=Atr->atrLen) {
    len++;
    unsigned char cs=XorSum(atr+1,len-1);
    // according to the ISO the initial TS byte isn't checksumed, but
    // some cards do so. In this case the checksum is equal TS.
    if(cs==0) PRINTF(L_CORE_SC,"%d: atr checksum ok",id);
    else if(cs==atr[0]) PRINTF(L_CORE_SC,"iso: %d: atr checksum is TS (not ISO compliant)",id);
    else {
      PRINTF(L_CORE_SC,"%d: atr checksum FAILED (cs:%02x)",id,cs);
      return false;
      }
    }
  else PRINTF(L_CORE_SC,"%d: atr checksum not given/not required",id);

  if(Atr->atrLen>len) PRINTF(L_CORE_SC,"%d: long atr read=%d len=%d",id,Atr->atrLen,len);
  Atr->wwt=960*Atr->WI*Atr->F/(clock/1000);
  Atr->bwt=(int)(960*(float)(1<<Atr->BWI)*372/(clock/1000));
  PRINTF(L_CORE_SC,"%d: indicated wwt(T0)=%d ms, bwt(T1)=%d ms (at %.4f MHz)",id,Atr->wwt,Atr->bwt,(float)clock/1000000);
  return true;
}

void cSmartCard::Invert(unsigned char *data, int n)
{
  static const unsigned char swaptab[] =  { 15,7,11,3,13,5,9,1,14,6,10,2,12,4,8,0 };
  for(int i=n-1; i>=0; i--)
    data[i]=(swaptab[data[i]&0x0f]<<4) | swaptab[data[i]>>4];
}

int cSmartCard::SerRead(unsigned char *data, int len, int to)
{
  if(atr->T==1 || atr->T==14) {
    int r=Read(data,len,to);
    if(r<0) Test(false);
    return r;
    }
  else {
    PRINTF(L_CORE_SC,"%d: SerRead() not allowed for ISO compliant cards (T=%d)",id,atr->T);
    return -1;
    }
}

int cSmartCard::SerWrite(const unsigned char *data, int len)
{
  if(atr->T==1 || atr->T==14) {
    int r=Write(data,len);
    if(r<0) Test(false);
    return r;
    }
  else {
    PRINTF(L_CORE_SC,"%d: SerWrite() not allowed for ISO compliant cards (T=%d)",id,atr->T);
    return -1;
    }
}

int cSmartCard::Read(unsigned char *data, int len, int to)
{
  int r=ser->Read(data,len,cfg->serTO,to);
  if(atr->convention==SM_INDIRECT && r>0) Invert(data,r);
  return r;
}

int cSmartCard::Write(const unsigned char *data, int len)
{
  unsigned char *tmp=AUTOMEM(len);
  if(atr->convention==SM_INDIRECT) {
    memcpy(tmp,data,len);
    Invert(tmp,len);
    data=tmp;
    }
  int r=ser->Write(data,len);
  if(r>0) {
    unsigned char *buff=AUTOMEM(r);
    int rr=ser->Read(buff,r,cfg->serTO);
    if(rr<0) r=rr;
    }
  return r;
}

int cSmartCard::Procedure(unsigned char ins, int restLen)
{
  int r;
  unsigned char buff;
  LBSTARTF(L_CORE_SC);
  LBPUT("%d: <- PROC: ",id);
  do {
    do {
      if(Read(&buff,1,cfg->workTO)<=0) return -1;
      LBPUT("%02x ",buff);
      } while(buff==0x60);

    if((buff&0xF0)==0x60 || (buff&0xF0)==0x90) { // SW1/SW2
      sb[0]=buff;
      if(Read(&buff,1)<=0) return -1;
      LBPUT("%02x",buff);
      sb[1]=buff;
      return 0;
      }
    else {
      if((buff&0xFE)==(ins&0xFE)) r=restLen;
      else if((~buff&0xFE)==(ins&0xFE)) r=1;
      else {
        LBPUT("cannot handle procedure %02x (ins=%02x)",buff,ins);
        return -1;
        }
      if(r>restLen) {
        LBPUT("data overrun r=%d restLen=%d",r,restLen);
        return -1;
        }
      }
    } while(r==0);
  LBEND();
  return r;
}

bool cSmartCard::IsoRead(const unsigned char *cmd, unsigned char *data)
{
  if(atr->T==0) { // only for ISO complaint cards
    LDUMP(L_CORE_SC,cmd,CMD_LEN,"%d: -> INS:",id);
    if(Write(cmd,CMD_LEN)<0) return Test(false);
    int tr=cmd[LEN_IDX] ? cmd[LEN_IDX] : 256;
    int len=0;
    while(1) {
      int r=Procedure(cmd[INS_IDX],tr-len);
      if(r<=0) return Test(r==0);
      if(Read(data+len,r)<0) return Test(false);
      LDUMP(L_CORE_SC,data+len,r,"%d: <- DATA:",id);
      len+=r;
      }
    }
  else PRINTF(L_CORE_SC,"%d: can't IsoRead() from incompatible card (T=%d)",id,atr->T);
  return false;
}

bool cSmartCard::IsoWrite(const unsigned char *cmd, const unsigned char *data)
{
  if(atr->T==0) { // only for ISO complaint cards
    LDUMP(L_CORE_SC,cmd,CMD_LEN,"%d: -> INS:",id);
    if(Write(cmd,CMD_LEN)<0) return Test(false);
    int len=0;
    while(1) {
      int r=Procedure(cmd[INS_IDX],cmd[LEN_IDX]-len);
      if(r<=0) return Test(r==0);
      if(Write(data+len,r)<0) return Test(false);
      LDUMP(L_CORE_SC,data+len,r,"%d: -> DATA:",id);
      len+=r;
      }
    }
  else PRINTF(L_CORE_SC,"%d: can't IsoWrite() to incompatible card (T=%d)",id,atr->T);
  return false;
}

bool cSmartCard::Test(bool res)
{
 if(!res) {
   TriggerReset();
   PRINTF(L_CORE_SC,"%d: reset triggered",id);
   }
 return res;
}

bool cSmartCard::Status(void)
{
  const struct StatusMsg *m=msg;
  while(m->sb[0]!=0xFF) {
    if(sb[0]==m->sb[0] && (m->sb[1]==0xFF || sb[1]==m->sb[1])) {
      if(!m->retval) PRINTF(L_CORE_SC,"%d: %s (status: %02x %02x)",id,m->message,sb[0],sb[1]);
      return m->retval;
      }
    m++;
    }
  PRINTF(L_CORE_SC,"%d: unknown (status: %02x %02x)",id,sb[0],sb[1]);
  return false;
}

int cSmartCard::CheckSctLen(const unsigned char *data, int off)
{
  int l=SCT_LEN(data);
  if(l+off > MAX_LEN) {
    PRINTF(L_CORE_SC,"section too long %d > %d",l,MAX_LEN-off);
    l=-1;
    }
  return l;
}

// -- cSmartCardData -----------------------------------------------------------

cSmartCardData::cSmartCardData(int Ident)
{
  ident=Ident;
}

// -- cSmartCardLink -----------------------------------------------------------

cSmartCardLink::cSmartCardLink(const char *Name, int Id)
{
  name=Name; id=Id;
  cSmartCards::Register(this);
}

// -- cSmartCards --------------------------------------------------------------

cSmartCardLink *cSmartCards::first=0;

cSmartCards smartcards;

static const char *serModes[] = { 0,"8e2","8o2","8n2" };

cSmartCards::cSmartCards(void)
:cThread("SmartcardWatcher")
{
  for(int i=0 ; i<MAX_PORTS ; i++) ports[i].Serial=0;
  firstRun=true;
}

void cSmartCards::Register(cSmartCardLink *scl)
{
  PRINTF(L_CORE_DYN,"registering %s smartcard (id %x)",scl->name,scl->id);
  scl->next=first;
  first=scl;
}

void cSmartCards::LaunchWatcher(void)
{
  for(int i=0 ; i<MAX_PORTS ; i++)
    if(ports[i].Serial) { Start(); return; }
  firstRun=false;
  PRINTF(L_GEN_WARN,"no smartcard interface defined!");
}

void cSmartCards::Shutdown(void)
{
  Cancel(3);
  mutex.Lock();
  for(int i=0 ; i<MAX_PORTS ; i++) {
    if(ports[i].Serial) {
      delete ports[i].Card;
      delete ports[i].Serial;
      ports[i].Serial=0;
      }
    }
  dataList.Clear();
  mutex.Unlock();
}

bool cSmartCards::AddPort(const char *devName, bool invCD, bool invRST, int clock)
{
  cMutexLock lock(&mutex);
  for(int i=0 ; i<MAX_PORTS ; i++) {
    if(!ports[i].Serial) {
      cSerial *ser=new cSerial(devName,invCD,invRST);
      if(ser && ser->Open()) {
        ports[i].Card=0;
        ports[i].CardId=0;
        ports[i].UseCount=0;
        ports[i].Dead=false;
        ports[i].PortNum=i;
        ports[i].Serial=ser;
        ports[i].Clock=clock ? clock : ISO_FREQ;
        PRINTF(L_CORE_LOAD,"smartcards: added serial port %s as port %d (%s CD, %s RESET, CLOCK %d)",
                  devName,i,invCD?"inverse":"normal",invRST?"inverse":"normal",ports[i].Clock);
        return true;
        }
      PRINTF(L_GEN_ERROR,"failed to open serial port %s",devName);
      delete ser;
      return false;
      }
    else if(!strcmp(ports[i].Serial->DeviceName(),devName)) {
      PRINTF(L_GEN_ERROR,"duplicate serial port %s. Check your config!",devName);
      return false;
      }
    }
  PRINTF(L_GEN_ERROR,"only %d serial ports supported",MAX_PORTS);
  return false;
}

void cSmartCards::LoadData(const char *cfgdir)
{
  mutex.Lock();
  dataList.Clear();
  ConfRead("smartcard data",AddDirectory(cfgdir,DATAFILE),true);
  mutex.Unlock();
}

bool cSmartCards::ParseLine(const char *line, bool fromCache)
{
  char *r=index(line,':');
  if(!r) return false;
  for(cSmartCardLink *scl=first; scl; scl=scl->next) {
    if(!strncasecmp(scl->name,line,r-line)) {
      cSmartCardData *scd=scl->CreateData();
      if(scd && scd->Parse(r+1)) {
        dataList.Add(scd);
        break;
        }
      else {
        delete scd;
        return false;
        }
      }
    }
  return true;
}

cSmartCardData *cSmartCards::FindCardData(cSmartCardData *param)
{
  cMutexLock lock(&mutex);
  for(cSmartCardData *cd=dataList.First(); cd; cd=dataList.Next(cd))
    if(cd->ident==param->ident && cd->Matches(param))
      return cd;
  return 0;
}

bool cSmartCards::HaveCard(int id)
{
  cMutexLock lock(&mutex);
  while(Running() && firstRun) cond.Wait(mutex);
  for(int i=0 ; i<MAX_PORTS ; i++)
    if(ports[i].Serial && ports[i].CardId==id) return true;
  return false;
}

cSmartCard *cSmartCards::LockCard(int id)
{
  mutex.Lock();
  while(Running() && firstRun) cond.Wait(mutex);
  for(int i=0 ; i<MAX_PORTS ; i++) {
    if(ports[i].Serial && ports[i].CardId==id) {
      ports[i].UseCount++;
      mutex.Unlock();
      cSmartCard *sc=ports[i].Card;
      sc->Lock();
      if(CardInserted(ports[i].Serial) && sc->CardUp() && !sc->NeedsReset())
        return sc;
      // if failed, unlock the card and decrement UseCount
      sc->Unlock();
      mutex.Lock();
      ports[i].UseCount--;
      cond.Broadcast();
      break;
      }
    }
  mutex.Unlock();
  return 0;
}

void cSmartCards::ReleaseCard(cSmartCard *sc)
{
  sc->Unlock();
  mutex.Lock();
  for(int i=0 ; i<MAX_PORTS ; i++) {
    if(ports[i].Card==sc) {
      ports[i].UseCount--;
      cond.Broadcast();
      mutex.Unlock();
      return;
      }
    }
  mutex.Unlock();
  PRINTF(L_GEN_DEBUG,"internal: can't find port in cSmartCards::ReleaseCard()");
}

bool cSmartCards::CardInserted(cSerial *ser)
{
  return ser->CheckCAR();
}

bool cSmartCards::CardReset(struct Port *port)
{
  if(Reset(port)<2 || !cSmartCard::ParseAtr(&port->Atr,port->PortNum,port->Clock))
    return false;
  if(!DoPTS(port)) {
    // reset card again and continue without PTS
    if(Reset(port)<2 || !cSmartCard::ParseAtr(&port->Atr,port->PortNum,port->Clock))
      return false;
    }
  return true;
}

int cSmartCards::Reset(struct Port *port)
{
  cSerial *ser=port->Serial;
  PRINTF(L_CORE_SC,"%d: reseting card (sermode %s)",port->PortNum,serModes[ser->CurrentMode()]);
  ser->ToggleRTS();
  cCondWait::SleepMs(20);
  ser->ToggleRTS();
  int r=ser->Read(port->Atr.atr,-MAX_ATR_LEN,800,2000);
  port->Atr.atrLen=r;
  if(r>0) LDUMP(L_CORE_SC,port->Atr.atr,r,"%d: <- ATR len=%d:",port->PortNum,r);
  return r;
}

bool cSmartCards::DoPTS(struct Port *port)
{
  const struct Atr *atr=&port->Atr;
  if(atr->F!=372 || atr->D!=1.0) { // PTS required
    cSerial *ser=port->Serial;
    const int id=port->PortNum;
    int baud=(int)((float)port->Clock*atr->D/atr->F);
    PRINTF(L_CORE_SC,"%d: PTS cycle: calculated baudrate %d",id,baud);

    if(atr->Tspec<0) {
      PRINTF(L_CORE_SC,"%d: negotiable mode",id);
#ifdef NO_PTS_PROTO
      PRINTF(L_CORE_SC,"%d: PTS disabled at compile time!!!",id);
      return true;
#else
      unsigned char req[4], conf[16];
      req[0]=0xFF;
      req[1]=0x10 | atr->T;
      req[2]=atr->TA1;
      req[3]=XorSum(req,3);
      LDUMP(L_CORE_SC,req,4,"%d: PTS request:",id);
      if(ser->Write(req,4)!=4) {
        PRINTF(L_CORE_SC,"%d: PTS request, write failed",id);
        return false;
        }
      if(ser->Read(conf,4,100,100)!=4) {
        PRINTF(L_CORE_SC,"%d: PTS request, echo readback failed",id);
        return false;
        }
      int n=ser->Read(conf,-16,200,1000);
      if(n>0) LDUMP(L_CORE_SC,conf,n,"%d: PTS confirm:",id);
      if(n<1 || conf[0]!=0xFF || XorSum(conf,n)!=0) {
        PRINTF(L_CORE_SC,"%d: PTS confirm, bad format",id);
        return false;
        }
      if(n<4 || conf[1]!=req[1] || conf[2]!=req[2]) {
        PRINTF(L_CORE_SC,"%d: PTS not confirmed",id);
        return false;
        }
#endif // NO_PTS_PROTO
      }
    else
      PRINTF(L_CORE_SC,"%d: specific mode Tspec=%d",id,atr->Tspec);

    int mode=ser->CurrentMode();
    if(atr->N==255) mode|=SM_1SB;
    if(!ser->SetMode(mode,baud)) {
      PRINTF(L_CORE_SC,"%d: setting baudrate %d failed",id,baud);
      return false;
      }
    }
 return true;
}

void cSmartCards::SetPort(struct Port *port, cSmartCard *sc, int id, bool dead)
{
  mutex.Lock();
  while(port->UseCount) cond.Wait(mutex);
  delete port->Card;
  port->Card=sc;
  port->CardId=id;
  port->Dead=dead;
  port->UseCount=0;
  mutex.Unlock();
}

void cSmartCards::Action(void)
{
  while(Running()) {
    for(int i=0 ; i<MAX_PORTS ;) {
      struct Port *port=&ports[i];
      cSerial *ser=port->Serial;
      if(ser) {
        if(port->Card) {
          cSmartCard *sc=port->Card;
          sc->Lock();
          if(!CardInserted(ser)) {
            sc->Unlock(); sc=0;
            PRINTF(L_CORE_SC,"%d: card removed (UseCount=%d)",port->PortNum,port->UseCount);
            SetPort(port,0,0,false);
            }
          else if(sc->NeedsReset()) {
            PRINTF(L_CORE_SC,"%d: card reset requested",port->PortNum);
            if(!ser->SetMode(ser->CurrentMode()&SM_MASK)
                || !CardReset(port)
                || !sc->Setup(ser,&port->Atr,port->PortNum)) {
              sc->Unlock(); sc=0;
              PRINTF(L_CORE_SC,"%d: card re-init failed",port->PortNum);
              SetPort(port,0,0,true);
              }
            }
          if(sc) sc->Unlock();
          }
        else if(CardInserted(ser)) {
          if(!port->Dead) {
            PRINTF(L_CORE_SC,"%d: new card inserted",port->PortNum);
            for(int mode=SM_NONE+1 ; mode<SM_MAX ; mode++) {
              if(ser->SetMode(mode)) {
                if(CardReset(port)) {
                  cSmartCardLink *scl=first;
                  while(scl) {
                    PRINTF(L_CORE_SC,"%d: checking for %s card",port->PortNum,scl->name);
                    cSmartCard *sc=scl->Create();
                    if(sc && sc->Setup(ser,&port->Atr,port->PortNum)) {
                      SetPort(port,sc,scl->id,false);
                      goto next; // ugly, any better solution?
                      }
                    delete sc;
                    scl=scl->next;
                    }
                  PRINTF(L_CORE_SC,"%d: no card handler found",port->PortNum);
                  }
                else PRINTF(L_CORE_SC,"%d: reset/atr error",port->PortNum);
                }
              else PRINTF(L_CORE_SC,"%d: failed to set serial mode %s",port->PortNum,serModes[mode]);
              }
            port->Dead=true;
            PRINTF(L_CORE_SC,"%d: can't initialise new card, ignoring port until card reinserted",port->PortNum);
            }
          }
        else {
          if(port->Dead) PRINTF(L_CORE_SC,"%d: card removed, port reactivated",port->PortNum);
          port->Dead=false;
          }
        }
next: i++;
      }
    if(firstRun) {
      mutex.Lock();
      cond.Broadcast();
      firstRun=false;
      mutex.Unlock();
      }
    cCondWait::SleepMs(300);
    }  
}

bool cSmartCards::ListCard(int num, char *str, int len)
{
  if(num>=MAX_PORTS || !ports[num].Serial) return false;
  str[0]=0;
  mutex.Lock();
  cSmartCard *sc=ports[num].Card;
  if(sc) {
    if(!sc->GetCardIdStr(str,len)) {
      cSmartCardLink *scl=first;
      while(scl) {
        if(scl->id==ports[num].CardId) {
          strn0cpy(str,scl->name,len);
          break;
          }
        scl=scl->next;
        }
      }
    }
  mutex.Unlock();
  return true;
}

bool cSmartCards::CardInfo(int num, char *str, int len)
{
  bool res=false;
  if(num<MAX_PORTS && ports[num].Serial) {
    str[0]=0;
    mutex.Lock();
    cSmartCard *sc=ports[num].Card;
    if(sc) res=sc->GetCardInfoStr(str,len);
    mutex.Unlock();
    }
  return res;
}

void cSmartCards::CardReset(int num)
{
  if(num<MAX_PORTS && ports[num].Serial) {
    mutex.Lock();
    cSmartCard *sc=ports[num].Card;
    if(sc) sc->TriggerReset();
    mutex.Unlock();
    }
}
