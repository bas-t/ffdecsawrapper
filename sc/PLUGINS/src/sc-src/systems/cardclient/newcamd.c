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
#include <stdlib.h>
#include <crypt.h>
#include <byteswap.h>

#include <vdr/thread.h>

#include "cc.h"
#include "misc.h"
#include "parse.h"

#include <openssl/des.h>

#define CWS_NETMSGSIZE 272

// -- cTripleDes ---------------------------------------------------------------

class cTripleDes {
private:
  DES_key_schedule ks1,ks2;
  //
  void SetOddParity(unsigned char *key); // key must be 16 bytes!
protected:
  unsigned char desKey[16];
  //
  void ScheduleKey(void);
  int PadMessage(unsigned char *data, int len);
  void Expand(unsigned char *expanded, const unsigned char *normal); // 14 byte key input, 16 byte expanded output
  void Decrypt(unsigned char *data, int len);
  const unsigned char *Encrypt(const unsigned char *data, int len, unsigned char *crypt);
  };

void cTripleDes::SetOddParity(unsigned char *key)
{
  DES_set_odd_parity((DES_cblock *)&key[0]); // set odd parity on both keys
  DES_set_odd_parity((DES_cblock *)&key[8]); // 
}

void cTripleDes::ScheduleKey(void)
{
  DES_key_sched((DES_cblock *)&desKey[0],&ks1);
  DES_key_sched((DES_cblock *)&desKey[8],&ks2);
}

void cTripleDes::Expand(unsigned char *expand, const unsigned char *normal)
{
  expand[0]  =   normal[0] & 0xfe;
  expand[1]  = ((normal[0] << 7) | (normal[1] >> 1)) & 0xfe;
  expand[2]  = ((normal[1] << 6) | (normal[2] >> 2)) & 0xfe;
  expand[3]  = ((normal[2] << 5) | (normal[3] >> 3)) & 0xfe;
  expand[4]  = ((normal[3] << 4) | (normal[4] >> 4)) & 0xfe;
  expand[5]  = ((normal[4] << 3) | (normal[5] >> 5)) & 0xfe;
  expand[6]  = ((normal[5] << 2) | (normal[6] >> 6)) & 0xfe;
  expand[7]  =   normal[6] << 1;
  expand[8]  =   normal[7] & 0xfe;
  expand[9]  = ((normal[7] << 7)  | (normal[8] >> 1)) & 0xfe;
  expand[10] = ((normal[8] << 6)  | (normal[9] >> 2)) & 0xfe;
  expand[11] = ((normal[9] << 5)  | (normal[10] >> 3)) & 0xfe;
  expand[12] = ((normal[10] << 4) | (normal[11] >> 4)) & 0xfe;
  expand[13] = ((normal[11] << 3) | (normal[12] >> 5)) & 0xfe;
  expand[14] = ((normal[12] << 2) | (normal[13] >> 6)) & 0xfe;
  expand[15] =   normal[13] << 1;
  SetOddParity(expand);
}

int cTripleDes::PadMessage(unsigned char *data, int len)
{
  DES_cblock padBytes;
  unsigned char noPadBytes;

  noPadBytes = (8 - ((len - 1) % 8)) % 8;
  if(len+noPadBytes+1 >= CWS_NETMSGSIZE-8) {
    PRINTF(L_CC_NEWCAMD,"message overflow in cTripleDes::PadMessage");
    return -1;
    }

  DES_random_key((DES_cblock *)padBytes);
  memcpy(data+len,padBytes,noPadBytes); len+=noPadBytes;
  data[len]=XorSum(data+2,len-2);
  return len+1;
}

const unsigned char *cTripleDes::Encrypt(const unsigned char *data, int len, unsigned char *crypt)
{
  DES_cblock ivec;
  DES_random_key((DES_cblock *)ivec);
  memcpy(crypt+len,ivec,sizeof(ivec));
  DES_ede2_cbc_encrypt(data+2,crypt+2,len-2,&ks1,&ks2,(DES_cblock *)ivec,DES_ENCRYPT);
  return crypt;
}

void cTripleDes::Decrypt(unsigned char *data, int len)
{
  if((len-2) % 8 || (len-2)<16) {
    PRINTF(L_CC_NEWCAMD,"warning: encrypted data size mismatch");
    return;
    }
  DES_cblock ivec;
  len-=sizeof(ivec); memcpy(ivec, data+len, sizeof(ivec));
  DES_ede2_cbc_encrypt(data+2,data+2,len-2,&ks1,&ks2,(DES_cblock *)ivec,DES_DECRYPT);
}

