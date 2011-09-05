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
#include <string.h>

#include "cc.h"
#include "parse.h"
#include "version.h"

// -- cCardClientRadegast ------------------------------------------------------

#define MAXCAIDS 16

class cCardClientRadegast : public cCardClient, private cIdSet {
private:
  bool emmProcessing;
  int caids[MAXCAIDS], numCaids;
  //
  void InitVars(void);
  void SetLength(unsigned char *buff, int len);
  int GetLength(const unsigned char *buff);
  int GetNanoStart(const unsigned char *buff);
  int GetMsgLength(const unsigned char *buff);
  int GetMaxLength(const unsigned char *buff);
  void StartMsg(unsigned char *buff, int cmd);
  bool CheckLength(const unsigned char *buff, int len);
  void AddNano(unsigned char *buff, int nano, int len, int value);
  void AddNano(unsigned char *buff, int nano, int len, const unsigned char *data);
  bool Send(const unsigned char *buff);
  int Recv(unsigned char *buff, int len);
public:
  cCardClientRadegast(const char *Name);
  virtual bool Init(const char *config);
  virtual bool Login(void);
  virtual bool CanHandle(unsigned short SysId);
  virtual bool ProcessECM(const cEcmInfo *ecm, const unsigned char *source, unsigned char *cw);
  virtual bool ProcessEMM(int caSys, const unsigned char *data);
  };

static cCardClientLinkReg<cCardClientRadegast> __rdg("Radegast");

cCardClientRadegast::cCardClientRadegast(const char *Name)
:cCardClient(Name)
{
  InitVars();
  so.SetRWTimeout(7*1000);
}

void cCardClientRadegast::InitVars(void)
{
  emmProcessing=false; numCaids=0;
}

bool cCardClientRadegast::CanHandle(unsigned short SysId)
{
  cMutexLock lock(this);
  if(numCaids<=0) return cCardClient::CanHandle(SysId);
  for(int i=0; i<numCaids; i++) if(caids[i]==SysId) return true;
  return false;
}

bool cCardClientRadegast::Init(const char *config)
{
  cMutexLock lock(this);
  Logout();
  return ParseStdConfig(config);
}

void cCardClientRadegast::SetLength(unsigned char *buff, int len)
{
  if(buff[0]<=0x90) buff[1]=min(255,len);
  else { buff[1]=len>>8; buff[2]=len&0xFF; }
}

int cCardClientRadegast::GetLength(const unsigned char *buff)
{
  return (buff[0]<=0x90) ? buff[1] : ((buff[1]<<8)+buff[2]);
}

int cCardClientRadegast::GetNanoStart(const unsigned char *buff)
{
  return (buff[0]<=0x90) ? 2 : 3;
}

int cCardClientRadegast::GetMsgLength(const unsigned char *buff)
{
  return GetLength(buff)+GetNanoStart(buff);
}

int cCardClientRadegast::GetMaxLength(const unsigned char *buff)
{
  return (buff[0]<=0x90) ? 0xFF : 0xFFFF;
}

void cCardClientRadegast::StartMsg(unsigned char *buff, int cmd)
{
  buff[0]=cmd;
  SetLength(buff,0);
}

bool cCardClientRadegast::CheckLength(const unsigned char *buff, int len)
{
  int l=GetLength(buff)+len+2;
  int max=GetMaxLength(buff);
  if(len>255) PRINTF(L_CC_RDGD,"cmd %02x: nano too long %d > 255",buff[0],len);
  else if(l>max) PRINTF(L_CC_RDGD,"cmd %02x: msg too long %d > %d",buff[0],l,max);
  return l<=max;
}

void cCardClientRadegast::AddNano(unsigned char *buff, int nano, int len, int value)
{
  unsigned char hex[4];
  for(int i=0; i<len; i++) hex[i]=value >> ((len-i-1)*8);
  AddNano(buff,nano,len,hex);
}

void cCardClientRadegast::AddNano(unsigned char *buff, int nano, int len, const unsigned char *data)
{
  int pos=GetLength(buff), off=GetNanoStart(buff);
  if(pos<GetMaxLength(buff)) {
    buff[off+pos]=nano;
    buff[off+pos+1]=len;
    memcpy(&buff[off+pos+2],data,len);
    pos+=len+2;
    }
  SetLength(buff,pos);
}

bool cCardClientRadegast::Send(const unsigned char *buff)
{
  return SendMsg(buff,GetMsgLength(buff));
}

int cCardClientRadegast::Recv(unsigned char *buff, int len)
{
  if(RecvMsg(buff,1)<0) {
    PRINTF(L_CC_RDGD,"short read");
    return -1;
    }
  int n=GetNanoStart(buff);
  if(RecvMsg(buff+1,n-1,200)<0) {
    PRINTF(L_CC_RDGD,"short read(2)");
    return -1;
    }
  int k=GetMsgLength(buff);
  if(RecvMsg(buff+n,k-n,200)<0) {
    PRINTF(L_CC_RDGD,"short read(3)");
    return -1;
    }
  return k;
}

