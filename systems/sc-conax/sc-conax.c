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
#include <ctype.h>

#include "system-common.h"
#include "smartcard.h"
#include "parse.h"
#include "misc.h"
#include "log-sc.h"
#include "log-core.h"
#include "version.h"

SCAPIVERSTAG();

#define SYSTEM_CONAX         0x0B00

#define SYSTEM_NAME          "SC-Conax"
#define SYSTEM_PRI           -5
#define SYSTEM_CAN_HANDLE(x) ((x)==SYSTEM_CONAX)

#define SC_NAME "Conax"
#define SC_ID   MAKE_SC_ID('C','o','n','x')

#define L_SC        9
#define L_SC_ALL    LALL(L_SC_LASTDEF)

static const struct LogModule lm_sc = {
  (LMOD_ENABLE|L_SC_ALL)&LOPT_MASK,
  (LMOD_ENABLE|L_SC_DEFDEF)&LOPT_MASK,
  "sc-conax",
  { L_SC_DEFNAMES }
  };
ADD_MODULE(L_SC,lm_sc)

// -- cSystemScConax -----------------------------------------------------------

class cSystemScConax : public cSystemScCore {
public:
  cSystemScConax(void);
  };

cSystemScConax::cSystemScConax(void)
:cSystemScCore(SYSTEM_NAME,SYSTEM_PRI,SC_ID,"SC Conax")
{
  hasLogger=true;
}

// -- cSystemLinkScConax -------------------------------------------------------

class cSystemLinkScConax : public cSystemLink {
public:
  cSystemLinkScConax(void);
  virtual bool CanHandle(unsigned short SysId);
  virtual cSystem *Create(void) { return new cSystemScConax; }
  };

static cSystemLinkScConax staticInit;

cSystemLinkScConax::cSystemLinkScConax(void)
:cSystemLink(SYSTEM_NAME,SYSTEM_PRI)
{
  Feature.NeedsSmartCard();
}

bool cSystemLinkScConax::CanHandle(unsigned short SysId)
{
  bool res=false;
  cSmartCard *card=smartcards.LockCard(SC_ID);
  if(card) {
    res=card->CanHandle(SysId);
    smartcards.ReleaseCard(card);
    }
  return res;
}

// -- cSmartCardDataConax ---------------------------------------------------

enum eDataType { dtPIN };

class cSmartCardDataConax : public cSmartCardData {
private:
  int IdLen(void) const { return 7; }
public:
  eDataType type;
  unsigned char id[7], pin[4];
  //
  cSmartCardDataConax(void);
  cSmartCardDataConax(eDataType Type, unsigned char *Id);
  virtual bool Parse(const char *line);
  virtual bool Matches(cSmartCardData *param);
  };

cSmartCardDataConax::cSmartCardDataConax(void)
:cSmartCardData(SC_ID)
{
  memset(id,0,sizeof(id));
}

cSmartCardDataConax::cSmartCardDataConax(eDataType Type, unsigned char *Id)
:cSmartCardData(SC_ID)
{
  type=Type;
  memset(id,0,sizeof(id));
  memcpy(id,Id,IdLen());
}

bool cSmartCardDataConax::Matches(cSmartCardData *param)
{
  cSmartCardDataConax *cd=(cSmartCardDataConax *)param;
  return cd->type==type && !memcmp(cd->id,id,IdLen());
}

bool cSmartCardDataConax::Parse(const char *line)
{
  line=skipspace(line);
  if(!strncasecmp(line,"PIN",3)) { type=dtPIN; line+=3; }
  else {
    PRINTF(L_CORE_LOAD,"smartcarddataconax: format error: datatype");
    return false;
    }
  line=skipspace(line);
  if(GetHex(line,id,IdLen())!=IdLen()) {
    PRINTF(L_CORE_LOAD,"smartcarddataconax: format error: serial");
    return false;
    }
  line=skipspace(line);
  if(type==dtPIN) {
    for(int i=0; i<4; i++) {
      if(!isdigit(*line)) {
        PRINTF(L_CORE_LOAD,"smartcarddataconax: format error: pin");
        return false;
        }
      pin[i]=*line++;
      }
    }
  return true;
}

