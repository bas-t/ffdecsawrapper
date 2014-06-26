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
#include <string.h>

#include "system-common.h"
#include "smartcard.h"
#include "data.h"
#include "misc.h"
#include "opts.h"
#include "parse.h"
#include "log-sc.h"
#include "version.h"

SCAPIVERSTAG();

#define SYSTEM_SECA          0x0100

#define SYSTEM_NAME          "SC-Seca"
#define SYSTEM_PRI           -5
#define SYSTEM_CAN_HANDLE(x) ((x)==SYSTEM_SECA)

#define SC_NAME "Seca"
#define SC_ID   MAKE_SC_ID('S','e','c','a')

#define SECADATE(buff,len,odate) { \
                                 const unsigned char *dd=(odate); \
                                 snprintf(buff,len,"%04d/%02d/%02d", \
                                   ((dd[0]&0xFE)>>1)+1990, \
                                   ((dd[0]&0x01)<<3) + ((dd[1]&0xE0)>>5), \
                                   dd[1]&0x1F); \
                                 }

#define L_SC        12
#define L_SC_PROC   LCLASS(L_SC,L_SC_LASTDEF<<1)
#define L_SC_ALL    LALL(L_SC_PROC)

static const struct LogModule lm_sc = {
  (LMOD_ENABLE|L_SC_ALL)&LOPT_MASK,
  (LMOD_ENABLE|L_SC_DEFDEF)&LOPT_MASK,
  "sc-seca",
  { L_SC_DEFNAMES,"process" }
  };
ADD_MODULE(L_SC,lm_sc)

static int blocker=0;

// -- cSystemScSeca ---------------------------------------------------------------

class cSystemScSeca : public cSystemScCore {
public:
  cSystemScSeca(void);
  };

cSystemScSeca::cSystemScSeca(void)
:cSystemScCore(SYSTEM_NAME,SYSTEM_PRI,SC_ID,"SC Seca")
{
  hasLogger=true;
  needsDescrData=true;
}

// -- cSystemLinkScSeca --------------------------------------------------------

static const char *block[] = {
  trNOOP("allow ALL"),
  trNOOP("block UNIQUE"),
  trNOOP("block SHARED"),
  trNOOP("block ALL")
  };

class cSystemLinkScSeca : public cSystemLink {
public:
  cSystemLinkScSeca(void);
  virtual bool CanHandle(unsigned short SysId);
  virtual cSystem *Create(void) { return new cSystemScSeca; }
  };

static cSystemLinkScSeca staticInit;

cSystemLinkScSeca::cSystemLinkScSeca(void)
:cSystemLink(SYSTEM_NAME,SYSTEM_PRI)
{
  opts=new cOpts(SYSTEM_NAME,1);
  opts->Add(new cOptSel("Blocker",trNOOP("SC-Seca: EMM updates"),&blocker,sizeof(block)/sizeof(char *),block));
  Feature.NeedsSmartCard();
}

bool cSystemLinkScSeca::CanHandle(unsigned short SysId)
{
  SysId&=SYSTEM_MASK;
  return smartcards.HaveCard(SC_ID) && SYSTEM_CAN_HANDLE(SysId);
}

// -- cProviderScSeca ----------------------------------------------------------

class cProviderScSeca : public cProviderSeca {
public:
  int index;
  unsigned char date[2], pbm[8];
  char name[17];
  //
  cProviderScSeca(const unsigned char *pi, const unsigned char *s):cProviderSeca(pi,s) {}
  };

// -- cSmartCardSeca -----------------------------------------------------------

struct SecaProvInfo {
  unsigned char prov[2];
  char name[16];
  unsigned char sa[3];
  unsigned char cb;
  unsigned char date[2];
  unsigned char rr;
  unsigned char rstart;
  unsigned char pbm[8];
  unsigned char rend;
  };

struct SecaChannelInfo {
  unsigned char pbm[8];
  unsigned char date[2];
  };

class cSmartCardSeca : public cSmartCard, private cIdSet {
private:
  char datebuff[16];
  //
  const char *Date(const unsigned char *date);
  bool CheckAccess(const unsigned char *data, const cProviderScSeca *p);
public:
  cSmartCardSeca(void);
  virtual bool Init(void);
  virtual bool Decode(const cEcmInfo *ecm, const unsigned char *data, unsigned char *cw);
  virtual bool Update(int pid, int caid, const unsigned char *data);
  };

