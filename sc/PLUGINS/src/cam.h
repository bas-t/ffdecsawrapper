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

#ifndef ___CAM_H
#define ___CAM_H

#include <ffdecsawrapper/thread.h>
#include "data.h"
#include "misc.h"

class cChannel;

class cEcmHandler;
class cEcmData;
class cLogger;
class cHookManager;
class cLogHook;
class cScDevice;
class cPrg;

// ----------------------------------------------------------------

class cEcmCache : public cStructListPlain<cEcmData> {
private:
  cEcmData *Exists(cEcmInfo *e);
protected:
  virtual bool ParseLinePlain(const char *line);
public:
  cEcmCache(void);
  void New(cEcmInfo *e);
  int GetCached(cSimpleList<cEcmInfo> *list, int sid, int Source, int Transponder);
  void Delete(cEcmInfo *e);
  void Flush(void);
  };

extern cEcmCache ecmcache;

// ----------------------------------------------------------------

#define MAX_CW_IDX        16
#define MAX_SPLIT_SID     16

class cCam : private cMutex {
private:
  int cardNum;
  cScDevice *device;
  cSimpleList<cEcmHandler> handlerList;
  cLogger *logger;
  cHookManager *hookman;
  int source, transponder, liveVpid, liveApid;
  int splitSid[MAX_SPLIT_SID+1];
  unsigned char indexMap[MAX_CW_IDX], lastCW[MAX_CW_IDX][2*8];
  //
  cEcmHandler *GetHandler(int sid, bool needZero, bool noshift);
  void RemHandler(cEcmHandler *handler);
  int GetFreeIndex(void);
  void LogStartup(void);
public:
  cCam(cScDevice *dev, int CardNum);
  virtual ~cCam();
  // EcmHandler API
  void WriteCW(int index, unsigned char *cw, bool force);
  void SetCWIndex(int pid, int index);
  void DumpAV7110(void);
  void LogEcmStatus(const cEcmInfo *ecm, bool on);
  void AddHook(cLogHook *hook);
  bool TriggerHook(int id);
  // Plugin API
  bool Active(bool log);
  void HouseKeeping(void);
  void Tune(const cChannel *channel);
  void PostTune(void);
  void SetPid(int type, int pid, bool on);
  void Stop(void);
  void AddPrg(cPrg *prg);
  bool HasPrg(int prg);
  char *CurrentKeyStr(int num);
  //
  bool IsSoftCSA(bool live);
  };

void LogStatsDown(void);

#endif // ___CAM_H
