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
#include <byteswap.h>

#include "cc.h"
#include "crypto.h"
#include "misc.h"
#include "parse.h"

#include <openssl/md5.h>

#define CCVERSION "3.37"
#define CCTIMEOUT 5000 // ms

// -- cCardClientCommon --------------------------------------------------------

class cCardClientCommon : public cCardClient, public cAES, protected cIdSet {
private:
  bool conReply, logReply, doAES;
  bool exclusive;
  int minMsgLen;
  cCondVar sleepCond;
  cTimeMs time;
protected:
  bool emmProcessing;
  char username[11], password[11];
  int emmReqLen;
  //
  bool ParseKeyConfig(const char *config, int *num);
  bool ParseUserConfig(const char *config, int *num);
  virtual bool SendMsg(const unsigned char *data, int len);
  virtual int RecvMsg(unsigned char *data, int len, int to=-1);
  virtual void HandleEMMRequest(const unsigned char *buff, int len) {}
  virtual bool CanHandleEMM(int SysId) { return false; }
public:
  cCardClientCommon(const char *Name, bool ConReply, bool LogReply, bool DoAES, int MinMsgLen);
  virtual bool Init(const char *config);
  virtual bool Login(void);
  virtual bool Immediate(void);
  virtual bool ProcessECM(const cEcmInfo *ecm, const unsigned char *source, unsigned char *cw);
  virtual bool ProcessEMM(int caSys, const unsigned char *source);
  };

cCardClientCommon::cCardClientCommon(const char *Name, bool ConReply, bool LogReply, bool DoAES, int MinMsgLen)
:cCardClient(Name)
{
  conReply=ConReply; logReply=LogReply; doAES=DoAES; minMsgLen=MinMsgLen;
  emmProcessing=exclusive=false;
  so.SetRWTimeout(CCTIMEOUT);
  emmReqLen=-64;
}

bool cCardClientCommon::ParseUserConfig(const char *config, int *num)
{
  int startNum=*num;
  if(sscanf(&config[*num],":%10[^:]:%10[^:]%n",username,password,num)==2) {
    *num+=startNum;
    PRINTF(L_CC_CORE,"%s: username=%s password=%s",name,username,password);;
    return true;
    }
  return false;
}

bool cCardClientCommon::ParseKeyConfig(const char *config, int *num)
{
  char hexkey[33];
  int startNum=*num;
  if(sscanf(&config[*num],":%32[^:]%n",hexkey,num)==1) {
    *num+=startNum;
    PRINTF(L_CC_CORE,"%s: key=%s",name,hexkey);
    unsigned char binkey[16];
    memset(binkey,0,sizeof(binkey));
    const char *line=hexkey;
    int n=GetHex(line,binkey,sizeof(binkey),false);
    if(n!=(int)sizeof(binkey))
      PRINTF(L_CC_CAMD,"warning AES key not %d bytes long",(int)sizeof(binkey));
    LDUMP(L_CC_CAMD,binkey,16,"AES activated key =");
    SetKey(binkey);
    }
  return true;
}

bool cCardClientCommon::SendMsg(const unsigned char *data, int len)
{
  unsigned char *buff2=AUTOMEM(minMsgLen);
  if(len<minMsgLen) {
    memcpy(buff2,data,len);
    memset(buff2+len,0,minMsgLen-len);
    data=buff2; len=minMsgLen;
    }
  unsigned char *buff=AUTOMEM(len+16);
  const int l=Encrypt(data,len,buff);
  if(l>0) { data=buff; len=l; }
  return cCardClient::SendMsg(data,len);
}

int cCardClientCommon::RecvMsg(unsigned char *data, int len, int to)
{
  int n=cCardClient::RecvMsg(data,len,to);
  if(n>0) {
    if(n&15) PRINTF(L_CC_CAMD,"AES crypted message length not a multiple of 16");
    Decrypt(data,n);
    }
  return n;
}

bool cCardClientCommon::Immediate(void)
{
  return emmAllowed && logReply && cCardClient::Immediate();
}