static const struct StatusMsg msgs[] = {
  // ECM status messages
  { { 0x90,0x00 }, "Instruction executed without errors", true },
  { { 0x90,0x02 }, "Signature failed", false },
  { { 0x90,0x27 }, "Decoding the preview (non-error)", true },
  { { 0x93,0x02 }, "No access, check your subscription", false },
  { { 0x96,0x00 }, "Chain of nonvalid entrance or null event or all the nanos process, none decoding", false },
  // EMM update status messages
  { { 0x90,0x09 }, "Card update was not meant for this card", false },
  { { 0x90,0x19 }, "Card update successfull, PPUA updated", true },
  { { 0x97,0x00 }, "Card update was successful", true },
  { { 0x97,0x40 }, "Card update was successful", true },
  { { 0x97,0x50 }, "Card update was successful", true },
  { { 0x97,0x78 }, "Card update was not necessary", false },
  { { 0x97,0xe0 }, "EEPROM update was not necessary", false },
  // unknown message
  { { 0xFF,0xFF }, 0, false }
  };

static const struct CardConfig cardCfg = {
  SM_8E2,2000,300
  };

cSmartCardSeca::cSmartCardSeca(void)
:cSmartCard(&cardCfg,msgs)
{}

bool cSmartCardSeca::Init(void)
{
  static unsigned char ins0e[] = { 0xC1,0x0e,0x00,0x00,0x08 }; // get serial nr. (UA)
  static unsigned char ins16[] = { 0xC1,0x16,0x00,0x00,0x07 }; // get nr. of providers
  static unsigned char ins12[] = { 0xC1,0x12,0x00,0x00,0x19 }; // get provider info
  static unsigned char ins34[] = { 0xC1,0x34,0x00,0x00,0x03 }; // request provider pbm (data=0x00 0x00 0x00)
  static unsigned char ins32[] = { 0xC1,0x32,0x00,0x00,0x0A }; // get the pbm data (p1=provider)

  static const unsigned char atrTester[] = { 0x0E,0x6C,0xB6,0xD6 };
  if(atr->T!=0 || atr->histLen<7 || memcmp(&atr->hist[3],atrTester,4)) {
    PRINTF(L_SC_INIT,"doesn't looks like a Seca card");
    return false;
    }

  infoStr.Begin();
  infoStr.Strcat("Seca smartcard\n");
  const char *type;
  switch(atr->hist[0]*256+atr->hist[1]) {
    case 0x5084: type="Generic"; break;
    case 0x5384: type="Philips"; break;
    case 0x5130:
    case 0x5430:
    case 0x5760: type="Thompson"; break;
    case 0x5284:
    case 0x5842:
    case 0x6060: type="Siemens"; break;
    case 0x7070: type="Canal+ NL"; break;
    default:     type="Unknown"; break;
    }
  snprintf(idStr,sizeof(idStr),"%s (%s %d.%d)",SC_NAME,type,atr->hist[2]&0x0F,atr->hist[2]>>4);
  PRINTF(L_SC_INIT,"cardtype: %s %d.%d",type,atr->hist[2]&0x0F,atr->hist[2]>>4);
  
  ResetIdSet();
  unsigned char buff[MAX_LEN];
  if(!IsoRead(ins0e,buff) || !Status()) {
    PRINTF(L_SC_ERROR,"reading card serial failed");
    return false;
    }
  SetCard(new cCardSeca(&buff[2]));
  PRINTF(L_SC_INIT,"card serial number: %llu",Bin2LongLong(&buff[2],6));
  infoStr.Printf("Type: %s %d.%d  Serial: %llu\n",type,atr->hist[2]&0x0F,atr->hist[2]>>4,Bin2LongLong(&buff[2],6));

  if(!IsoRead(ins16,buff) || !Status()) {
    PRINTF(L_SC_ERROR,"reading provider map failed");
    return false;
    }
  int provMap=buff[2]*256+buff[3];
  if(LOG(L_SC_INIT)) {
    int n=0, i=provMap;
    do { n+=i&1; i>>=1; } while(i);
    PRINTF(L_SC_INIT,"card has %d providers (0x%04x)",n,provMap);
    }

  for(int i=0 ; i<16 ; i++) {
    if(provMap&(1<<i)) {
      PRINTF(L_SC_INIT,"reading info for provider index %d",i);
      ins12[2]=i;
      if(!IsoRead(ins12,buff) || !Status()) {
        PRINTF(L_SC_ERROR,"reading provider info failed");
        return false;
        }
      ins32[2]=i;
      static const unsigned char ins34data[] = { 0x00,0x00,0x00 };
      if(!IsoWrite(ins34,ins34data) || !Status() ||
         !IsoRead(ins32,&buff[ins12[4]]) || !Status())
        memset(&buff[ins12[4]],0xFF,ins32[4]); // fake PBM response if card doesn't support command

      struct SecaProvInfo *spi=(struct SecaProvInfo *)buff;
      cProviderScSeca *p=new cProviderScSeca(spi->prov,spi->sa);
      if(p) {
        AddProv(p);
        p->index=i;
        strn0cpy(p->name,spi->name,sizeof(p->name));
        memcpy(p->date,spi->date,sizeof(p->date));
        memcpy(p->pbm,spi->pbm,sizeof(p->pbm));
        }
      infoStr.Printf("Prov %x (%.16s) until %s\n",p->provId[0]*256+p->provId[1],p->name,Date(p->date));
      char str[20];
      PRINTF(L_SC_INIT,"provider 0x%02x%02x '%.16s' expires %s pbm %s",
             p->provId[0],p->provId[1],p->name,Date(p->date),HexStr(str,p->pbm,sizeof(p->pbm)));
      }
    }

  infoStr.Finish();
  return true;
}