// -- cSmartCardConax ----------------------------------------------------------

#define STDENTTAG 0x32
#define PPVENTTAG 0x39
#define ADDR_SIZE 7

struct stdent {
  unsigned short id;
  char name[16], date[4][16];
  unsigned int pbm[2];
  };

class cSmartCardConax : public cSmartCard, private cIdSet {
private:
  int sysId, cardVer, currency;
  unsigned char ua[ADDR_SIZE];
  //
  int GetLen(void);
public:
  cSmartCardConax(void);
  virtual bool Init(void);
  virtual bool Decode(const cEcmInfo *ecm, const unsigned char *data, unsigned char *cw);
  virtual bool Update(int pid, int caid, const unsigned char *data);
  virtual bool CanHandle(unsigned short SysId);
  };

static const struct StatusMsg msgs[] = {
  { { 0x90,0x00 }, "Instruction executed without errors", true },
  { { 0x90,0x11 }, "Bad instruction", false },
  { { 0x90,0x12 }, "Access denied, check your subscription", false },
  { { 0x90,0x13 }, "Bit error detected", false },
  { { 0x90,0x16 }, "Insuficient info", false },
  { { 0x94,0x00 }, "Instruction executed without errors, waiting for user interaction", true },
  { { 0x98,0xFF }, "Instruction accepted, data to be read", true },
  { { 0x9c,0xFF }, "Instruction accepted, data to be read", true },
  { { 0xFF,0xFF }, 0, false }
  };

static const struct CardConfig cardCfg = {
  SM_8E2,1000,100
  };

cSmartCardConax::cSmartCardConax(void)
:cSmartCard(&cardCfg,msgs)
{
  sysId=0; cardVer=0; currency=0;
  memset(ua,0,sizeof(ua));
}

bool cSmartCardConax::CanHandle(unsigned short SysId)
{
  return (SysId==sysId);
}

int cSmartCardConax::GetLen(void)
{
  return (sb[0]==0x98 || sb[0]==0x9C || (sb[0]==0x90 && sb[1]==0x00)) ? sb[1] : -1;
}

