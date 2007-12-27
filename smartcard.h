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

#ifndef ___SMARTCARD_H
#define ___SMARTCARD_H

#include <vdr/thread.h>
#include "data.h"
#include "misc.h"

// ----------------------------------------------------------------

#define MAX_LEN     256        // max. response length
#define CMD_LEN     5          // command length
#define INS_IDX     1          // INS index
#define LEN_IDX     4          // LEN index

#define SB_LEN      2          // status byte (SB) len
#define MAX_ATR_LEN 33         // max. ATR length
#define MAX_HIST    15         // max. number of historical characters
#define IDSTR_LEN   25         // lenght of card identify string

#define MAKE_SC_ID(a,b,c,d) (((a)<<24)+((b)<<16)+((c)<<8)+(d))

// ----------------------------------------------------------------

#define DEBUG_ISO // debug iso/smartcard functions
#ifdef DEBUG_ISO
#define di(x) { (x); }
#else
#define di(x) ;
#endif

#ifdef DEBUG_ISO
#define DUMP(data,len) { const int l=(len); \
                         for(int i=0 ; i<l ; i++) printf(" %02x",*((data)+i)); \
                         printf("\n"); }
#else
#define DUMP(data,len) ;
#endif

// ----------------------------------------------------------------

class cSerial;
class cSmartCards;

// ----------------------------------------------------------------

class cInfoStr : public cLineBuff {
private:
  cMutex mutex;
  char *current;
public:
  cInfoStr(void);
  ~cInfoStr();
  // Query API
  bool Get(char *buff, int len);
  // Construct API
  void Begin(void);
  void Finish();
  };

// ----------------------------------------------------------------

#define SM_NONE 0
#define SM_8E2  1
#define SM_8O2  2
#define SM_8N2  3
#define SM_MAX  4
#define SM_MASK 0x1F
#define SM_1SB  0x80

#define SM_DIRECT   0
#define SM_INDIRECT 1

struct CardConfig {
  int SerMode;
  int workTO, serTO; // values in ms
  };

struct StatusMsg {
  unsigned char sb[SB_LEN];
  char *message; 
  bool retval;
  };

struct Atr {
  int T, F, N, WI, BWI, CWI, TA1, Tspec;
  float D;
  int wwt, bwt;
  int atrLen, histLen;
  int convention;
  unsigned char atr[MAX_ATR_LEN];
  unsigned char hist[MAX_HIST];
  };

class cSmartCard : protected cMutex {
friend class cSmartCards;
private:
  cSerial *ser;
  const struct CardConfig *cfg;
  const struct StatusMsg *msg;
  bool cardUp, needsReset;
  int id;
  //
  int Procedure(unsigned char ins, int restLen);
  int Read(unsigned char *data, int len, int to=0);
  int Write(const unsigned char *data, int len);
protected:
  const struct Atr *atr;
  unsigned char sb[SB_LEN];
  char idStr[IDSTR_LEN];
  cInfoStr infoStr;
  //
  int SerRead(unsigned char *data, int len, int to=0);
  int SerWrite(const unsigned char *data, int len);
  bool IsoRead(const unsigned char *cmd, unsigned char *data);
  bool IsoWrite(const unsigned char *cmd, const unsigned char *data);
  bool Status(void);
  bool Test(bool res);
  int CheckSctLen(const unsigned char *data, int off);
  void TriggerReset(void) { needsReset=true; }
  static void Invert(unsigned char *data, int n);
public:
  cSmartCard(const struct CardConfig *Cfg, const struct StatusMsg *Msg);
  virtual ~cSmartCard() {};
  virtual bool Init(void)=0;
  virtual bool Decode(const cEcmInfo *ecm, const unsigned char *data, unsigned char *cw)=0;
  virtual bool Update(int pid, int caid, const unsigned char *data) { return false; }
  virtual bool CanHandle(unsigned short CaId) { return true; }
  //
  bool Setup(cSerial *Ser, const struct Atr *Atr, int Id);
  static bool ParseAtr(struct Atr *atr, int id, int clock);
  bool CardUp(void) { return cardUp; }
  bool NeedsReset(void) { return needsReset; }
  bool GetCardIdStr(char *str, int len);
  bool GetCardInfoStr(char *str, int len);
  };

// ----------------------------------------------------------------

class cSmartCardData : public cSimpleItem {
friend class cSmartCards;
protected:
  int ident;
public:
  cSmartCardData(int Ident);
  virtual ~cSmartCardData() {}
  virtual bool Parse(const char *line)=0;
  virtual bool Matches(cSmartCardData *cmp)=0;
  };

// ----------------------------------------------------------------

class cSmartCardLink {
friend class cSmartCards;
private:
  cSmartCardLink *next;
  const char *name;
  int id;
public:
  cSmartCardLink(const char *Name, int Id);
  virtual ~cSmartCardLink() {}
  virtual cSmartCard *Create(void)=0;
  virtual cSmartCardData *CreateData(void) { return 0; }
  int ID(void) { return id; }
  };

// ----------------------------------------------------------------

#define MAX_PORTS 4

struct Port {
  cSerial *Serial;
  bool Dead;
  cSmartCard *Card;
  int CardId, UseCount, PortNum, Clock;
  struct Atr Atr;
  };

class cSmartCards : private cThread, private cConfRead {
friend class cSmartCardLink;
private:
  static cSmartCardLink *first;
  cMutex mutex;
  cCondVar cond;
  struct Port ports[MAX_PORTS];
  bool firstRun;
  cSimpleList<cSmartCardData> dataList;
  //
  static void Register(cSmartCardLink *scl);
  bool CardInserted(cSerial *ser);
  bool CardReset(struct Port *port);
  int Reset(struct Port *port);
  bool DoPTS(struct Port *port);
  void SetPort(struct Port *port, cSmartCard *sc, int id, bool dead);
protected:
  virtual void Action(void);
  virtual bool ParseLine(const char *line, bool fromCache);
public:
  cSmartCards(void);
  void Shutdown(void);
  // to be called ONLY from a system class!
  bool HaveCard(int id);
  cSmartCard *LockCard(int id);
  void ReleaseCard(cSmartCard *sc);
  cSmartCardData *FindCardData(cSmartCardData *param);
  // to be called ONLY from frontend thread!
  bool AddPort(const char *devName, bool invCD, bool invRST, int clock);
  void LaunchWatcher(void);
  void LoadData(const char *cfgdir);
  bool ListCard(int num, char *str, int len);
  bool CardInfo(int num, char *str, int len);
  void CardReset(int num);
  };

extern cSmartCards smartcards;

#endif //___SMARTCARD_H