const char *cSmartCardSeca::Date(const unsigned char *date)
{
  SECADATE(datebuff,sizeof(datebuff),date);
  return datebuff;
}

bool cSmartCardSeca::CheckAccess(const unsigned char *data, const cProviderScSeca *p)
{
  struct SecaChannelInfo *sci=(struct SecaChannelInfo*)data;
  if(sci->date[0]>p->date[0] || (sci->date[0]==p->date[0] && sci->date[1]>p->date[1]))
    return false;
  char str[20];  
  PRINTF(L_SC_PROC,"channelinfo date %s pbm %s",Date(sci->date),HexStr(str,sci->pbm,sizeof(sci->pbm)));
  int result=0;
  for(int i=7; i>=0; i--) // check pbm (is this right? seems to work though)
    result|=(sci->pbm[i]&p->pbm[i]);
  return (result!=0);
}

bool cSmartCardSeca::Decode(const cEcmInfo *ecm, const unsigned char *data, unsigned char *cw)
{
  static unsigned char ins3c[] = { 0xC1,0x3c,0x00,0x00,0x00 }; // coding cw
  static unsigned char ins3a[] = { 0xC1,0x3a,0x00,0x00,0x10 }; // decoding cw    
  static unsigned char ins30[] = { 0xC1,0x30,0x00,0x02,0x09 };
  static unsigned char ins30data[] = { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF }; 

  cProviderScSeca *p=(cProviderScSeca *)FindProv(data);
  if(p && ecm->Data()) {
    PRINTF(L_SC_PROC,"provider 0x%04x index %d '%.16s' (expires %s)",cParseSeca::ProvId(data),p->index,p->name,Date(p->date));
    if(CheckAccess(ecm->Data(),p)) {
      const unsigned char *payload;
      ins3c[2]=p->index | (cParseSeca::SysMode(data) & 0xF0);
      ins3c[3]=cParseSeca::KeyNr(data);
      ins3c[4]=cParseSeca::Payload(data,&payload);
      if(IsoWrite(ins3c,payload)) {
        bool r;
        if(sb[0]==0x90 && sb[1]==0x1A) // need to use token
          r=IsoWrite(ins30,ins30data) && Status() && IsoWrite(ins3c,payload) && Status();
        else
          r=Status();
        if(r && IsoRead(ins3a,cw) && Status())
          return true;
        }
      }
    else PRINTF(L_SC_ERROR,"update your subscription to view this channel");
    }
  return false;
}

bool cSmartCardSeca::Update(int pid, int caid, const unsigned char *data)
{
  static unsigned char ins40[] = { 0xC1,0x40,0x00,0x00,0x00 }; 

  if(data[0]==0x83) return false; // don't know how to handle
  if(blocker==0 || (data[0]==0x82 && blocker==2) || (data[0]==0x84 && blocker==1)) {
    cProviderScSeca *p=(cProviderScSeca *)FindProv(data);
    if(p && MatchEMM(data)) {
      PRINTF(L_SC_PROC,"got %s update",data[0]==0x82?"UNIQUE":"SHARED");
      const unsigned char *payload;
      ins40[2]=p->index | (cParseSeca::SysMode(data) & 0xF0);
      ins40[3]=cParseSeca::KeyNr(data);
      ins40[4]=cParseSeca::Payload(data,&payload);
      if(IsoWrite(ins40,payload) && Status()) return Init();
      }
    }
  return false;
}

// -- cSmartCardLinkSeca -------------------------------------------------------

class cSmartCardLinkSeca : public cSmartCardLink {
public:
  cSmartCardLinkSeca(void):cSmartCardLink(SC_NAME,SC_ID) {}
  virtual cSmartCard *Create(void) { return new cSmartCardSeca(); }
  };

static cSmartCardLinkSeca staticScInit;