bool cSmartCardConax::Init(void)
{
  static const unsigned char cnxHist[] = { '0','B','0','0' }; // XXX is this always true ?

  sysId=0; cardVer=0; currency=0;
  memset(ua,0,sizeof(ua));
  ResetIdSet();
  if(atr->histLen<4 || memcmp(atr->hist, cnxHist, atr->histLen)) {
    PRINTF(L_SC_INIT,"doesn't look like a Conax card");
    return false;
    }

  infoStr.Begin();
  infoStr.Strcat("Conax smartcard\n");
  static unsigned char ins26[] = { 0xdd, 0x26, 0x00, 0x00, 0x03 };
  static unsigned char insc6[] = { 0xdd, 0xc6, 0x00, 0x00, 0x03 };
  static unsigned char insca[] = { 0xdd, 0xca, 0x00, 0x00, 0x00 };
  static const unsigned char hostVer[] = { 0x10,0x01,0x01 };  // host version
  unsigned char buff[MAX_LEN];
  int l;
  if(!IsoWrite(ins26,hostVer) || !Status() || (l=GetLen())<=0) {
    PRINTF(L_SC_ERROR,"card caid request failed");
    return false;
    }
  insca[4]=l;
  if(!IsoRead(insca,buff) || !Status()) {
    PRINTF(L_SC_ERROR,"card caid read failed");
    return false;
    }
  for(int i=0; i<l; i+=buff[i+1]+2) {
    switch(buff[i]) {
      case 0x20: cardVer=buff[i+2]; break;
      case 0x28: {
                 int s=(buff[i+2]<<8)+buff[i+3];
                 if(s!=sysId) CaidsChanged();
                 sysId=s;
                 break;
                 }
      case 0x2f: currency=(buff[i+2]<<8)+buff[i+3]; break;
      }
    }
  infoStr.Printf("Card v.%d Caid %04x\n",cardVer,sysId);
  PRINTF(L_SC_INIT,"card v.%d caid %04x",cardVer,sysId);
  snprintf(idStr,sizeof(idStr),"%s (V.%d)",SC_NAME,cardVer);

  static unsigned char ins82[] = { 0xdd,0x82,0x00,0x00,0x11 };
  static const unsigned char ins82Data[] = { 0x11,0x0f,0x01,0xb0,0x0f,0xff,0xff,0xfb,0x00,0x00,0x09,0x04,0x0b,0x00,0xe0,0x30,0x2b };
  if(!IsoWrite(ins82,ins82Data) || !Status() || (l=GetLen())<=0) {
    PRINTF(L_SC_ERROR,"card serial request failed");
    return false;
    }
  insca[4]=l;
  if(!IsoRead(insca,buff) || !Status()) {
    PRINTF(L_SC_ERROR,"card serial read failed");
    return false;
    }
  LBSTARTF(L_SC_INIT);
  LBPUT("card serials");
  for(int i=2; i<l; i+=buff[i+1]+2) {
    if(buff[i]==0x23 && buff[i+1]==ADDR_SIZE) { // Card Address
      if(CheckNull(&buff[i+2],4)) {
        AddProv(new cProviderConax(&buff[i+2]));
        LBPUT(" prov");
        }
      else {
        SetCard(new cCardConax(&buff[i+2]));
        memcpy(ua,&buff[i+2],sizeof(ua));
        LBPUT(" card");
        }
      char str[ADDR_SIZE*2+4];
      LBPUT(" %s",HexStr(str,&buff[i+2],ADDR_SIZE));
      }
    }
  LBEND();

  static const unsigned char stdEntits[] = { 0x1C,0x01,0x00 };
  if(IsoWrite(insc6,stdEntits) && Status() && (l=GetLen())>0) {
    infoStr.Printf("Subscriptions:\n");
    infoStr.Printf("|id  |name        |date             |\n");
    infoStr.Printf("+----+------------+-----------------+\n");

    do {
      insca[4]=l;
      if(!IsoRead(insca,buff) || !Status()) {
        PRINTF(L_SC_ERROR,"failed to read entitlements");
        break;
        }
      for(int i=0; i<l; i+=buff[i+1]+2) {
        if(buff[i]!=STDENTTAG) {
          PRINTF(L_SC_ERROR,"bad entitlement format %02x != %02x",buff[i],STDENTTAG);
          break;
          }

        struct stdent ent;
        memset(&ent,0,sizeof(ent));
        ent.id=(buff[i+2]<<8)|buff[i+3];

        int date=0, pbm=0;
        int max=i+buff[i+1]+2;
        for(int j=i+4; j<max; j+=buff[j+1]+2) {
          switch(buff[j]) {
            case 0x01: // prov. name
              snprintf(ent.name,sizeof(ent.name),"%.12s",&buff[j+2]);
              break;
            case 0x30: // date
              if(date<=3) {
                snprintf(ent.date[date],sizeof(ent.date[0]),"%02d.%02d.%02d",buff[j+2]&0x1F,buff[j+3]&0xF,(1990+((buff[j+3]>>4)+((buff[j+2]>>5)&0x7)*10))%100);
                date++;
                }
              break;
            case 0x20:
              if(pbm<=1) {
                ent.pbm[pbm]=(buff[j+2]<<24)|(buff[j+3]<<16)|(buff[j+4]<<8)|buff[j+5];
                pbm++;
                }
              break;
            }         
          }
        infoStr.Printf("|%04X|%s|%s-%s|\n",ent.id,ent.name,ent.date[0],ent.date[1]);
        infoStr.Printf("|    |            |%s-%s|\n",ent.date[2],ent.date[3]);
        }
      } while((l=GetLen())>0);
    }
  else
    PRINTF(L_SC_ERROR,"requesting entitlements failed");

/*
  static const unsigned char ppvEntits[] = { 0x1C,0x01,0x01 };
  if(IsoWrite(ins26,ppvEntits) && Status()) {
    while((l=GetLen())>0) {
      insca[4]=l;
      if(!IsoRead(insca,buff) || !Status()) {
        PRINTF(L_SC_ERROR,"failed to read PPV entitlements");
        break;
        }


      }
    }
  else
    PRINTF(L_SC_ERROR,"getting PPV entitlements failed");
*/

  infoStr.Finish();
  return true;
}