// -- cNewCamdClient -----------------------------------------------------------

#define USERLEN        32
#define PASSWDLEN      32
#define CWS_FIRSTCMDNO 0xe0

typedef enum {
  MSG_CLIENT_2_SERVER_LOGIN = CWS_FIRSTCMDNO,
  MSG_CLIENT_2_SERVER_LOGIN_ACK,
  MSG_CLIENT_2_SERVER_LOGIN_NAK,
  MSG_CARD_DATA_REQ,
  MSG_CARD_DATA,
  MSG_SERVER_2_CLIENT_NAME,
  MSG_SERVER_2_CLIENT_NAME_ACK,
  MSG_SERVER_2_CLIENT_NAME_NAK,
  MSG_SERVER_2_CLIENT_LOGIN,
  MSG_SERVER_2_CLIENT_LOGIN_ACK,
  MSG_SERVER_2_CLIENT_LOGIN_NAK,
  MSG_ADMIN,
  MSG_ADMIN_ACK,
  MSG_ADMIN_LOGIN,
  MSG_ADMIN_LOGIN_ACK,
  MSG_ADMIN_LOGIN_NAK,
  MSG_ADMIN_COMMAND,
  MSG_ADMIN_COMMAND_ACK,
  MSG_ADMIN_COMMAND_NAK
  } net_msg_type_t;

typedef enum {
  COMMTYPE_CLIENT,
  COMMTYPE_SERVER
  } comm_type_t;

struct CustomData {
  union {
    struct {
      unsigned short prgId; // Big-Endian
      unsigned char data[6];
      } V525;
    struct {
      unsigned int prgId;  // Big-Endian
      } V520;
    };
  };

class cCardClientNewCamd : public cCardClient, private cTripleDes, private cIdSet {
private:
  unsigned char configKey[14];
  unsigned short netMsgId;
  int caId, protoVers, cdLen;
  bool emmProcessing, loginOK;
  char username[USERLEN], password[PASSWDLEN];
  //
  void InitVars(void);
  void InitProtoVers(int vers);
  bool NextProto(void);
  void InitCustomData(struct CustomData *cd, const unsigned short PrgId, const unsigned char *data);
  void PrepareLoginKey(unsigned char *deskey, const unsigned char *rkey, const unsigned char *ckey);
  // Client Helper functions
  bool SendMessage(const unsigned char *data, int len, bool UseMsgId, const struct CustomData *cd=0, comm_type_t commType=COMMTYPE_CLIENT);
  int ReceiveMessage(unsigned char *data, bool UseMsgId, struct CustomData *cd=0, comm_type_t commType=COMMTYPE_CLIENT);
  bool CmdSend(net_msg_type_t cmd,  comm_type_t commType=COMMTYPE_CLIENT);
  int CmdReceive(comm_type_t commType=COMMTYPE_CLIENT);
public:
  cCardClientNewCamd(const char *Name);
  // 
  virtual bool Init(const char *CfgDir);
  virtual bool Login(void);
  virtual bool CanHandle(unsigned short SysId);
  virtual bool ProcessECM(const cEcmInfo *ecm, const unsigned char *data, unsigned char *Cw);
  virtual bool ProcessEMM(int caSys, const unsigned char *data);
  };

static cCardClientLinkReg<cCardClientNewCamd> __ncd("Newcamd");

cCardClientNewCamd::cCardClientNewCamd(const char *Name)
:cCardClient(Name)
{
  memset(username,0,sizeof(username));
  memset(password,0,sizeof(password));
  InitVars();
  InitProtoVers(525);
  so.SetRWTimeout(20*1000);
}

void cCardClientNewCamd::InitVars(void)
{
  netMsgId=0; caId=-1; emmProcessing=false;
  ResetIdSet();
}

void cCardClientNewCamd::InitProtoVers(int vers)
{
  switch(vers) {
    case 525: protoVers=525; cdLen=8; break;
    default:  protoVers=520; cdLen=4; break;
    }
  PRINTF(L_CC_NEWCAMD,"now using protocol version %d (cdLen=%d)",protoVers,cdLen);
  loginOK=false;
}

