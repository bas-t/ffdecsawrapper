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

#ifndef ___DEVICE_H
#define ___DEVICE_H

#include <linux/dvb/ca.h>
#include <ffdecsawrapper/dvbdevice.h>
#include <ffdecsawrapper/thread.h>
#include "misc.h"

typedef int caid_t;

class cCam;
class cDeCSA;
class cDeCsaTSBuffer;
class cScCiAdapter;

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

class cScDevices : public cDvbDevice {
private:
  static int budget;
public:
#if APIVERSNUM >= 10711 // make compiler happy. These are never used!
  cScDevices(void):cDvbDevice(0,0) {}
#else
  cScDevices(void):cDvbDevice(0) {}
#endif
  static void OnPluginLoad(void);
  static void OnPluginUnload(void);
  static bool Initialize(void);
  static void Startup(void);
  static void Shutdown(void);
  static void SetForceBudget(int n);
  static bool ForceBudget(int n);
  static void DvbName(const char *Name, int a, int f, char *buffer, int len);
  static int DvbOpen(const char *Name, int a, int f, int Mode, bool ReportError=false);
  };

// ----------------------------------------------------------------

class cScDevice : public cDvbDevice {
friend class cScDevices;
private:
  cDeCSA *decsa;
  cDeCsaTSBuffer *tsBuffer;
  cMutex tsMutex;
  cScCiAdapter *ciadapter;
  cCiAdapter *hwciadapter;
  cCam *cam;
  int fd_dvr, fd_ca, fd_ca2;
  bool softcsa, fullts;
  cMutex cafdMutex;
  cTimeMs lastDump;
  //
#ifndef FFDECSAWRAPPER
  void LateInit(void);
  void EarlyShutdown(void);
  bool ScActive(void);
#endif //FFDECSAWRAPPER
  //
protected:
#ifndef FFDECSAWRAPPER
  virtual bool Ready(void);
  virtual bool SetPid(cPidHandle *Handle, int Type, bool On);
  virtual bool SetChannelDevice(const cChannel *Channel, bool LiveView);
  virtual bool OpenDvr(void);
  virtual void CloseDvr(void);
  virtual bool GetTSPacket(uchar *&Data);
#endif //FFDECSAWRAPPER
public:
  cScDevice(int Adapter, int Frontend, int cafd);
  ~cScDevice();
#ifndef FFDECSAWRAPPER
  virtual bool HasCi(void);
#endif //FFDECSAWRAPPER
  virtual bool SetCaDescr(ca_descr_t *ca_descr, bool initial);
  virtual bool SetCaPid(ca_pid_t *ca_pid);
  int FilterHandle(void);
  void DumpAV7110(void);
  cCam *Cam(void) { return cam; }
  bool SoftCSA(bool live);
  void CaidsChanged(void);
  };

#endif // ___DEVICE_H
