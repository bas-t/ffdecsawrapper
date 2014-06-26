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

#ifndef ___SYSTEM_CC_H
#define ___SYSTEM_CC_H

#include <vdr/thread.h>
#include "system.h"
#include "network.h"
#include "misc.h"
#include "log.h"

#define L_CC          6
#define L_CC_CORE     LCLASS(L_CC,0x2)
#define L_CC_LOGIN    LCLASS(L_CC,0x4)
#define L_CC_ECM      LCLASS(L_CC,0x8)
#define L_CC_EMM      LCLASS(L_CC,0x10)
#define L_CC_CAMD     LCLASS(L_CC,0x20)
#define L_CC_CAMD35   LCLASS(L_CC,0x40)
#define L_CC_CAMDEXTR LCLASS(L_CC,0x80)
#define L_CC_RDGD     LCLASS(L_CC,0x100)
#define L_CC_NEWCAMD  LCLASS(L_CC,0x200)
#define L_CC_GBOX     LCLASS(L_CC,0x400)
#define L_CC_CCCAM    LCLASS(L_CC,0x800)
#define L_CC_CCCAM2   LCLASS(L_CC,0x1000)
#define L_CC_CCCAM2DT LCLASS(L_CC,0x2000)
#define L_CC_CCCAM2SH LCLASS(L_CC,0x4000)
#define L_CC_CCCAM2EX LCLASS(L_CC,0x8000)

#define L_CC_ALL      LALL(L_CC_CCCAM2EX)

// ----------------------------------------------------------------

class cEcmInfo;
class cNetSocket;
class cCardClients;
class cSystemCardClient;

// ----------------------------------------------------------------

#define MAX_CC_CAID 16

class cCardClient : public cStructItem, protected cMutex {
friend class cSystemCardClient;
protected:
  cNetSocket so;
  const char *name;
  char hostname[64];
  int port;
  int emmAllowed;
  int emmCaid[MAX_CC_CAID], emmMask[MAX_CC_CAID], numCaid;
  cMsgCache msECM, msEMM;
  //
  bool ParseStdConfig(const char *config, int *num=0);
  virtual bool SendMsg(const unsigned char *data, int len);
  virtual int RecvMsg(unsigned char *data, int len, int to=-1);
  virtual void Logout(void);
  void CaidsChanged(void);
public:
  cCardClient(const char *Name);
  virtual bool Init(const char *config)=0;
  virtual bool Login(void)=0;
  virtual bool Immediate(void);
  virtual bool CanHandle(unsigned short SysId);
  virtual bool ProcessECM(const cEcmInfo *ecm, const unsigned char *data, unsigned char *cw)=0;
  virtual bool ProcessEMM(int caSys, const unsigned char *data) { return false; }
  const char *Name(void) { return name; }
  };

// ----------------------------------------------------------------

class cCardClientLink {
friend class cCardClients;
private:
  cCardClientLink *next;
protected:
  const char *name;
public:
  cCardClientLink(const char *Name);
  virtual ~cCardClientLink() {};
  virtual cCardClient *Create(void)=0;
  };

// ----------------------------------------------------------------

template<class CC> class cCardClientLinkReg : public cCardClientLink {
public:
  cCardClientLinkReg(const char *Name):cCardClientLink(Name) {}
  virtual cCardClient *Create(void) { return new CC(name); }
  };

#endif //___SYSTEM_CC_H
