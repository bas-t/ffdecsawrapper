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

// initialy written by anetwolf anetwolf@hotmail.com
// based on crypto & protocol information from _silencer

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <openssl/sha.h>

#include "cc.h"
#include "network.h"

#define SHAREID(x) ((*(x+0)<<24) | (*(x+1)<<16) | (*(x+2)<<8) | *(x+3))

static const char *cccamstr="CCcam";

// -- cCCcamCrypt --------------------------------------------------------------

class cCCcamCrypt {
private:
  unsigned char keytable[256];
  unsigned char state, counter, sum;
  //
  static unsigned int ShiftRightAndFill(unsigned int value, unsigned int fill, unsigned int places);
  static void Swap(unsigned char *p1, unsigned char *p2);
public:
  void Init(const unsigned char *key, int length);
  void Decrypt(const unsigned char *in, unsigned char *out, int length);
  void Encrypt(const unsigned char *in, unsigned char *out, int length);
  static void ScrambleDcw(unsigned char *data, unsigned int length, const unsigned char *nodeid, unsigned int shareid);
  static bool DcwChecksum(const unsigned char *data);
  static bool CheckConnectChecksum(const unsigned char *data, int length);
  static void Xor(unsigned char *data, int length);
  };

void cCCcamCrypt::Init(const unsigned char *key, int length)
{
  for(int pos=0; pos<=255; pos++) keytable[pos]=pos;
  int result=0;
  unsigned char curr_char=0;
  for(int pos=0; pos<=255; pos++) {
    curr_char+=keytable[pos] + key[result];
    Swap(keytable+pos,keytable+curr_char);
    result=(result+1)%length;
    }
  state=key[0]; counter=0; sum=0;
}

void cCCcamCrypt::Decrypt(const unsigned char *in, unsigned char *out, int length)
{
  for(int pos=0; pos<length; pos++) {
    sum+=keytable[++counter];
    Swap(keytable+counter,keytable+sum);
    out[pos]=in[pos] ^ state ^ keytable[(keytable[sum]+keytable[counter])&0xFF];
    state^=out[pos];
    }
}

void cCCcamCrypt::Encrypt(const unsigned char *in, unsigned char *out, int length)
{
  for(int pos=0; pos<length; pos++) {
    sum+=keytable[++counter];
    Swap(keytable+counter,keytable+sum);
    out[pos]=in[pos] ^ state ^ keytable[(keytable[sum]+keytable[counter])&0xFF];
    state^=in[pos];
    }
}

void cCCcamCrypt::Swap(unsigned char *p1, unsigned char *p2)
{
  unsigned char tmp=*p1; *p1=*p2; *p2=tmp;
}

void cCCcamCrypt::ScrambleDcw(unsigned char *data, unsigned int length, const unsigned char *nodeid, unsigned int shareid)
{
  int s=0;
  int nodeid_high=(nodeid[0]<<24)|(nodeid[1]<<16)|(nodeid[2]<<8)|nodeid[3];
  int nodeid_low =(nodeid[4]<<24)|(nodeid[5]<<16)|(nodeid[6]<<8)|nodeid[7];
  for(unsigned int i=0; i<length; i++) {
    //cNible index, 0..4..8
    int nible_index=i+s;
    // Shift one nible to the right for high and low nodeid
    // Make sure the first shift is an signed one (sar on intel), else you get wrong results! 
    int high=nodeid_high>>nible_index;
    int low=ShiftRightAndFill(nodeid_low,nodeid_high,nible_index);
    // After 8 nibles or 32 bits use bits from high, based on signed flag it will be 0x00 or 0xFF 
    if(nible_index&32) low=high&0xFF;
    char final=*(data+i)^(low&0xFF);
    // Odd index inverts final
    if(i&0x01) final=~final;
    // Result
    *(data+i)=((shareid>>(2*(i&0xFF)))&0xFF)^final;
    s+=3;
    }
}

bool cCCcamCrypt::DcwChecksum(const unsigned char *data)
{
  return ((data[0]+data[1]+data[2])&0xff)==data[3] &&
         ((data[4]+data[5]+data[6])&0xff)==data[7] &&
         ((data[8]+data[9]+data[10])&0xff)==data[11] &&
         ((data[12]+data[13]+data[14])&0xff)==data[15];
}

bool cCCcamCrypt::CheckConnectChecksum(const unsigned char *data, int length)
{
  if(length==16) {
    bool valid=true;
    for(int i=0; i<4; i++)
      if(((data[i+0]+data[i+4]+data[i+8])&0xFF)!=data[i+12]) valid=false;
    return valid;
    }
  return false;
}