bool cCardClientCommon::Init(const char *config)
{
  cMutexLock lock(this);
  Logout();
  int num=0;
  if(ParseStdConfig(config,&num) &&
     ParseUserConfig(config,&num) &&
     (!doAES || ParseKeyConfig(config,&num))) {
    return true;
    }
  return false;
}

bool cCardClientCommon::Login(void)
{
  Logout();
  if(!so.Connect(hostname,port)) return false;
  PRINTF(L_CC_LOGIN,"%s: connected to %s:%d (%s)",name,hostname,port,name);
  emmProcessing=false;

  unsigned char buff[128];
  if(conReply) {
    if(RecvMsg(buff,16)<0) {
      PRINTF(L_CC_CAMD,"bad connect reply");
      return false;
      }
    }

  memset(buff,0,32);
  int user_len=strlen(username)+1;
  memcpy(buff+1,username,user_len);
  int pass_len=strlen(password)+1;
  memcpy(buff+1+user_len+1,password,pass_len);
  int vers_len=strlen(CCVERSION)+1;
  memcpy(buff+1+user_len+pass_len+1,CCVERSION,vers_len);
  PRINTF(L_CC_CAMD,"login user='%s' password=hidden version=%s",username,CCVERSION);
  if(!SendMsg(buff,32)) return false;

  if(emmAllowed && logReply) {
    PRINTF(L_CC_CAMD,"waiting for login reply ...");
    int r=RecvMsg(buff,emmReqLen);
    if(r>0) HandleEMMRequest(buff,r);
    }
  PRINTF(L_CC_LOGIN,"%s: login done",name);
  return true;
}

bool cCardClientCommon::ProcessEMM(int caSys, const unsigned char *source)
{
  if(emmAllowed && CanHandleEMM(caSys)) {
    cMutexLock lock(this);
    if(MatchEMM(source)) {
      const int length=SCT_LEN(source);
      int id=msEMM.Get(source,length,0);
      if(id>0 || emmAllowed>1) {
        unsigned char *buff=AUTOMEM(length+32);
        buff[0]=0x03;
        buff[1]=(caSys>>8);
        buff[2]=(caSys&0xFF);
        memcpy(buff+3,((cCardIrdeto *)card)->hexSer,3);
        buff[6]=((cCardIrdeto *)card)->hexBase;
        memcpy(&buff[7],source,length);
        //PRINTF(L_CC_CAMD,"%s: sending EMM for caid 0x%04X",name,caSys);
        SendMsg(buff,length+7);
        msEMM.Cache(id,true,0);
        }
      return true;
      }
    }
  return false;
}

bool cCardClientCommon::ProcessECM(const cEcmInfo *ecm, const unsigned char *source, unsigned char *cw)
{
  Lock();
  bool res=false;
  while(exclusive) sleepCond.Wait(*this);
  if((so.Connected() || Login()) && (!emmProcessing || CanHandle(ecm->caId))) {
    const int length=SCT_LEN(source);
    unsigned char *buff=AUTOMEM(length+32);
    int n;
    while((n=RecvMsg(buff,-16,0))>0) HandleEMMRequest(buff,n);
    buff[0]=0x02;
    buff[1]=(ecm->caId>>8);
    buff[2]=(ecm->caId&0xFF);
    memset(&buff[3],0,4);
    memcpy(&buff[7],source,length);
    if(SendMsg(buff,length+7)) {
      exclusive=true;
      time.Set(CCTIMEOUT);
      do {
        sleepCond.TimedWait(*this,50);
        while((n=RecvMsg(buff,-32,0))>0) {
          if(n>=21 && buff[0]==2) {
            if(!CheckNull(buff+5,16)) {
              if(!res) {
                memcpy(cw,buff+5,16);
                res=true;
                }
              else PRINTF(L_CC_CAMD,"unexpected CW packet");
              }
            else {
              PRINTF(L_CC_ECM,"%s: server is unable to handle ECM",name);
              n=-1;
              break;
              }
            }
          else HandleEMMRequest(buff,n);
          }
        } while(!res && n>=0 && !time.TimedOut());
      if(!res && time.TimedOut()) PRINTF(L_CC_ECM,"%s: CW request timed out",name);
      exclusive=false;
      sleepCond.Broadcast();
      }
    }
  Unlock();
  return res;
}