bool cCardClientNewCamd::NextProto(void)
{
  if(loginOK) return false;
  switch(protoVers) {
    case 525: InitProtoVers(520); break;
    default:  return false;
    }
  return true;
}

void cCardClientNewCamd::InitCustomData(struct CustomData *cd, const unsigned short PrgId, const unsigned char *data)
{
  if(cd) {
    switch(protoVers) {
      case 525:
        cd->V525.prgId=bswap_16(PrgId);
        if(data) memcpy(cd->V525.data,data,sizeof(cd->V525.data));
        else memset(cd->V525.data,0,sizeof(cd->V525.data));
        break;
      default:
        cd->V520.prgId=bswap_32((unsigned int)PrgId);
        break;
      }
    }
}

void cCardClientNewCamd::PrepareLoginKey(unsigned char *deskey, const unsigned char *rkey, const unsigned char *ckey)
{
  unsigned char tmpkey[14];
  for (int i=0; i<(int)sizeof(tmpkey); i++) { tmpkey[i]=rkey[i]^ckey[i]; }
  Expand(deskey, tmpkey);
}

bool cCardClientNewCamd::SendMessage(const unsigned char *data, int len, bool UseMsgId, const struct CustomData *cd, comm_type_t commType)
{
  if(len<3||len+cdLen+4>CWS_NETMSGSIZE) {
    PRINTF(L_CC_NEWCAMD,"bad message size %d in SendMessage",len);
    return false;
    }
  unsigned char netbuf[CWS_NETMSGSIZE];
  memset(&netbuf[2],0,cdLen+2);
  memcpy(&netbuf[cdLen+4],data,len);
  netbuf[cdLen+4+1]=(data[1]&0xf0)|(((len-3)>>8)&0x0f);
  netbuf[cdLen+4+2]=(len-3)&0xff;
  len+=4;
  if(cd) memcpy(&netbuf[4],cd,cdLen);
  len+=cdLen;
  if(UseMsgId) {
    if(commType==COMMTYPE_CLIENT) netMsgId++;
    netbuf[2]=netMsgId>>8;
    netbuf[3]=netMsgId&0xff;
    }
  if((len=cTripleDes::PadMessage(netbuf,len))<0) {
    PRINTF(L_CC_NEWCAMD,"PadMessage failed");
    return false;
    }
  if((data=cTripleDes::Encrypt(netbuf,len,netbuf))==0) {
    PRINTF(L_CC_NEWCAMD,"Encrypt failed");
    return false;
    }
  len+=sizeof(DES_cblock);
  netbuf[0]=(len-2)>>8;
  netbuf[1]=(len-2)&0xff;
  return cCardClient::SendMsg(netbuf,len);
}

int cCardClientNewCamd::ReceiveMessage(unsigned char *data, bool UseMsgId, struct CustomData *cd, comm_type_t commType)
{
  unsigned char netbuf[CWS_NETMSGSIZE];
  if(cCardClient::RecvMsg(netbuf,2)<0) {
    PRINTF(L_CC_NEWCAMD,"failed to read message length");
    return 0;
    }
  int mlen=WORD(netbuf,0,0xFFFF);
  if(mlen>CWS_NETMSGSIZE-2) {
   PRINTF(L_CC_NEWCAMD,"receive message buffer overflow");
   return 0;
   }
  if(cCardClient::RecvMsg(netbuf+2,mlen,200)<0) {
    PRINTF(L_CC_NEWCAMD,"failed to read message");
    return 0;
    }
  mlen+=2;
  cTripleDes::Decrypt(netbuf,mlen); mlen-=sizeof(DES_cblock);
  if(XorSum(netbuf+2,mlen-2)) {
    PRINTF(L_CC_NEWCAMD,"checksum error");
    return 0;
    }

  int returnLen=WORD(netbuf,5+cdLen,0x0FFF)+3;
  if(cd) memcpy(cd,&netbuf[4],cdLen);
  if(UseMsgId) {
    switch(commType) {
      case COMMTYPE_SERVER:
        netMsgId=WORD(netbuf,2,0xFFFF);
        break;
      case COMMTYPE_CLIENT:
        if(netMsgId!=WORD(netbuf,2,0xFFFF)) {
          PRINTF(L_CC_NEWCAMD,"bad msgid %04x != %04x ",netMsgId,WORD(netbuf,2,0xFFFF));
          return -2;
          }
        break;
      default:
        PRINTF(L_CC_NEWCAMD,"unknown commType %x",commType);
        return -1;
      }
    }
  memcpy(data,netbuf+4+cdLen,returnLen);
  return returnLen;
}