void cCCcamCrypt::Xor(unsigned char *data, int length)
{
  if(length>=16) {
    for(int index=0; index<8; index++) {
      *(data+8)=index * (*data);
      if(index<=5) *data^=cccamstr[index];
      data++;
      }
    }
}

unsigned int cCCcamCrypt::ShiftRightAndFill(unsigned int value, unsigned int fill, unsigned int places)
{
  return (value>>places) | ((((1<<places)-1)&fill)<<(32-places));
}

// -- cEcmShare ----------------------------------------------------------------

class cEcmShares;

class cEcmShare : public cSimpleItem {
friend class cEcmShares;
private:
  int source, transponder, pid;
  int shareid;
public:
  cEcmShare(const cEcmInfo *ecm, int id);
  };

cEcmShare::cEcmShare(const cEcmInfo *ecm, int id)
{
  pid=ecm->ecm_pid;
  source=ecm->source;
  transponder=ecm->transponder;
  shareid=id;
}

// -- cEcmShares ---------------------------------------------------------------

class cEcmShares : public cSimpleList<cEcmShare> {
private:
  cEcmShare *Find(const cEcmInfo *ecm);
public:
  int FindShare(const cEcmInfo *ecm);
  void AddShare(const cEcmInfo *ecm, int id);
  };

static cEcmShares ecmshares;

cEcmShare *cEcmShares::Find(const cEcmInfo *ecm)
{
  for(cEcmShare *e=First(); e; e=Next(e))
    if(e->pid==ecm->ecm_pid && e->source==ecm->source && e->transponder==ecm->transponder)
      return e;
  return 0;
}

int cEcmShares::FindShare(const cEcmInfo *ecm)
{
  cEcmShare *e=Find(ecm);
  PRINTF(L_CC_CCCAM2,"shareid %08x for %04x/%x/%x",e ? e->shareid:0,ecm->ecm_pid,ecm->source,ecm->transponder);
  return e ? e->shareid:0;
}

void cEcmShares::AddShare(const cEcmInfo *ecm, int id)
{
  cEcmShare *e=Find(ecm);
  if(e) {
    if(e->shareid!=id) {
      e->shareid=id;
      PRINTF(L_CC_CCCAM2,"update shareid %08x for %04x/%x/%x",id,ecm->ecm_pid,ecm->source,ecm->transponder);
      }
    }
  else {
    Add(new cEcmShare(ecm,id));
    PRINTF(L_CC_CCCAM2,"added shareid %08x for %04x/%x/%x",id,ecm->ecm_pid,ecm->source,ecm->transponder);
    }
}

// -- cShare -------------------------------------------------------------------

class cShares;

class cShare : public cSimpleItem {
friend class cShares;
private:
  int shareid, caid, provid;
  int hops;
  time_t last;
  bool success;
public:
  cShare(int Shareid, int Caid, int Provid, int Hops);
  cShare(const cShare *s);
  bool Compare(const cShare *s) const;
  int ShareID(void) const { return shareid; };
  int CaID(void) const { return caid;};
  int ProvID(void) const { return provid; };
  int Hops(void) const { return hops; };
  time_t Last(void) const { return last; };
  };

cShare::cShare(int Shareid, int Caid, int Provid, int Hops)
{
  shareid=Shareid;
  caid=Caid;
  provid=Provid;
  hops=Hops;
  last=0; success=false;
}

cShare::cShare(const cShare *s)
{
  shareid=s->shareid;
  caid=s->caid;
  provid=s->provid;
  hops=s->hops;
  last=s->last;
  success=s->success;
}

bool cShare::Compare(const cShare *s) const
{
  // successfull is better ;)
  if(s->success!=success) return s->success;
  // lower hops is better
  if(s->hops!=hops) return s->hops<hops;
  // later (higher) last is better
  return s->last>last;
}

// -- cShares ------------------------------------------------------------------

class cShares : public cMutex, public cSimpleList<cShare> {
private:
  cShare *Find(int shareid);
public:
  int GetShares(const cEcmInfo *ecm, cShares *ss);
  void SetLast(int shareid, time_t last);
  };

cShare *cShares::Find(int shareid)
{
  for(cShare *s=First(); s; s=Next(s))
    if(s->shareid==shareid) return s;
  return 0;
}

void cShares::SetLast(int shareid, time_t last)
{
  Lock();
  cShare *s=Find(shareid);
  if(s) s->last=last;
  Unlock();
}