// -- cCardClientCamd33 --------------------------------------------------------

#define EMMREQLEN33 13

class cCardClientCamd33 : public cCardClientCommon {
private:
  int CAID;
  unsigned char lastEmmReq[32];
protected:
  virtual void HandleEMMRequest(const unsigned char *buff, int len);
  virtual bool CanHandleEMM(int SysId);
public:
  cCardClientCamd33(const char *Name);
  virtual bool CanHandle(unsigned short SysId);
  };

static cCardClientLinkReg<cCardClientCamd33> __camd33("Camd33");

cCardClientCamd33::cCardClientCamd33(const char *Name)
:cCardClientCommon(Name,true,true,true,0)
{
  CAID=0;
  memset(lastEmmReq,0,sizeof(lastEmmReq));
  emmReqLen=EMMREQLEN33;
}

bool cCardClientCamd33::CanHandle(unsigned short SysId)
{
  return CanHandleEMM(SysId) || cCardClient::CanHandle(SysId);
}

bool cCardClientCamd33::CanHandleEMM(int SysId)
{
  return (emmProcessing && SysId==CAID);
}

void cCardClientCamd33::HandleEMMRequest(const unsigned char *buff, int len)
{
  if(len>=emmReqLen && buff[0]==0 && !CheckNull(buff,len) && memcmp(buff,lastEmmReq,emmReqLen)) {
    emmProcessing=false;
    int c=buff[1]*256+buff[2];
    if(c!=CAID) CaidsChanged();
    CAID=c;
    ResetIdSet();
    switch(CAID>>8) {
      case 0x17:
      case 0x06:
        SetCard(new cCardIrdeto(buff[6],&buff[3]));
        AddProv(new cProviderIrdeto(0,&buff[7]));
        AddProv(new cProviderIrdeto(2,&buff[10]));
        memcpy(lastEmmReq,buff,13);
        PRINTF(L_CC_LOGIN,"%s: CAID: %04x HexSerial: %02X%02X%02X, HexBase: %02X",name,CAID,buff[3],buff[4],buff[5],buff[6]);
        PRINTF(L_CC_LOGIN,"%s: Provider00: %02X%02X%02X, Provider10: %02X%02X%02X",name,buff[7],buff[8],buff[9],buff[10],buff[11],buff[12]);
        if(!emmAllowed) PRINTF(L_CC_EMM,"%s: EMM disabled from config",name);
        emmProcessing=true;
        break;
      }
    }
}

// -- cCardClientCardd ---------------------------------------------------------

class cCardClientCardd : public cCardClientCommon {
public:
  cCardClientCardd(const char *Name);
  };

static cCardClientLinkReg<cCardClientCardd> __cardd("Cardd");

cCardClientCardd::cCardClientCardd(const char *Name)
:cCardClientCommon(Name,false,true,false,96)
{}

// -- cCardClientBuffy ---------------------------------------------------------

#define MAX_CAIDS 16

class cCardClientBuffy : public cCardClientCommon {
private:
  unsigned short CAIDs[MAX_CAIDS], numCAIDs;
public:
  cCardClientBuffy(const char *Name);
  virtual bool Init(const char *config);
  virtual bool Login(void);
  virtual bool CanHandle(unsigned short SysId);
  };

static cCardClientLinkReg<cCardClientBuffy> __buffy("Buffy");

cCardClientBuffy::cCardClientBuffy(const char *Name)
:cCardClientCommon(Name,true,false,true,0)
{}

bool cCardClientBuffy::Init(const char *config)
{
  cMutexLock lock(this);
  return cCardClientCommon::Init(config);
}

bool cCardClientBuffy::CanHandle(unsigned short SysId)
{
  cMutexLock lock(this);
  for(int i=0; i<numCAIDs; i++) if(CAIDs[i]==SysId) return true;
  return false;
}

