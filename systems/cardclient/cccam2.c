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
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <boost/scoped_array.hpp>
#include <openssl/sha.h>
#include "cc.h"
#include "network.h"

typedef unsigned char node_id[8];

struct cccam_crypt_block
{
   unsigned char keytable[256];
   unsigned char state;
   unsigned char counter;
   unsigned char sum;
};


// -- cHandlerItem -------------------------------------------------------------

class cHandlerItem : public cSimpleItem {
   private:
      int handlerid,caid,provid;
   public:
      cHandlerItem(int Handlerid, int Caid, int Provid);
      int GetHandlerID(void){ return handlerid; };
      int GetCaID(void) { return caid;};
      int GetProvID(void) { return provid; };
};

cHandlerItem::cHandlerItem(int Handlerid, int Caid, int Provid)
{
   handlerid=Handlerid;
   caid=Caid;
   provid=Provid;
}

class cHandlers {
   public:
      cSimpleList<cHandlerItem>  cHandlerList;
};

// -- cCCcam2Card ---------------------------------------------------------------

class cCCcam2Card : public cMutex {
   private:
      bool newcw;
      unsigned char cw[16];
      cCondVar cwwait;
   public:
      cCCcam2Card(void);
      bool GetCw(unsigned char *Cw);
      void NewCw(const unsigned char *Cw, bool result);
};

cCCcam2Card::cCCcam2Card(void) {
   newcw=false;
}

void cCCcam2Card::NewCw(const unsigned char *Cw, bool result)
{
  cMutexLock lock(this);
  if(result) {
     newcw=true;
     memcpy(cw,Cw,sizeof(cw));
  }else{
     newcw=false;
  }
  cwwait.Broadcast();
}

bool cCCcam2Card::GetCw(unsigned char *Cw)
{
  cMutexLock lock(this);
  if(!newcw) cwwait.Wait(*this);
  if(newcw) {
    memcpy(Cw,cw,sizeof(cw));
    newcw=false;
    return true;
    }
  return false;
}

// -- cCardClientCCcam2 ---------------------------------------------------------

class cCardClientCCcam2 : public cCardClient , private cThread {
private:
   cCCcam2Card card;
   cHandlers *handlers;
  cNetSocket so;
   node_id nodeid;
   int server_packet_count;
   bool login_completed;
   struct cccam_crypt_block client_encrypt_state,client_decrypt_state;
   unsigned char buffer[3072];
   unsigned char recvbuff[3072];
   unsigned char netbuff[1024];
   int shareid;
   unsigned char cwdata[16];
   char username[20], password[20];
   bool getcards;