int cShares::GetShares(const cEcmInfo *ecm, cShares *ss)
{
  Lock();
  Clear();
  int oldid=ecmshares.FindShare(ecm);
  ss->Lock();
  for(cShare *s=ss->First(); s; s=ss->Next(s)) {
    if(s->caid==ecm->caId && ((ecm->caId>>8)!=0x05 || s->provid==ecm->provId) && !Find(s->shareid)) {
      cShare *n=new cShare(s);
      if(n->shareid==oldid) n->success=true;
      // keep the list sorted
      cShare *l=0;
      for(cShare *t=First(); t; t=Next(t)) {
        if(t->Compare(n)) {
          if(l) Add(n,l); else Ins(n);
          n=0; break;
          }
        l=t;
        }
      if(n) Add(n);
      }
    }
  ss->Unlock();
  Unlock();
  return Count();
}

// -- cCardClientCCcam2 ---------------------------------------------------------

class cCardClientCCcam2 : public cCardClient , private cThread {
private:
  cCCcamCrypt encr, decr;
  cShares shares;
  cNetSocket so;
  unsigned char nodeid[8];
  int shareid;
  char username[21], password[21];
  //
  bool newcw;
  unsigned char cw[16];
  cMutex cwmutex;
  cCondVar cwwait;
  //
  void Logout(void);
  void PacketAnalyzer(const unsigned char *data, int length);
protected:
  virtual bool Login(void);
  virtual void Action(void);
public:
  cCardClientCCcam2(const char *Name);
  ~cCardClientCCcam2();
  virtual bool Init(const char *CfgDir);
  virtual bool ProcessECM(const cEcmInfo *ecm, const unsigned char *data, unsigned char *Cw, int cardnum);
  virtual bool ProcessEMM(int caSys, const unsigned char *data);
  };

static cCardClientLinkReg<cCardClientCCcam2> __ncd("cccam2");

cCardClientCCcam2::cCardClientCCcam2(const char *Name)
:cCardClient(Name)
,cThread("CCcam2 listener")
,so(DEFAULT_CONNECT_TIMEOUT,2,600)
{
  shareid=0; newcw=false;
}

cCardClientCCcam2::~cCardClientCCcam2()
{
  Logout();
}

void cCardClientCCcam2::PacketAnalyzer(const unsigned char *data, int length)
{
  while(length>4) {
    int cmdlen=(data[2]<<8)+data[3];
    if(cmdlen<=0 || cmdlen+4>length) break;
    switch(data[1]) {
      case 1:
        {
        PRINTF(L_CC_CCCAM2,"got CW, current shareid %08x",shareid);
        unsigned char tempcw[16];
        memcpy(tempcw,data+4,16);
        cCCcamCrypt::ScrambleDcw(tempcw,16,nodeid,shareid);
        if(cCCcamCrypt::DcwChecksum(tempcw)) {
          cwmutex.Lock();
          newcw=true;
          memcpy(cw,tempcw,16);
          cwwait.Broadcast();
          cwmutex.Unlock();
          }
        decr.Decrypt(tempcw,tempcw,16);
        break;
        }
      case 4:
        {
        int shareid=SHAREID(&data[4]);
        shares.Lock();
        for(cShare *s=shares.First(); s; s=shares.Next(s)) {
          if(s->ShareID()==shareid) {
            PRINTF(L_CC_CCCAM2,"REMOVE share %08x caid: %04x provider: %06x",s->ShareID(),s->CaID(),s->ProvID());
            shares.Del(s);
            break;
            }
          }
        shares.Unlock();
        break;
        }
      case 7:
        {
        int caid=(data[8+4]<<8) | data[9+4];
        int shareid=SHAREID(&data[4]);
        int provider_counts=data[20+4];
        int uphops=data[10+4];
        int maxdown=data[11+4];
        LDUMP(L_CC_CCCAM2,data+12+4,8,"share %08x serial ",shareid);
        PRINTF(L_CC_CCCAM2,"share %08x uphops %d maxdown %d",shareid,uphops,maxdown);
        for(int i=0; i<provider_counts; i++) {
          int provider=(data[21+4+i*7]<<16) | (data[22+4+i*7]<<8) | data[23+4+i*7];
          shares.Lock();
          shares.Add(new cShare(shareid,caid,provider,uphops));
          shares.Unlock();
          PRINTF(L_CC_CCCAM2,"ADD share %08x caid: %04x provider: %06x",shareid,caid,provider);
          }
        break;
        }
      case 8:
        LDUMP(L_CC_CCCAM2,data+4,8,"Server NodeId: ");
        PRINTF(L_CC_CCCAM2,"Server Version: %s",data+4+8);
        PRINTF(L_CC_CCCAM2,"Builder Version: %s",data+4+8+32);
        break;
      case 0xff:
      case 0xfe:
        PRINTF(L_CC_CCCAM2,"server can't decode this ecm");
        cwmutex.Lock();
        newcw=false;
        cwwait.Broadcast();
        cwmutex.Unlock();
        break;
      default:
        PRINTF(L_CC_CCCAM2,"got unhandled cmd %x",data[1]);
        break;
      }
    data+=cmdlen+4; length-=cmdlen+4;
    }
}