bool cCardClientBuffy::Login(void)
{
  cMutexLock lock(this);
  if(!cCardClientCommon::Login()) return false;

  unsigned char buff[128];
  memset(buff,0,sizeof(buff));
  buff[0]=0x0A;
  if(!SendMsg(buff,32)) return false;
  int n=RecvMsg(buff,-sizeof(buff));
  if(n<0) return false;
  for(int i=1; i<n && numCAIDs<MAX_CAIDS; i+=2) {
    unsigned short caid=(buff[i+1]<<8)+buff[i];
    if(caid==0xFFFF) break;
    if(caid) CAIDs[numCAIDs++]=caid;
    }
  CaidsChanged();
  LBSTART(L_CC_LOGIN);
  LBPUT("%s: CAIDs ",name);
  for(int i=0; i<numCAIDs && CAIDs[i]; i++) LBPUT("%04X ",CAIDs[i]);
  LBEND();
  return true;
}

// -- cCardClientCamd35 --------------------------------------------------------

struct CmdBlock {
  unsigned int ucrc;
  struct {
    unsigned char cmd;    // 0
    unsigned char len;    // 1
    unsigned char pad[2]; // 2  XXXX
    unsigned int crc;     // 4
    } udp_header;
  struct {
    unsigned short srvID; // 8
    unsigned short casID; // 10
    unsigned int prvID;   // 12
    unsigned short pinID; // 16
    unsigned char pad2[2];// 18 XXXX
    } service;
  unsigned char data[0];  // 20
  };

#define CBSIZE(cb) (sizeof((cb)->udp_header)+sizeof((cb)->service))
#define UCSIZE(cb) (sizeof((cb)->ucrc))
#define HDSIZE(cb) (CBSIZE(cb)+UCSIZE(cb))

struct EmmReq02 {
  unsigned short caid;
  unsigned char hex[4];
  unsigned char D0, D2, D3;
  };

struct EmmReq05 {
  unsigned short caids[8]; // 0
  int caidCount;           // 16
  unsigned char ua[6];     // 20
  unsigned short provMap;  // 26
  struct {                 // 28
    unsigned char prov[2];
    unsigned char sa[3];
    } provInfo[16];
  unsigned char D0, D2, D3; // 108
  };

class cCardClientCamd35 : public cCardClient, public cAES, private cIdSet {
private:
  unsigned int ucrc;
  unsigned short pinid;
  bool exclusive, emmCmd06;
  cCondVar sleepCond;
  cTimeMs time;
protected:
  bool emmProcessing;
  char username[33], password[33];
  int numCaids, Caids[8];
  unsigned char Dx[8];
  int lastEmmReq;
  //
  bool ParseUserConfig(const char *config, int *num);
  bool SendBlock(struct CmdBlock *cb, int datalen);
  int RecvBlock(struct CmdBlock *cb, int maxlen, int to);
  void HandleEMMRequest(const struct CmdBlock *cb);
  bool CanHandleEMM(unsigned short SysId);
public:
  cCardClientCamd35(const char *Name);
  virtual bool Init(const char *config);
  virtual bool Login(void);
  virtual bool ProcessECM(const cEcmInfo *ecm, const unsigned char *data, unsigned char *cw);
  virtual bool ProcessEMM(int caSys, const unsigned char *data);
  };

static cCardClientLinkReg<cCardClientCamd35> __camd35("Camd35");

cCardClientCamd35::cCardClientCamd35(const char *Name)
:cCardClient(Name)
{
  emmProcessing=exclusive=emmCmd06=false; pinid=0; numCaids=0; lastEmmReq=0;
  memset(Dx,0,sizeof(Dx));
  so.SetRWTimeout(CCTIMEOUT);
  so.SetUDP(true);
}

bool cCardClientCamd35::ParseUserConfig(const char *config, int *num)
{
  int startNum=*num;
  if(sscanf(&config[*num],":%32[^:]:%32[^:]%n",username,password,num)==2) {
    *num+=startNum;
    unsigned char md[16];
    ucrc=bswap_32(crc32_le(0,MD5((unsigned char *)username,strlen(username),md),16));
    PRINTF(L_CC_CORE,"%s: username=%s password=%s ucrc=%08x",name,username,password,ucrc);;
    SetKey(MD5((unsigned char *)password,strlen(password),md));
    return true;
    }
  return false;
}