   bool check_connect_checksum( unsigned char* data, int length );
   void cccam_xor( unsigned char* data, int length );
   int cccam_init_crypt( cccam_crypt_block* block, const unsigned char* key, int length );
   int cccam_decrypt( cccam_crypt_block* block, const unsigned char* in, unsigned char* out, int length );
   int cccam_encrypt( cccam_crypt_block* block, const unsigned char* in, unsigned char* out, int length );
   inline unsigned int pow_of_two( unsigned int n );
   inline unsigned int shift_right_and_fill( unsigned int value, unsigned int fill, unsigned int places );
   void scramble_dcw( unsigned char* data, unsigned int length, node_id nodeid, unsigned int shareid );
   bool dcw_checksum( unsigned char *data);
   bool packet_analyzer(cccam_crypt_block* block,unsigned char* data, int length);
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
,cThread("CCcam listener")
,so(DEFAULT_CONNECT_TIMEOUT,20,DEFAULT_IDLE_TIMEOUT)
{
  server_packet_count=0;
  login_completed=false;
  bzero(client_encrypt_state.keytable,sizeof(client_encrypt_state.keytable));
   bzero(client_decrypt_state.keytable,sizeof(client_decrypt_state.keytable));
   bzero(nodeid,sizeof(nodeid));
   bzero(buffer,sizeof(buffer));
   bzero(cwdata,sizeof(cwdata));
   handlers=new cHandlers();
   shareid=0;
   getcards=false;
}

cCardClientCCcam2::~cCardClientCCcam2()
{
   so.Disconnect();
  Cancel(3);
}

bool cCardClientCCcam2::check_connect_checksum( unsigned char* data, int length )
{
    bool valid=false;
   if (length == 16)  {
      unsigned char sum1( data[0] + data[4] + data[8] );
      unsigned char sum2( data[1] + data[5] + data[9] );
      unsigned char sum3( data[2] + data[6] + data[10] );
      unsigned char sum4( data[3] + data[7] + data[11] );
      valid = ( ( sum1 == data[12] ) && ( sum2 == data[13] ) && ( sum3 == data[14] ) && ( sum4 == data[15] ) );
   }
   return valid;
}

void cCardClientCCcam2::cccam_xor(unsigned char* data, int length)
{
    if (data == 0) return;
   if ( length >= 16 )   {
      const char* cccam="CCcam";
      unsigned char index(0);
      do
      {
         *(data + 8) = index * (*data);
          if ( index <= 5 ) *data ^= cccam[index];
         ++index;
         ++data;
      }
      while( index < 8 );
   }
}

int cCardClientCCcam2::cccam_init_crypt( cccam_crypt_block* block, const unsigned char* key, int length )
{
   int result=0;
  if ( block == 0 )  return result;
  unsigned char* keytable( &block->keytable[0] );
  unsigned char* it( &block->keytable[0] );
  unsigned char prev_char( 0 );
  unsigned char curr_char( 0 );
  int pos=0;
  do
   {
      keytable[pos] = pos;
      ++pos;
   }
   while ( pos <= 255 );
   block->counter = 0;
   block->sum = 0;
   pos = 0;
   do
   {
      curr_char = keytable[pos++] + *reinterpret_cast<const unsigned char*>(key + result);
      curr_char += prev_char;
      prev_char = curr_char;
      std::swap( *it++, *(keytable + curr_char) );
      result = ( result + 1 ) % length;
   }
   while ( pos <= 255 );
   block->state = *key;
   return result;
}

int cCardClientCCcam2::cccam_decrypt( cccam_crypt_block* block, const unsigned char* in, unsigned char* out, int length )
{
   if ( block == 0 ) return 0;
   unsigned char* keytable( &block->keytable[0] );
   int pos( 0 );
    do
    {
      unsigned char in_xor( *(in + pos) ^ block->state );
      unsigned char key_sum( block->sum + *(keytable + ++block->counter) );
      block->sum += *(keytable + block->counter);
      std::swap( *(keytable + block->counter), *(keytable + key_sum) );
      unsigned char out_xor( in_xor ^ *(keytable + (unsigned char)(*(keytable + key_sum) + *(keytable + block->counter))) );
      *(out + pos) = out_xor;
      block->state ^= out_xor;
      ++pos;
    }
    while ( pos < length );
   return length;
}

int cCardClientCCcam2::cccam_encrypt( cccam_crypt_block* block, const unsigned char* in, unsigned char* out, int length )
{
   if ( block == 0 )   return 0;
   unsigned char* keytable( &block->keytable[0] );
   int pos( 0 );
   do
   {
      unsigned char key_sum( block->sum + *(keytable + ++block->counter) );
      block->sum += *(keytable + block->counter);
      std::swap( *(keytable + block->counter), *(keytable + key_sum) );
      unsigned char state( *(in + pos) ^ *(keytable  + (unsigned char)(*(keytable + key_sum) + *(keytable + block->counter))) );
      *(out + pos) = block->state ^ state;
      block->state ^= *(in + pos++);
   }
   while ( pos < length );
   return length;
}

inline unsigned int cCardClientCCcam2::pow_of_two( unsigned int n )
{
   uint result( 1 );
  for ( uint i( 0 ); i < n; ++i) result *= 2;
  return result;
}

inline unsigned int cCardClientCCcam2::shift_right_and_fill( unsigned int value, unsigned int fill, unsigned int places )
{
   return ( value >> places ) | ( ( ( pow_of_two( places ) - 1 ) & fill ) << ( 32 - places ) );
}

void cCardClientCCcam2::scramble_dcw( unsigned char* data, unsigned int length, node_id nodeid, unsigned int shareid)
{
    unsigned int i( 0 );
   int s( 0 );
   int nodeid_high( ( nodeid[0] << 24 ) | ( nodeid[1] << 16 ) | ( nodeid[2] << 8 ) | nodeid[3] );
   int nodeid_low( ( nodeid[4] << 24 ) | ( nodeid[5] << 16 ) | ( nodeid[6] << 8 ) | nodeid[7] );
   do
   {
      /* Nible index, 0..4..8 */
      char nible_index( i + s );
      /* Shift one nible to the right for high and low nodeid */
      /* Make sure the first shift is an signed one (sar on intel), else you get wrong results! */
      int high( nodeid_high >> nible_index );
      int low( shift_right_and_fill( nodeid_low, nodeid_high, nible_index ) );
      /* After 8 nibles or 32 bits use bits from high, based on signed flag it will be 0x00 or 0xFF */
      if ( nible_index & 32 )
         low = high & 0xFF;
      char final( *(data + i) ^ ( low & 0xFF ) );
      /* Odd index inverts final */
      if ( i & 0x01 )
         final = ~final;
      /* Result */
      *(data + i) = ( ( shareid >> ( 2 * ( i & 0xFF ) ) ) & 0xFF ) ^ final;
      ++i;
      s += 3;
    }
    while ( i < length );
}

bool cCardClientCCcam2::dcw_checksum( unsigned char *data)
{
   if( ((data[0] + data[1] + data[2]) & 0xff)==data[3] &&
         ((data[4] + data[5] + data[6]) & 0xff)==data[7] &&
         ((data[8] + data[9] + data[10]) & 0xff)==data[11] &&
         ((data[12] + data[13] + data[14]) & 0xff)==data[15] )
      return true;
   return false;
}

bool cCardClientCCcam2::packet_analyzer(cccam_crypt_block* block,unsigned char* data, int length)
{
   if (length<4) return false;
   int cccam_command=data[1];
   int packet_len=(data[2] & 0xff) <<8 | (data[3] & 0xff);
   int wp=4;
   char str[32];
   if ((packet_len + 4)>length) return false;
   if(packet_len>=0) {
      switch(cccam_command) {
         case  0: {
            break;
         }
         case 1: {
            unsigned char tempcw[16];
            memcpy(tempcw,data+4,16);
            scramble_dcw( tempcw, 16, nodeid, shareid );
            if (dcw_checksum(tempcw)) {
               memcpy(cwdata,tempcw,16);
            }
            card.NewCw(cwdata,true);
            unsigned char temp[16];
            cccam_decrypt(block,tempcw,temp,16);
            break;
         }
         case 4: {
            int handler=(data[0+4] & 0xff)<<24 | (data[1+4] & 0xff)<<16 |(data[2+4] & 0xff)<<8 | (data[3+4] & 0xff);
            for ( cHandlerItem *hv=handlers->cHandlerList.First(); hv; hv=handlers->cHandlerList.Next(hv))
            {
               if(hv->GetHandlerID()==handler) handlers->cHandlerList.Del(hv);
               PRINTF(L_CC_CCCAM,"REMOVE handler %08x caid: %04x provider: %06x",hv->GetHandlerID(),hv->GetCaID(),hv->GetProvID());
            }
            break;
         }
         case 7: {
            int caid=(data[8+4] & 0xff )<< 8 | (data[9+4] & 0xff);
            int handler=(data[0+4] & 0xff)<<24 | (data[1+4] & 0xff)<<16 |(data[2+4] & 0xff)<<8 | (data[3+4] & 0xff);
           int provider_counts=data[20+4] & 0xff;
           int uphops=data[10+4];
           int maxdown=data[11+4];
           PRINTF(L_CC_CCCAM,"handler %08x serial %s uphops %d maxdown %d",handler,HexStr(str,data+12+4,8),uphops,maxdown);
           for(int i=0;i<provider_counts;i++) {
              int provider=(data[21+4+i*7] & 0xff)<<16 | (data[22+4+i*7] & 0xff)<<8 | (data[23+4+i*7] & 0xff);
              cHandlerItem *n=new cHandlerItem(handler,caid,provider);
              handlers->cHandlerList.Add(n);
              PRINTF(L_CC_CCCAM,"ADD handler %08x caid: %04x provider: %06x",handler,caid,provider);
           }
           getcards=true;
            break;
         }
         case 8: {
            PRINTF(L_CC_CCCAM,"Server NodeId %s",HexStr(str,data+wp,8));
            wp+=8;
            PRINTF(L_CC_CCCAM,"Server Version: %s",data+wp);
            wp+=32;
            PRINTF(L_CC_CCCAM,"Builder Version: %s",data+wp);
            break;
         }
         case 0xfe:{
            PRINTF(L_CC_CCCAM,"cccam can't decode this ecm");
            card.NewCw(cwdata,false);
            break;
         }
         case 0xff:{
            PRINTF(L_CC_CCCAM,"cccam can't decode this ecm");
            card.NewCw(cwdata,false);
            break;
         }
         default: {
            break;
         }
      }
   }
   if((packet_len+4)<length) packet_analyzer(block,data+4+packet_len,length-4-packet_len);
   return true;
}

bool cCardClientCCcam2::Init(const char *config)
{
  cMutexLock lock(this);
  int num=0;
  int randomfd=open("/dev/urandom",O_RDONLY|O_NONBLOCK);
  if(randomfd<0) return false;
  int len=read(randomfd,nodeid,sizeof(nodeid));
  close(randomfd);
  if (len!=sizeof(nodeid)) return false;
  if(!ParseStdConfig(config,&num)
     || sscanf(&config[num],":%40[^:]:%40[^:]",username,password)!=2 ) return false;
  char str[32];
  PRINTF(L_CC_CORE,"%s: username=%s password=%s nodeid=%s",name,username,password,HexStr(str,nodeid,sizeof(nodeid)));
  return Immediate() ? Login() : true;
}

bool cCardClientCCcam2::Login(void)
{
   const char *str="CCcam";
   unsigned char clientinfo[]={
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
   boost::scoped_array<unsigned char> do_not_overwrite( new unsigned char[20] );
   boost::scoped_array<unsigned char> response( new unsigned char[20] );
   boost::scoped_array<unsigned char> sendbuff( new unsigned char[20] );
   boost::scoped_array<unsigned char> hash(new unsigned char[20]);
  cMutexLock lock(this);
  login_completed=false;
  getcards=false;
  server_packet_count=0;
  int len=0;
  so.Disconnect();
  if(!so.Connect(hostname,port)) return false;
   while(len=so.Read(buffer,sizeof(buffer),-1)) {
     if (server_packet_count > 0) {
        cccam_decrypt( &client_decrypt_state, buffer, buffer, len );
        HEXDUMP(L_CC_CCCAM,buffer,len,"Receive Messages");
     }
     if (login_completed) {
        packet_analyzer(&client_decrypt_state,buffer,len);
        if(getcards)  {
           Start();
           return true;
        }
     }
     switch ( server_packet_count ) {
        case 0:
           if(len!=16) return false;
           if(!check_connect_checksum(buffer,len)) return false;
           PRINTF(L_CC_CCCAM,"Receive Message CheckSum correct");
           cccam_xor(buffer,len);

           SHA_CTX ctx;
           SHA1_Init( &ctx );
            SHA1_Update( &ctx, buffer, len);
            SHA1_Final( hash.get(),&ctx );

            cccam_init_crypt( &client_decrypt_state, hash.get(), 20 );
            cccam_decrypt( &client_decrypt_state, buffer, buffer, 16 );
            cccam_init_crypt( &client_encrypt_state, buffer, 16 );
            cccam_encrypt( &client_encrypt_state, hash.get(), hash.get(), 20 );

            HEXDUMP(L_CC_CCCAM,hash.get(),20,"Send Messages");
            cccam_encrypt( &client_encrypt_state, hash.get(), response.get(), 20 );
            so.Write(response.get(),20);
            bzero(response.get(),20);
            bzero(sendbuff.get(),20);
            strcpy(reinterpret_cast<char*>(response.get()),username);
            HEXDUMP(L_CC_CCCAM,response.get(),20,"Send UserName Messages");
            cccam_encrypt( &client_encrypt_state, response.get(), sendbuff.get(), 20 );
           so.Write(sendbuff.get(),20);
           bzero(response.get(),20);
            bzero(sendbuff.get(),20);
            strcpy(reinterpret_cast<char*>(response.get()),password);
            HEXDUMP(L_CC_CCCAM,response.get(),20,"Password");
            cccam_encrypt( &client_encrypt_state, response.get(), sendbuff.get(), strlen(password));
            bzero(sendbuff.get(),20);
            cccam_encrypt( &client_encrypt_state,reinterpret_cast<const unsigned char*>(str), sendbuff.get(), 6 );
            so.Write(sendbuff.get(),6);
            break;
         case 1:
            if (len < 20) break;
            if(strcmp("CCcam", reinterpret_cast<char*>(buffer))==0) {
               login_completed=true;
               PRINTF(L_CC_CORE,"CCcam login Success!");
               strcpy(reinterpret_cast<char*>(clientinfo + USERNAME_POS),username);
               strcpy(reinterpret_cast<char*>(clientinfo + VERSION_POS),"vdr-sc");
               memcpy(clientinfo + NODEID_POS,nodeid,8);
               bzero(netbuff,sizeof(netbuff));
               cccam_encrypt( &client_encrypt_state,reinterpret_cast<const unsigned char*>(&clientinfo),
                reinterpret_cast<unsigned char*>(&netbuff), sizeof(clientinfo) );
               so.Write(netbuff,sizeof(clientinfo));
            }else{
               return false;
            }
            break;
         default:
            break;
     }
     server_packet_count++;
  }
  return false;
}

bool cCardClientCCcam2::ProcessECM(const cEcmInfo *ecm, const unsigned char *data, unsigned char *cw, int cardnum)
{
   const unsigned char ecm_head[]={
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
  cMutexLock lock(this);
  if((!so.Connected() && !Login()) || !CanHandle(ecm->caId)) return false;
  cCCcam2Card *c=&card;
  shareid=0;
  bzero(buffer,sizeof(buffer));
  memcpy(buffer,ecm_head,sizeof(ecm_head));
  memcpy(buffer+sizeof(ecm_head),data,SCT_LEN(data));
  int ecm_len=sizeof(ecm_head)+SCT_LEN(data);
  buffer[CCCAM_COMMAND_POS]=1;
  buffer[CCCAM_LEN_POS]=((ecm_len-4) >>8) & 0xff;
  buffer[CCCAM_LEN_POS+1]=(ecm_len-4) & 0xff;
  buffer[ECM_CAID_POS]=(ecm->caId >> 8) & 0xFF;
  buffer[ECM_CAID_POS+1]=ecm->caId & 0xFF;
  buffer[ECM_PROVIDER_POS]=(ecm->provId >>16) & 0xFF;
  buffer[ECM_PROVIDER_POS+1]=(ecm->provId >>8) & 0xFF;
   buffer[ECM_PROVIDER_POS+2]=ecm->provId & 0xFF;
   buffer[ECM_SID_POS]=(ecm->prgId >> 8) & 0xFF;
   buffer[ECM_SID_POS+1]=ecm->prgId & 0xFF;
   buffer[ECM_LEN_POS]=SCT_LEN(data);
   for ( cHandlerItem *hv=handlers->cHandlerList.First(); hv; hv=handlers->cHandlerList.Next(hv))
   {
      if(ecm->caId >= 0x500 && ecm->caId <= 0x05FF){
         if(hv->GetCaID()==ecm->caId && hv->GetProvID()==ecm->provId) shareid=hv->GetHandlerID();
      }
      else if((ecm->caId >= 0x1800 && ecm->caId <= 0x18FF)
         || (ecm->caId>= 0x0600 && ecm->caId <= 0x06FF)
         || (ecm->caId >= 0x0900 && ecm->caId <= 0x09FF)
         || ecm->caId == 0x0B00){
         if(hv->GetCaID()==ecm->caId) shareid=hv->GetHandlerID();
      }else{
         if(hv->GetCaID()==ecm->caId && hv->GetProvID()==ecm->provId) shareid=hv->GetHandlerID();
      }
   }
   PRINTF(L_CC_CORE,"%d: Ecm CaID %04x Provider %04x",cardnum,ecm->caId,ecm->provId);
   if(shareid==0) return false;
   buffer[ECM_HANDLER_POS]=(shareid>>24) & 0xFF;
   buffer[ECM_HANDLER_POS+1]=(shareid>>16) & 0xFF;
   buffer[ECM_HANDLER_POS+2]=(shareid>>8) & 0xFF;
   buffer[ECM_HANDLER_POS+3]=shareid & 0xFF;
   PRINTF(L_CC_CORE,"%d: Find Server HandlerID %x",cardnum,shareid);
   HEXDUMP(L_CC_CCCAM,buffer,ecm_len,"%d: Send ECM Messages",cardnum);
   cccam_encrypt( &client_encrypt_state,buffer,netbuff,ecm_len);
   so.Write(netbuff,ecm_len);
   if(!c->GetCw(cw)) return false;
   PRINTF(L_CC_CCCAM,"%d: got CW",cardnum);
   return true;
}

bool cCardClientCCcam2::ProcessEMM(int caSys, const unsigned char *data)
{
   return false;
}

void cCardClientCCcam2::Action(void)
{
   while(Running()) {
              usleep(100);
      bzero(recvbuff,sizeof(recvbuff));
     int len=so.Read(recvbuff,sizeof(recvbuff),0);
     if(len>0) {
         cccam_decrypt( &client_decrypt_state, recvbuff, recvbuff, len );
         HEXDUMP(L_CC_CCCAM,recvbuff,len,"Receive Messages");
         packet_analyzer(&client_decrypt_state,recvbuff,len);
      }
   }
}