bool cSmartCardConax::Decode(const cEcmInfo *ecm, const unsigned char *data, unsigned char *cw)
{
  static unsigned char insa2[] = { 0xDD,0xA2,0x00,0x00,0x00 };
  static unsigned char insca[] = { 0xDD,0xCA,0x00,0x00,0x00 };
  static unsigned char insc8[] = { 0xDD,0xC8,0x00,0x00,0x07 };
  static unsigned char insc8data[] = { 0x1D,0x05,0x01,0x00,0x00,0x00,0x00 };

  int l;
  if((l=CheckSctLen(data,3))<=0) return false;
  unsigned char buff[MAX_LEN];
  buff[0]=0x14;
  buff[1]=l+1;
  buff[2]=0;
  memcpy(buff+3,data,l);

  insa2[4]=l+3;
  if(!IsoWrite(insa2,buff) || !Status() || (l=GetLen())<=0) return false;
  int gotIdx=0;
  do {
    insca[4]=l;
    if(!IsoRead(insca,buff) || !Status()) return false;
    for(int i=0; i<l; i+=buff[i+1]+2) {
      switch(buff[i]) {
        case 0x25:
          if(buff[i+1]>=13) {
            int idx=buff[i+4];
            if(idx<=1) {
              gotIdx|=(1<<idx);
              memcpy(cw+idx*8,&buff[i+7],8);
              }
            }
          break;
        case 0x31:
          {
          if(buff[i+1]==0x02 && (buff[i+2]==0x00 || buff[i+2]==0x40) && buff[i+3]==0x00)
            break;
          // PIN required
          cSmartCardDataConax cd(dtPIN,ua);
          cSmartCardDataConax *entry=(cSmartCardDataConax *)smartcards.FindCardData(&cd);
          if(entry) {
            memcpy(&insc8data[3],entry->pin,4);
            if(!IsoWrite(insc8,insc8data) || !Status()) {
              PRINTF(L_SC_ERROR,"failed to send PIN");
              return false;
              }
            // resend ECM request
            if(!IsoWrite(insa2,buff) || !Status() || GetLen()<=0) return false;
            gotIdx=0;
            l=0; continue; // abort loop
            }
          else {
            PRINTF(L_SC_ERROR,"no PIN available");
            return false;
            }
          break;
          }
        }
      }
    } while((l=GetLen())>0);
  if(gotIdx!=3) PRINTF(L_SC_ERROR,"strange, only got index %d cw. Failing... (this may be a bug)",gotIdx==1?0:1);
  return gotIdx==3;
}

bool cSmartCardConax::Update(int pid, int caid, const unsigned char *data)
{
  static unsigned char ins84[] = { 0xdd,0x84,0x00,0x00,0x00 };
  unsigned char buff[MAX_LEN];

  if(MatchEMM(data)) {
    int l;
    if((l=CheckSctLen(data,2))>0) {
      buff[0]=0x12; buff[1]=l;
      memcpy(&buff[2],data,l);
      ins84[4]=l+2;
      if(IsoWrite(ins84,buff) && !Status()) return true;
      }
    }
  return false;
}

// -- cSmartCardLinkConax -------------------------------------------------------------

class cSmartCardLinkConax : public cSmartCardLink {
public:
  cSmartCardLinkConax(void):cSmartCardLink(SC_NAME,SC_ID) {}
  virtual cSmartCard *Create(void) { return new cSmartCardConax(); }
  };

static cSmartCardLinkConax staticScInit;