bool cCardClientCamd35::SendBlock(struct CmdBlock *cb, int datalen)
{
  cb->udp_header.len=datalen;
  cb->udp_header.crc=bswap_32(crc32_le(0,&cb->data[0],datalen));
  datalen+=CBSIZE(cb);
  unsigned char *buff=AUTOMEM(datalen+UCSIZE(cb)+16);
  *((unsigned int *)buff)=ucrc;
  HEXDUMP(L_CC_CAMDEXTR,cb,datalen+UCSIZE(cb),"send:");
  const int l=Encrypt((unsigned char *)cb+UCSIZE(cb),datalen,buff+UCSIZE(cb));
  if(l<=0) return false;
  return cCardClient::SendMsg(buff,l+UCSIZE(cb));
}

int cCardClientCamd35::RecvBlock(struct CmdBlock *cb, int maxlen, int to)
{
  unsigned char *m=(unsigned char *)cb;
  int n=cCardClient::RecvMsg(m,-maxlen,to);
  if(n<=0) return n;
  if((unsigned int)n>=HDSIZE(cb)) {
    if(cb->ucrc==ucrc) {
      Decrypt(m+UCSIZE(cb),n-UCSIZE(cb));
      if((unsigned int)n<cb->udp_header.len+HDSIZE(cb))
        PRINTF(L_CC_CAMD35,"packet length doesn't match data length");
      else if(cb->udp_header.crc!=bswap_32(crc32_le(0,&cb->data[0],cb->udp_header.len)))
        PRINTF(L_CC_CAMD35,"data crc failed");
      else {
        HEXDUMP(L_CC_CAMDEXTR,cb,n,"recv:");
        return n;
        }
      }
    else PRINTF(L_CC_CAMD35,"wrong ucrc: got %08x, want %08x",cb->ucrc,ucrc);
    }
  else PRINTF(L_CC_CAMD35,"short packet received");
  return -1;
}

bool cCardClientCamd35::Init(const char *config)
{
  cMutexLock lock(this);
  Logout();
  int num=0;
  return (ParseStdConfig(config,&num) && ParseUserConfig(config,&num));
}

bool cCardClientCamd35::Login(void)
{
  Logout();
  if(!so.Connect(hostname,port)) return false;
  PRINTF(L_CC_LOGIN,"%s: connected to %s:%d (%s)",name,hostname,port,name);
  emmProcessing=false; lastEmmReq=0;
  PRINTF(L_CC_LOGIN,"%s: login done",name);
  return true;
}