bool cCardClientNewCamd::CmdSend(net_msg_type_t cmd, comm_type_t commType)
{
  unsigned char buffer[3];
  buffer[0]=cmd; buffer[1]=buffer[2]=0;
  return SendMessage(buffer,sizeof(buffer),false,0,commType);
}

int cCardClientNewCamd::CmdReceive(comm_type_t commType)
{
  unsigned char buffer[CWS_NETMSGSIZE];
  if(ReceiveMessage(buffer,false,0,commType)!=3) return -1;
  return buffer[0];
}

bool cCardClientNewCamd::CanHandle(unsigned short SysId)
{
  return (caId>=0 && (SysId==caId || (caId==0x1234 && SysId==0x1801))) || cCardClient::CanHandle(SysId);
}

bool cCardClientNewCamd::Init(const char *config)
{
  cMutexLock lock(this);
  Logout();
  int num=0;
  char key[29];
  const char *tmp=key;
  if(!ParseStdConfig(config,&num)
     || sscanf(&config[num],":%31[^:]:%31[^:]:%28[^:]",username,password,key)!=3
     || GetHex(tmp,configKey,sizeof(configKey),false)!=14) return false;
  char str[32];
  PRINTF(L_CC_CORE,"%s: username=%s password=%s key=%s",name,username,password,HexStr(str,configKey,14));
  return true;
}

bool cCardClientNewCamd::Login(void)
{
  Logout();
  if(!so.Connect(hostname,port)) return false;

  InitVars();
  unsigned char randData[14];
  if(so.Read(randData,sizeof(randData))<0) {
    PRINTF(L_CC_NEWCAMD,"no connect answer from %s:%d",hostname,port);
    Logout();
    return false;
    }

  char *crPasswd=crypt(password,"$1$abcdefgh$");
  unsigned char buffer[CWS_NETMSGSIZE];
  const int userLen=strlen(username)+1;
  const int passLen=strlen(crPasswd)+1;
  
  // prepare the initial login message
  buffer[0] = MSG_CLIENT_2_SERVER_LOGIN;
  buffer[1] = 0;
  buffer[2] = userLen+passLen;
  memcpy(&buffer[3],username,userLen);
  memcpy(&buffer[3]+userLen,crPasswd,passLen);

  // XOR configKey with randData and expand the 14 byte result -> 16 byte
  PrepareLoginKey(desKey,randData,configKey);
  cTripleDes::ScheduleKey();

  // set NewCS client identification
  // this seems to conflict with other cardservers e.g. rqcs !!
  //struct CustomData cd;
  //InitCustomData(&cd,0x5644,0);

  if(!SendMessage(buffer,buffer[2]+3,true) || CmdReceive()!=MSG_CLIENT_2_SERVER_LOGIN_ACK) {
    PRINTF(L_CC_NEWCAMD,"failed to login to cardserver for username %s (proto %d)",username,protoVers);
    if(NextProto()) return Login();
    return false;
    }

  // create the session key (for usage later)
  unsigned char tmpkey[14];
  memcpy(tmpkey, configKey, sizeof(tmpkey));
  const int passStrLen=strlen(crPasswd);
  for(int i=0; i<passStrLen; ++i) tmpkey[i%14]^=crPasswd[i];

  cTripleDes::Expand(desKey,tmpkey); // expand 14 byte key -> 16 byte
  cTripleDes::ScheduleKey();

  if(!CmdSend(MSG_CARD_DATA_REQ) || ReceiveMessage(buffer,false)<=0) return false;
  if(buffer[0] == MSG_CARD_DATA) {
    int c=(buffer[4]<<8)+buffer[5];
    if(c!=caId) CaidsChanged();
    caId=c;
    LBSTARTF(L_CC_LOGIN);
    char str[32], str2[32];
    LBPUT("%s: CaID=%04x admin=%d srvUA=%s",name,caId,buffer[3]==1,HexStr(str,&buffer[6],8));
    if(!CheckNull(&buffer[6],8)) {
      emmProcessing=true;
      switch(caId>>8) {
        case 0x17:
        case 0x06: SetCard(new cCardIrdeto(buffer[6+4],&buffer[6+5])); break;
        case 0x01: SetCard(new cCardSeca(&buffer[6+2])); break;
        case 0x0b: SetCard(new cCardConax(&buffer[6+1])); break;
        case 0x09: SetCard(new cCardNDS(&buffer[6+4])); break;
        case 0x05: SetCard(new cCardViaccess(&buffer[6+3])); break;
        case 0x0d: SetCard(new cCardCryptoworks(&buffer[6+3])); break;
        case 0x12:
        case 0x18: if(caId>=0x1801 || caId==0x1234) {
                     SetCard(new cCardNagra2(&buffer[6+4]));
                     break;
                     }
                   // fall through to default
        default:
          LBPUT(" (unhandled)");
          break;
        }
      }
    LBPUT(" provider");
    for(int i=(buffer[14]-1)*11; i>=0; i-=11) {
      LBPUT(" %s/%s",HexStr(str2,&buffer[15+i],3),HexStr(str,&buffer[18+i],8));
      if(!CheckNull(&buffer[18+i],8)) {
        switch(caId>>8) {
          case 0x17:
          case 0x06: AddProv(new cProviderIrdeto(buffer[18+i+4],&buffer[18+i+5])); break;
          case 0x01: AddProv(new cProviderSeca(&buffer[15+i+1],&buffer[18+i+4])); break;
          case 0x0b: AddProv(new cProviderConax(&buffer[18+i+1])); break;
          case 0x09: AddProv(new cProviderNDS(&buffer[18+i+4])); break;
          case 0x05: AddProv(new cProviderViaccess(&buffer[15+i],&buffer[18+i+4])); break;
          case 0x0d: AddProv(new cProviderCryptoworks(&buffer[18+i+3])); break;
          default:
            LBPUT(" <unhandled>");
            break;
          }
        }
      }
    LBEND();
    if(emmProcessing && !emmAllowed)
      PRINTF(L_CC_EMM,"%s: EMM disabled from config",name);
    }
  loginOK=true;
  return true;
}

