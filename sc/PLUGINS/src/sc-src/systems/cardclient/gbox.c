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
 *
 * This code is based on morfsta's version but has been rewritten nearly
 * from scratch.
 * 
 * To get it working ensure that GBOX is running with no F: { 01 } line,
 * and no pmt.tmp file in /var/tmp.
 */ 

#include <stdio.h>

#include <vdr/pat.h>

#include "cc.h" 
#include "parse.h"

// -- cGboxClient ----------------------------------------------------------- 
 
class cCardClientGbox : public cCardClient { 
private: 
  int GetMsg(int cmd, unsigned char *buff, int len);
public: 
  cCardClientGbox(const char *Name); 
  virtual bool Init(const char *CfgDir); 
  virtual bool Login(void);
  virtual bool ProcessECM(const cEcmInfo *ecm, const unsigned char *data, unsigned char *Cw);
  }; 
 
static cCardClientLinkReg<cCardClientGbox> __ncd("gbox");

cCardClientGbox::cCardClientGbox(const char *Name)
:cCardClient(Name)
{
  so.SetRWTimeout(2*1000);
  so.SetUDP(true);
}

bool cCardClientGbox::Init(const char *config) 
{ 
  cMutexLock lock(this); 
  Logout();
  int num=0;
  if(!ParseStdConfig(config,&num)) return false;
  return true;
} 
 
bool cCardClientGbox::Login(void) 
{ 
  Logout();
  if(!so.Bind("127.0.0.1",8003)) return false;
  return true;
} 

int cCardClientGbox::GetMsg(int cmd, unsigned char *buff, int len)
{
  int n;
  do {
    n=so.Read(buff,-len);
    } while(n>0 && buff[0]!=cmd);
  return n;
}

bool cCardClientGbox::ProcessECM(const cEcmInfo *ecm, const unsigned char *data, unsigned char *cw)
{
  cMutexLock lock(this);
  if((!so.Connected() && !Login()) || !CanHandle(ecm->caId)) return false;
  so.Flush();

  const int caid=ecm->caId;
  const int pid =ecm->ecm_pid;
  int n;
  const unsigned char *descr=ecm->GetCaDescr(&n);
  if(!descr) {
    PRINTF(L_CC_GBOX,"no CA descriptor for caid %04x sid %d prov %04x",caid,ecm->prgId,ecm->provId);
    return false;
    }

  static const unsigned char pmt[] = {
    0x87,
    0x02,0xb0,0x25, 	// tid, sct len
    0x17,0x74, 		// sid
    0xc3,0x00,0x00, 	// vers, sctnr
    0xff,0xec, 		// pcr pid
    0xf0,0x00,		// prg info len
    0x02, 0xff,0xec, 0xf0,0x00
    };
  unsigned char buff[512];
  if(sizeof(pmt)+n+8>sizeof(buff)) {
    PRINTF(L_CC_GBOX,"CA descriptor buffer overflow %d",sizeof(pmt)+n+8);
    return false;
    }
  memcpy(buff,pmt,sizeof(pmt));
  buff[4]=ecm->prgId >> 8;
  buff[5]=ecm->prgId & 0xFF;
  memcpy(&buff[sizeof(pmt)],descr,n);
  buff[16]=0xF0 | ((n>>8)&0x0F);
  buff[17]=n & 0xFF;
  SetSctLen(&buff[1],sizeof(pmt)-4+n+4);
  buff[2]=(buff[2]&0x0F)|0xB0;

  if(so.SendTo("127.0.0.1",8004,buff,sizeof(pmt)+n+4)<0) {
    PRINTF(L_CC_GBOX,"failed to send PMT data. GBOX running?");
    return false;
    }

  if((n=GetMsg(0x8a,buff,sizeof(buff)))<0) {
    PRINTF(L_CC_GBOX,"failed to get ECM port. GBOX running?");
    return false;
    }
  int pidnum=-1;
  if(n>=2) {
    for(int i=0 ; i<buff[1]; i++) {
      if(WORD(buff,2+i*2,0x1FFF)==pid) {
        pidnum=i;
        break;
        }
      }
    }
  if(pidnum<0) {
    PRINTF(L_CC_ECM,"%s: unable to decode for CAID %04X/PID %04X",name,caid,pid);
    return false;
    }

  n=SCT_LEN(data);
  if(n>=256) {
    PRINTF(L_CC_ECM,"%s: ECM section too long %d > 255",name,n);
    return false;
    }
  buff[0]=0x88;
  buff[1]=(pid>>8)&0x1F;
  buff[2]=pid & 0xFF;
  buff[3]=n;
  memcpy(&buff[4],data,n);
  n+=4;
  if(so.SendTo("127.0.0.1",8005+pidnum,buff,n)<0) {
    PRINTF(L_CC_GBOX,"failed to send ECM data. GBOX running?");
    return false;
    }

  if(GetMsg(0x89,buff,sizeof(buff))<0) {
    PRINTF(L_CC_GBOX,"failed to get CW. GBOX running?");
    return false;
    }
  if(n<17) {
    PRINTF(L_CC_GBOX,"bad CW answer from GBOX");
    return false;
    }
  memcpy(cw,&buff[1],16);
  return true;
}