void cCardClientCamd35::HandleEMMRequest(const struct CmdBlock *cb)
{
  int c=crc32_le(0,cb->data,cb->udp_header.len);
  if(c!=lastEmmReq) {
    lastEmmReq=c;
    ResetIdSet();
    if(cb->udp_header.cmd==0x02 && cb->udp_header.len>=9) {
      struct EmmReq02 *req=(struct EmmReq02 *)cb->data;
      numCaids=1;
      Caids[0]=bswap_16(req->caid);
      SetCard(new cCardIrdeto(req->hex[3],&req->hex[0]));
      Dx[0]=req->D0;
      Dx[2]=req->D2;
      Dx[3]=req->D3;
      char str[20];
      PRINTF(L_CC_LOGIN,"%s: CAID: %04x HexSerial: %s, HexBase: %02X (D0-%d D2-%d D3-%d)\n",
             name,Caids[0],HexStr(str,((cCardIrdeto *)card)->hexSer,3),((cCardIrdeto *)card)->hexBase,
             Dx[0],Dx[2],Dx[3]);
      if(!emmProcessing || emmCmd06) PRINTF(L_CC_CAMD35,"got cmd 02, doing cmd02 EMM");
      emmCmd06=false;
      }
    else if(cb->udp_header.cmd==0x05 && cb->udp_header.len>=111) {
      struct EmmReq05 *req=(struct EmmReq05 *)cb->data;
      numCaids=bswap_32(req->caidCount);
      for(int i=numCaids-1; i>=0; i--) Caids[i]=bswap_16(req->caids[i]);
      LBSTARTF(L_CC_LOGIN);
      LBPUT("%s: CAIDS:",name);
      for(int i=numCaids-1; i>=0; i--) LBPUT(" %04x",Caids[i]);
      char str[20], str2[20];
      LBPUT(" ua=%s",HexStr(str,req->ua,6));
      switch(Caids[0]>>8) {
        case 0x17:
        case 0x06: SetCard(new cCardIrdeto(req->ua[3],&req->ua[0])); break;
        case 0x01: SetCard(new cCardSeca(&req->ua[0])); break;
        case 0x0d: SetCard(new cCardCryptoworks(&req->ua[0])); break;
        case 0x05: SetCard(new cCardViaccess(&req->ua[0])); break;
        case 0x18: SetCard(new cCardNagra2(&req->ua[2])); break;
        default:
          LBPUT(" (unhandled)");
          break;
        }
      LBPUT(" providers");
      int map=bswap_16(req->provMap);
//      for(int i=0; i<15; i++) //XXX not sure what actualy is the correct
//        if(map & (1<<i)) {    //XXX interpretation of provMap
      for(int i=0; i<map && i<15; i++) {
        LBPUT(" %d:%s/%s",i,HexStr(str,req->provInfo[i].prov,2),HexStr(str2,req->provInfo[i].sa,3));
        if(!CheckNull(req->provInfo[i].sa,3) && !CheckFF(req->provInfo[i].sa,3)) {
          switch(Caids[0]>>8) {
            case 0x17:
            case 0x06: AddProv(new cProviderIrdeto(req->provInfo[i].prov[0],req->provInfo[i].sa)); break;
            case 0x01: AddProv(new cProviderSeca(req->provInfo[i].prov,req->provInfo[i].sa)); break;
            default:
              LBPUT(" <unhandled>");
              break;
            }
          }
        }
      Dx[0]=req->D0;
      Dx[2]=req->D2;
      Dx[3]=req->D3;
      LBPUT(" (D0-%d D2-%d D3-%d)",Dx[0],Dx[2],Dx[3]);
      LBEND();

      if(!emmProcessing || !emmCmd06) PRINTF(L_CC_CAMD35,"got cmd 05, doing cmd06 EMM");
      emmCmd06=true;
      }
    else return;

    if(!emmAllowed) PRINTF(L_CC_EMM,"%s: EMM disabled from config",name);
    emmProcessing=true;
    CaidsChanged();
    }
}

bool cCardClientCamd35::CanHandleEMM(unsigned short SysId)
{
  for(int i=numCaids-1; i>=0; i--) if(SysId==Caids[i]) return true;
  return false;
}