bool cCardClientNewCamd::ProcessECM(const cEcmInfo *ecm, const unsigned char *data, unsigned char *cw)
{
  cMutexLock lock(this);
  if((!so.Connected() && !Login()) || !CanHandle(ecm->caId)) return false;
  so.Flush();

  struct CustomData cd;
  InitCustomData(&cd,(unsigned short)ecm->prgId,0);
  if(!SendMessage(data,SCT_LEN(data),true,&cd)) return false;
  unsigned char buffer[CWS_NETMSGSIZE];
  int n;
  while((n=ReceiveMessage(buffer,true))==-2)
    PRINTF(L_CC_NEWCAMD,"msg ID sync error. Retrying...");
  switch(n) {
    case 19: // ecm was decoded
      // check for zero cw, as newcs doesn't send both cw's every time
      if(!CheckNull(buffer+3+0,8)) memcpy(cw+0,buffer+3+0,8);
      if(!CheckNull(buffer+3+8,8)) memcpy(cw+8,buffer+3+8,8);
      return true;
    case 3:
      PRINTF(L_CC_ECM,"%s: card was not able to decode the channel",name);
      break;
    default:
      PRINTF(L_CC_NEWCAMD,"unexpected server response (code %d)",n);
      break;
    }
  return false;
}

bool cCardClientNewCamd::ProcessEMM(int caSys, const unsigned char *data)
{
  if(emmProcessing && emmAllowed) {
    cMutexLock lock(this);
    cAssembleData ad(data);
    if(MatchAndAssemble(&ad,0,0)) {
      while((data=ad.Assembled())) {
        int len=SCT_LEN(data);
        int id=msEMM.Get(data,len,0);
        if(id>0 || emmAllowed>1) {
          if(SendMessage(data,len,true,0)) {
            unsigned char buffer[CWS_NETMSGSIZE];
            len=ReceiveMessage(buffer,true);
            if(len>=3) {
              if(!(buffer[1]&0x10))
                PRINTF(L_CC_EMM,"EMM rejected by card");
              }
            else
              PRINTF(L_CC_NEWCAMD,"unexpected server response (code %d)",len);
            }
          msEMM.Cache(id,true,0);
          }
        }
      return true;
      }
    }
  return false;
}
