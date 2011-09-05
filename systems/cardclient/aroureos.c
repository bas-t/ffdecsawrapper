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

// -- cCardClientAroureos ------------------------------------------------------

class cCardClientAroureos : public cCardClient, protected cIdSet {
private:
  bool ParseCardConfig(const char *config, int *num);
public:
  cCardClientAroureos(const char *Name);
  virtual bool Init(const char *config);
  virtual bool Login(void);
  virtual bool ProcessECM(const cEcmInfo *ecm, const unsigned char *source, unsigned char *cw);
  virtual bool ProcessEMM(int caSys, const unsigned char *source);
  };

static cCardClientLinkReg<cCardClientAroureos> __aroureos("Aroureos");

cCardClientAroureos::cCardClientAroureos(const char *Name)
:cCardClient(Name)
{
  so.SetRWTimeout(5*1000);
}

bool cCardClientAroureos::ParseCardConfig(const char *config, int *num)
{
  int hb, hs;
  int startNum=*num;
  if(sscanf(&config[*num],":%x:%x%n",&hb,&hs,num)==2) {
    *num+=startNum;
    unsigned char h[3];
    h[0]=(hs>>16)&0xFF;
    h[1]=(hs>> 8)&0xFF;
    h[2]=(hs>> 0)&0xFF;
    PRINTF(L_CC_LOGIN,"%s: hexser %02X%02X%02X hexbase %02X",name,h[0],h[1],h[2],hb);
    ResetIdSet();
    SetCard(new cCardIrdeto(hb,&h[0]));
    }
  return true;
}

bool cCardClientAroureos::Init(const char *config)
{
  cMutexLock lock(this);
  Logout();
  int num=0;
  return ParseStdConfig(config,&num) &&
         ParseCardConfig(config,&num);
}

bool cCardClientAroureos::Login(void)
{
  Logout();
  if(!so.Connect(hostname,port)) return false;
  PRINTF(L_CC_LOGIN,"%s: connected to %s:%d",name,hostname,port);
  if(!emmAllowed) PRINTF(L_CC_EMM,"%s: EMM disabled from config",name);
  return true;  
}

bool cCardClientAroureos::ProcessEMM(int caSys, const unsigned char *source)
{
  if(emmAllowed) {
    cMutexLock lock(this);
    if(MatchEMM(source)) {
      const int length=SCT_LEN(source);
      int id=msEMM.Get(source,length,0);
      if(id>0 || emmAllowed>1) {
        unsigned char *buff=AUTOMEM(length+8);
        memcpy(buff,"EMM",3);
        memcpy(&buff[3],source,length);
        SendMsg(buff,length+3);
        msEMM.Cache(id,true,0);
        }
      return true;
      }
    }
  return false;
}

bool cCardClientAroureos::ProcessECM(const cEcmInfo *ecm, const unsigned char *source, unsigned char *cw)
{
  cMutexLock lock(this);
  so.Flush();
  const int len=SCT_LEN(source);
  if(len<=93) {
    unsigned char buff[128];
    memcpy(buff,"ECM",3);
    memcpy(&buff[3],source,len);

    if(!SendMsg(buff,96)) return false;
    if(RecvMsg(buff,16)>0 && !CheckNull(buff,16)) {
      memcpy(cw,buff,16);
      return true;
      }
    }
  return false;
}
