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

#include <linux/dvb/ca.h>
#include <vdr/ci.h>
#include <vdr/thread.h>
#include "data.h"
#include "misc.h"

class cChannel;
class cRingBufferLinear;
class cDevice;

class cEcmHandler;
class cEcmData;
class cLogger;
class cHookManager;
class cLogHook;
class cScCamSlot;
class cDeCSA;
class cPrg;
class cScDevicePlugin;

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

#ifndef SASC
class cCiFrame {
private:
  cRingBufferLinear *rb;
  unsigned char *mem;
  int len, alen, glen;
public:
  cCiFrame(void);
  ~cCiFrame();
  void SetRb(cRingBufferLinear *Rb) { rb=Rb; }
  unsigned char *GetBuff(int l);
  void Put(void);
  unsigned char *Get(int &l);
  void Del(void);
  int Avail(void);
  };
#endif //!SASC

// ----------------------------------------------------------------

class cCaDescr {
private:
  unsigned char *descr;
  int len;
public:
  cCaDescr(void);
  cCaDescr(const cCaDescr &arg);
  ~cCaDescr();
  const unsigned char *Get(int &l) const;
  void Set(const cCaDescr *d);
  void Set(const unsigned char *de, int l);
  void Clear(void);
  bool operator== (const cCaDescr &arg) const;
  void Join(const cCaDescr *cd, bool rev=false);
  cString ToString(void);
  };

// ----------------------------------------------------------------

class cPrgPid : public cSimpleItem {
private:
  bool proc;
public:
  int type, pid;
  cCaDescr caDescr;
  //
  cPrgPid(int Type, int Pid) { type=Type; pid=Pid; proc=false; }
  bool Proc(void) const { return proc; }
  void Proc(bool is) { proc=is; };
  };

// ----------------------------------------------------------------

class cPrg : public cSimpleItem {
private:
  bool isUpdate, pidCaDescr;
  //
  void Setup(void);
public:
  int sid, source, transponder;
  cSimpleList<cPrgPid> pids;
  cCaDescr caDescr;
  //
  cPrg(void);
  cPrg(int Sid, bool IsUpdate);
  bool IsUpdate(void) const { return isUpdate; }
  bool HasPidCaDescr(void) const { return pidCaDescr; }
  void SetPidCaDescr(bool val) { pidCaDescr=val; }
  bool SimplifyCaDescr(void);
  void DumpCaDescr(int c);
  };

// ----------------------------------------------------------------

typedef int caid_t;

#define MAX_CI_SLOTS      8
#ifdef VDR_MAXCAID
#define MAX_CI_SLOT_CAIDS VDR_MAXCAID
#else
#define MAX_CI_SLOT_CAIDS 16
#endif

#define MAX_CW_IDX        16
#define MAX_SPLIT_SID     16

#ifndef SASC
class cCam : public cCiAdapter, public cSimpleItem {
#else
class cCam : public cSimpleItem {
#endif
private:
  cDevice *device;
  const char *devId;
  int adapter, frontend;
#ifndef SASC
  cMutex ciMutex;
  cRingBufferLinear *rb;
  cScCamSlot *slots[MAX_CI_SLOTS];
  cCiFrame frame;
  //
  cDeCSA *decsa;
#endif
  cScDevicePlugin *devplugin;
  bool softcsa, fullts;
  //
  cTimeMs caidTimer, triggerTimer;
  int version[MAX_CI_SLOTS];
  caid_t caids[MAX_CI_SLOTS][MAX_CI_SLOT_CAIDS+1];
  int tcid;
  bool rebuildcaids;
  //
  cTimeMs readTimer, writeTimer;
  //
  cMutex camMutex;
  cSimpleList<cEcmHandler> handlerList;
  cLogger *logger;
  cHookManager *hookman;
  int source, transponder, liveVpid, liveApid;
  int splitSid[MAX_SPLIT_SID+1];
  unsigned char indexMap[MAX_CW_IDX], lastCW[MAX_CW_IDX][2*8];
  //
  void BuildCaids(bool force);
  cEcmHandler *GetHandler(int sid, bool needZero, bool noshift);
  void RemHandler(cEcmHandler *handler);
  int GetFreeIndex(void);
  void LogStartup(void);
protected:
#ifndef SASC
  virtual int Read(unsigned char *Buffer, int MaxLength);
  virtual void Write(const unsigned char *Buffer, int Length);
  virtual bool Reset(int Slot);
  virtual eModuleStatus ModuleStatus(int Slot);
  virtual bool Assign(cDevice *Device, bool Query=false);
#endif
public:
  cCam(cDevice *Device, int Adapter, int Frontend, const char *DevId, cScDevicePlugin *DevPlugin, bool SoftCSA, bool FullTS);
  virtual ~cCam();
  // CI adapter API
  int GetCaids(int slot, unsigned short *Caids, int max);
  void CaidsChanged(void);
  virtual bool SetCaDescr(ca_descr_t *ca_descr, bool initial);
  virtual bool SetCaPid(ca_pid_t *ca_pid);
  void Stop(void);
  void AddPrg(cPrg *prg);
  bool HasPrg(int prg);
  // EcmHandler API
  void WriteCW(int index, unsigned char *cw, bool force);
  void SetCWIndex(int pid, int index);
  void DumpAV7110(void);
  bool IsSoftCSA(bool live);
  int Adapter(void) { return adapter; }
  int Frontend(void) { return frontend; }
  // System API
  void LogEcmStatus(const cEcmInfo *ecm, bool on);
  void AddHook(cLogHook *hook);
  bool TriggerHook(int id);
  // Plugin API
  bool Active(bool log);
  void HouseKeeping(void);
  void Tune(const cChannel *channel);
  void PostTune(void);
  void SetPid(int type, int pid, bool on);
  char *CurrentKeyStr(int num, const char **id);
#ifndef SASC
  bool OwnSlot(const cCamSlot *slot) const;
  cDeCSA *DeCSA(void) const { return decsa; }
#endif
  };

void LogStatsDown(void);

// ----------------------------------------------------------------

#ifndef SASC

#define MAX_CSA_PIDS 8192
#define MAX_CSA_IDX  16

class cDeCSA {
private:
  int cs;
  unsigned char **range, *lastData;
  unsigned char pidmap[MAX_CSA_PIDS];
  void *keys[MAX_CSA_IDX];
  unsigned int even_odd[MAX_CSA_IDX], flags[MAX_CSA_IDX];
  cMutex mutex;
  cCondVar wait;
  cTimeMs stall;
  bool active;
  const char *devId;
  //
  bool GetKeyStruct(int idx);
  void ResetState(void);
public:
  cDeCSA(const char *DevId);
  ~cDeCSA();
  bool Decrypt(unsigned char *data, int len, bool force);
  bool SetDescr(ca_descr_t *ca_descr, bool initial);
  bool SetCaPid(ca_pid_t *ca_pid);
  void SetActive(bool on);
  };
#endif //!SASC

#endif // ___CAM_H