bool cCardClientCamd35::ProcessEMM(int caSys, const unsigned char *data)
{
  if(emmProcessing && emmAllowed) {
    cMutexLock lock(this);
    PRINTF(L_CC_CAMDEXTR,"got EMM caSys=%.4x",caSys);
    if(CanHandleEMM(caSys)) {
      LDUMP(L_CC_CAMDEXTR,&data[0],10,"EMM starts with");
      int upd;
      cProvider *p;
      cAssembleData ad(data);
      if(MatchAndAssemble(&ad,&upd,&p)) {
        PRINTF(L_CC_CAMDEXTR,"EMM matched upd=%d provId=%.4llx",upd,p ? p->ProvId() : 0);
        while((data=ad.Assembled())) {
          LDUMP(L_CC_CAMDEXTR,&data[0],10,"processing assembled EMM");
          if(Dx[upd]) {
            unsigned char buff[300];
            memset(buff,0xff,sizeof(buff));
            struct CmdBlock *cb=(struct CmdBlock *)buff;
            cb->udp_header.cmd=emmCmd06 ? 0x06 : 0x02;
            cb->service.srvID=0;
            cb->service.casID=bswap_16(caSys);
            cb->service.prvID=bswap_32(p ? p->ProvId() : 0);
            cb->service.pinID=pinid++;
            int len=SCT_LEN(data), off=0;
            switch(caSys>>8) {
              case 0x0d:
                {
                static const unsigned char head[] = { 0x8F,0x70,0x00,0xA4,0x42,0x00,0x00,0x00 };
                int c, n;
                switch(data[0]) {
                  case 0x82: c=0x42; n=10; break;
                  case 0x84: c=0x48; n=9; break;
                  case 0x88:
                  case 0x89: c=0x44; n=5; break;
                  default:   continue;
                  }
                if(len<n) continue;
                off=sizeof(head);
                memcpy(&cb->data[0],head,off);
                cb->data[3+1]=c;
                cb->data[3+4]=len-n;
                SetSctLen(&cb->data[0],len-n+5);
                data+=n; len-=n;
                break;
                }
              }
            if(len+off<=255) {
              memcpy(&cb->data[off],data,len);
              int id=msEMM.Get(&cb->data[0],len+off,0);
              if(id>0 || emmAllowed>1) {
                SendBlock(cb,len+off);
                msEMM.Cache(id,true,0);
                }
              else PRINTF(L_CC_CAMDEXTR,"not send, already in cache");
              }
            else PRINTF(L_CC_EMM,"%s: EMM length %d > 255, not supported by UDP protocol",name,len+off);
            }
          else PRINTF(L_CC_CAMDEXTR,"dropped, updtype %d blocked",upd);
          }
        return true;
        }
      else PRINTF(L_CC_CAMDEXTR,"dropped, doesn't match card data");
      }
    else PRINTF(L_CC_CAMDEXTR,"dropped, doesn't want caSys %.4x",caSys);
    }
  return false;
}

bool cCardClientCamd35::ProcessECM(const cEcmInfo *ecm, const unsigned char *data, unsigned char *cw)
{
  bool res=false;
  const int length=SCT_LEN(data);
  if(length<=255) {
    Lock();
    while(exclusive) sleepCond.Wait(*this);
    unsigned char buff[300];
    memset(buff,0xff,sizeof(buff));
    struct CmdBlock *cb=(struct CmdBlock *)buff;
    const unsigned short pid=pinid++;
    int n;
    while((n=RecvBlock(cb,sizeof(buff),0))>0) {
      if(cb->udp_header.cmd==0x01 || cb->udp_header.cmd==0x44)
        PRINTF(L_CC_CAMD35,"unexpected CW answer on flush");
      else
        HandleEMMRequest(cb);
      }
    cb->udp_header.cmd=0x00;
    cb->service.srvID=bswap_16(ecm->prgId);
    cb->service.casID=bswap_16(ecm->caId);
    cb->service.prvID=bswap_32(ecm->provId);
    cb->service.pinID=pid;
    memcpy(&cb->data[0],data,length);
    if(SendBlock(cb,length)) {
      exclusive=true;
      time.Set(CCTIMEOUT);
      do {
        sleepCond.TimedWait(*this,50);
        while((n=RecvBlock(cb,sizeof(buff),0))>0) {
          if(cb->udp_header.cmd==0x01) {
            if(cb->udp_header.len>=16 && cb->service.pinID==pid && !res) {
              if(!CheckNull(&cb->data[0],8)) memcpy(&cw[0],&cb->data[0],8);
              if(!CheckNull(&cb->data[8],8)) memcpy(&cw[8],&cb->data[8],8);
              res=true;
              }
            else PRINTF(L_CC_CAMD35,"unexpected/bad CW packet");
            }
          else if(cb->udp_header.cmd==0x44) {
            if(cb->service.pinID==pid) {
              PRINTF(L_CC_ECM,"%s: server is unable to handle ECM",name);
              n=-1;
              break;
              }
            }
          else HandleEMMRequest(cb);
          }
        } while(!res && n>=0 && !time.TimedOut());
      if(!res && time.TimedOut()) PRINTF(L_CC_ECM,"%s: CW request timed out",name);
      exclusive=false;
      sleepCond.Broadcast();
      }
    Unlock();
    }
  else PRINTF(L_CC_ECM,"%s: ECM length %d > 255, not supported by UDP protocol",name,length);
  return res;
}