bool cCardClientCCcam2::Init(const char *config)
{
  cMutexLock lock(this);
  int num=0;
  if(!ParseStdConfig(config,&num)
     || sscanf(&config[num],":%20[^:]:%20[^:]",username,password)!=2 ) return false;
  PRINTF(L_CC_CORE,"%s: username=%s password=%s",name,username,password);
  for(unsigned int i=0; i<sizeof(nodeid); i++) nodeid[i]=rand();
  LDUMP(L_CC_CCCAM2,nodeid,sizeof(nodeid),"NODEID:");
  return Immediate() ? Login() : true;
}

void cCardClientCCcam2::Logout(void)
{
  Cancel(3);
  so.Disconnect();
  so.SetQuietLog(true);
}

bool cCardClientCCcam2::Login(void)
{
  Logout();
  shares.Lock();
  shares.Clear();
  shares.Unlock();
  if(!so.Connect(hostname,port)) return false;

  unsigned char buffer[512];
  int len;
  if((len=so.Read(buffer,sizeof(buffer),10000))<=0) {
    PRINTF(L_CC_CCCAM2,"no welcome from server");
    return false;
    }
  LDUMP(L_CC_CCCAM2,buffer,len,"welcome answer:");
  if(len!=16 || !cCCcamCrypt::CheckConnectChecksum(buffer,len)) {
    PRINTF(L_CC_CCCAM2,"bad welcome from server");
    return false;
    }
  PRINTF(L_CC_CCCAM2,"welcome checksum correct");

  cCCcamCrypt::Xor(buffer,len);
  unsigned char buff2[64];
  SHA1(buffer,len,buff2);
  decr.Init(buff2,20);
  decr.Decrypt(buffer,buffer,16);
  encr.Init(buffer,16);
  encr.Encrypt(buff2,buff2,20);

  LDUMP(L_CC_CCCAM2,buff2,20,"welcome response:");
  encr.Encrypt(buff2,buffer,20);
  if(so.Write(buffer,20)!=20) {
    PRINTF(L_CC_CCCAM2,"failed to send welcome response");
    return false;
    }

  memset(buff2,0,20);
  strcpy((char *)buff2,username);
  LDUMP(L_CC_CCCAM2,buff2,20,"send username:");
  encr.Encrypt(buff2,buffer,20);
  if(so.Write(buffer,20)!=20) {
    PRINTF(L_CC_CCCAM2,"failed to send username");
    return false;
    }

  encr.Encrypt((unsigned char *)password,buffer,strlen(password));
  encr.Encrypt((unsigned char *)cccamstr,buffer,6);
  if(so.Write(buffer,6)!=6) {
    PRINTF(L_CC_CCCAM2,"failed to send password hash");
    return false;
    }

  if((len=so.Read(buffer,sizeof(buffer),6000))<=0) {
    PRINTF(L_CC_CCCAM2,"no login answer from server");
    return false;
    }
  decr.Decrypt(buffer,buffer,len);
  LDUMP(L_CC_CCCAM2,buffer,len,"login answer:");

  if(len<20 || strcmp(cccamstr,(char *)buffer)!=0) {
    PRINTF(L_CC_CCCAM2,"login failed");
    return false;
    }
  PRINTF(L_CC_LOGIN,"CCcam login succeed");

  static unsigned char clientinfo[] = {
    0x00, 
    //CCcam command
    0x00,
    //packet length
    0x00,0x5D,
#define USERNAME_POS 4
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,
#define NODEID_POS 24
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
#define WANTEMU_POS 32
    0x00,
#define VERSION_POS 33
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
#define BUILDERNUM_POS 65
    0x32,0x38,0x39,0x32,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
    };
  strcpy((char *)clientinfo+USERNAME_POS,username);
  strcpy((char *)clientinfo+VERSION_POS,"vdr-sc");
  memcpy(clientinfo+NODEID_POS,nodeid,8);
  LDUMP(L_CC_CCCAM2,clientinfo,sizeof(clientinfo),"send clientinfo:");
  encr.Encrypt(clientinfo,buffer,sizeof(clientinfo));
  if(so.Write(buffer,sizeof(clientinfo))!=sizeof(clientinfo)) {
    PRINTF(L_CC_CCCAM2,"failed to send clientinfo");
    return false;
    }
  Start();
  return true;
}