bool cCardClientRadegast::Login(void)
{
  Logout();
  if(!so.Connect(hostname,port)) return false;
  PRINTF(L_CC_LOGIN,"%s: connected to %s:%d",name,hostname,port);

  InitVars();
  unsigned char buff[512];
  char hello[32];
  snprintf(hello,sizeof(hello),"rdgd/vdr-sc-%s",ScVersion);
  StartMsg(buff,0x90);			// RDGD_MSG_CLIENT_HELLO
  AddNano(buff,1,strlen(hello),(unsigned char *)hello);	// RDGD_NANO_DESCR
  if(!Send(buff) || Recv(buff,sizeof(buff))<0) return false;
  if(buff[0]==0x91) {
    PRINTF(L_CC_RDGD,"got server hello, assuming V4 mode");
    StartMsg(buff,0x94);		// RDGD_MSG_CLIENT_CAP_REQ;
    int n;
    if(!Send(buff) || (n=Recv(buff,sizeof(buff)))<0) return false;
    if(buff[0]==0x95) {
      LBSTARTF(L_CC_LOGIN);
      LBPUT("radegast: got caps");
      int caid;
      for(int l=GetNanoStart(buff); l<n; l+=buff[l+1]+2) {
        switch(buff[l]) {
          case 0xE2:
            LBPUT(" VERS %s",(char *)&buff[l+2]);
            break;
          case 0xE4: // CAP_NANO_CAID
            if(numCaids>=MAXCAIDS) { l=n; break; } //stop processing
            caid=(buff[l+2]<<8)+buff[l+3];
            LBPUT(" CAID %04X",caid);
            caids[numCaids++]=caid;
            // XXX we should have EMM processing setup here, but as we don't
            // XXX get any ua/sa we cannot filter EMM anyways
            break;
          case 0xE5: // CAP_NANO_PROVID
            for(int i=0; i<buff[l+1]; i+=3)
              LBPUT("/%02X%02X%02X",buff[l+2+i],buff[l+2+i+1],buff[l+2+i+2]);
            break;
          }
        }
      LBEND();
      }
    }
  else PRINTF(L_CC_RDGD,"no server hello, assuming old mode");
  if(emmProcessing && !emmAllowed) 
    PRINTF(L_CC_EMM,"%s: EMM disabled from config",name);
  CaidsChanged();
  return true;
}

bool cCardClientRadegast::ProcessECM(const cEcmInfo *ecm, const unsigned char *source, unsigned char *cw)
{
  cMutexLock lock(this);
  if((!so.Connected() && !Login()) || !CanHandle(ecm->caId)) return false;
  so.Flush();
  int len=SCT_LEN(source);
  int keynr=-1;
  switch(ecm->caId>>8) {
    case 0x01: // Seca
      keynr=cParseSeca::KeyNr(source)&0x0F; break;
    case 0x05: // Viaccess
      keynr=cParseViaccess::KeyNr(source); break;
    case 0x18: // Nagra2
      if(ecm->caId>=0x1801) keynr=(source[7]&0x10) | 0x86;
      break;
    }
  unsigned char buff[512], tmp[10];
  StartMsg(buff,1);			// CMD_ECM_KEY_ASK
  AddNano(buff,2,1,ecm->caId>>8);       // ECM_NANO_CAID_INDEX (old)
  AddNano(buff,10,2,ecm->caId);		// ECM_NANO_CAID
  sprintf((char *)tmp,"%08X",ecm->provId);
  AddNano(buff,6,8,tmp);		// ECM_NANO_PROVIDER
  if(keynr>=0) {
    sprintf((char *)tmp,"%04X",keynr);
    AddNano(buff,7,4,tmp);		// ECM_NANO_KEYNO
    }
  if(!CheckLength(buff,len)) return false;
  AddNano(buff,3,len,source);		// ECM_NANO_PACKET

  if(!Send(buff) || (len=Recv(buff,sizeof(buff)))<0) return false;
  if(buff[0]==2) {
    for(int l=GetNanoStart(buff); l<len; l+=buff[l+1]+2) {
      switch(buff[l]) {
        case 0x05:
          if(buff[l+1]==16) {
            // check for zero cw, as someone may not send both cw's every time
            if(!CheckNull(buff+l+ 2,8)) memcpy(cw+0,buff+l+ 2,8);
            if(!CheckNull(buff+l+10,8)) memcpy(cw+8,buff+l+10,8);
            return true;
            }
          else PRINTF(L_CC_RDGD,"wrong cw length %d from server",buff[l+1]);
          break;
        case 0x04:
          PRINTF(L_CC_ECM,"%s: key not available from server",name);
          break;
        default:
          PRINTF(L_CC_RDGD,"unknown nano %02x in ECM response",buff[l]);
          break;
        }
      }
    }
  else PRINTF(L_CC_RDGD,"bad ECM response from server %02x != 02",buff[0]);
  return false;
}

bool cCardClientRadegast::ProcessEMM(int caSys, const unsigned char *data)
{
  if(emmProcessing && emmAllowed) {
    cMutexLock lock(this);
    int upd;
    cProvider *p;
    cAssembleData ad(data);
    if(MatchAndAssemble(&ad,&upd,&p)) {
      while((data=ad.Assembled())) {
        int len=SCT_LEN(data);
        int id=msEMM.Get(data,len,0);
        if(id>0 || emmAllowed>1) {
          unsigned char buff[512];
          StartMsg(buff,0x41);			//
          AddNano(buff,0x42,2,caSys);		// EMM_CAID
          if(p) {
            unsigned char tmp[10];
            sprintf((char *)tmp,"%08X",(int)p->ProvId());
            AddNano(buff,0x46,8,tmp);		// EMM_PROVID
            }
/*
          if(upd==2 || upd==3) {
            AddNano(buff,0x44,2,(unsigned char *)"aa");	// EMM_ADDR_VAL
            }
*/
          AddNano(buff,0x45,1,upd==3 ? 0x11 : (upd==2 ? 0x12 : 0x13)); // EMM_ADDR_TYPE
          if(CheckLength(buff,len)) {
            AddNano(buff,0x43,len,data);	// EMM_CA_DATA
            Send(buff);
            }
          msEMM.Cache(id,true,0);
          }
        }
      return true;
      }
    }
  return false;
}