bool cCardClientCCcam2::ProcessECM(const cEcmInfo *ecm, const unsigned char *data, unsigned char *Cw, int cardnum)
{
  cMutexLock lock(this);
  if((!so.Connected() && !Login()) || !CanHandle(ecm->caId)) return false;

  static const unsigned char ecm_head[] = {
    0x00,
#define CCCAM_COMMAND_POS 1
    0x01,
#define CCCAM_LEN_POS 2
    0x00,0x00,
#define ECM_CAID_POS 4
    0x00,0x00,
    0x00,
#define ECM_PROVIDER_POS 7
    0x00,0x00,0x00,
#define ECM_HANDLER_POS 10
    0x00,0x00,0x00,0x00,
#define ECM_SID_POS 14
    0x00,0x00,
#define ECM_LEN_POS 16
    0x00,
#define ECM_DATA_POS 17
    };
  PRINTF(L_CC_CCCAM2,"ECM caid %04x prov %04x pid %04x",ecm->caId,ecm->provId,ecm->ecm_pid);
  int ecm_len=sizeof(ecm_head)+SCT_LEN(data);
  unsigned char *buffer=AUTOMEM(ecm_len);
  unsigned char *netbuff=AUTOMEM(ecm_len);
  memcpy(buffer,ecm_head,sizeof(ecm_head));
  memcpy(buffer+sizeof(ecm_head),data,SCT_LEN(data));
  buffer[CCCAM_COMMAND_POS]=1;
  buffer[CCCAM_LEN_POS]=(ecm_len-4)>>8;
  buffer[CCCAM_LEN_POS+1]=ecm_len-4;
  buffer[ECM_CAID_POS]=ecm->caId>>8;
  buffer[ECM_CAID_POS+1]=ecm->caId;
  buffer[ECM_PROVIDER_POS]=ecm->provId>>16;
  buffer[ECM_PROVIDER_POS+1]=ecm->provId>>8;
  buffer[ECM_PROVIDER_POS+2]=ecm->provId;
  buffer[ECM_SID_POS]=ecm->prgId>>8;
  buffer[ECM_SID_POS+1]=ecm->prgId;
  buffer[ECM_LEN_POS]=SCT_LEN(data);
  cShares curr;
  if(curr.GetShares(ecm,&shares)<1) return false;
  LBSTART(L_CC_CCCAM2);
  LBPUT("share try list for pid %04x",ecm->ecm_pid);
  time_t now=time(0);
  for(cShare *s=curr.First(); s; s=curr.Next(s))
    LBPUT("shareid %08x hops %d last %d: caid %04x prov %x",s->ShareID(),s->Hops(),(int)(s->Last()>0?s->Last()-now:-1),s->CaID(),s->ProvID());
  LBEND();
  for(cShare *s=curr.First(); s; s=curr.Next(s)) {
    if((shareid=s->ShareID())==0) continue;
    buffer[ECM_HANDLER_POS]=shareid>>24;
    buffer[ECM_HANDLER_POS+1]=shareid>>16;
    buffer[ECM_HANDLER_POS+2]=shareid>>8;
    buffer[ECM_HANDLER_POS+3]=shareid;
    PRINTF(L_CC_CCCAM2,"now try shareid %08x",shareid);
    HEXDUMP(L_CC_CCCAM2,buffer,ecm_len,"send ecm:");
    encr.Encrypt(buffer,netbuff,ecm_len);
    so.Write(netbuff,ecm_len);
    cwmutex.Lock();
    newcw=false;
    if(cwwait.TimedWait(cwmutex,7000)) {
      if(newcw) {
        memcpy(Cw,cw,16);
        cwmutex.Unlock();
        shares.SetLast(shareid,time(0));
        ecmshares.AddShare(ecm,shareid);
        PRINTF(L_CC_CCCAM2,"got CW");
        return true;
        }
      }
    else PRINTF(L_CC_CCCAM2,"getting CW timedout");
    cwmutex.Unlock();
    }
  return false;
}

bool cCardClientCCcam2::ProcessEMM(int caSys, const unsigned char *data)
{
  return false;
}

void cCardClientCCcam2::Action(void)
{
  while(Running() && so.Connected()) {
    unsigned char recvbuff[1024];
    int len=so.Read(recvbuff,sizeof(recvbuff));
    if(len>0) {
      decr.Decrypt(recvbuff,recvbuff,len);
      HEXDUMP(L_CC_CCCAM2,recvbuff,len,"msg in:");
      PacketAnalyzer(recvbuff,len);
      }
    }
}
