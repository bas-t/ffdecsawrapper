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

#include <vdr/dvbdevice.h>
#include <vdr/thread.h>

class cCam;
class cDeCSA;
class cDeCsaTSBuffer;
class cScCiAdapter;

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
  cDeCsaTSBuffer *tsBuffer;
  cMutex tsMutex;
  cCam *cam;
  cCiAdapter *hwciadapter;
  int fd_dvr, fd_ca, fd_ca2;
  bool softcsa, fullts;
  char devId[8];
  //
#ifndef SASC
  void LateInit(void);
  void EarlyShutdown(void);
  bool ScActive(void);
#endif //SASC
  //
protected:
#ifndef SASC
  virtual bool Ready(void);
  virtual bool SetPid(cPidHandle *Handle, int Type, bool On);
  virtual bool SetChannelDevice(const cChannel *Channel, bool LiveView);
  virtual bool OpenDvr(void);
  virtual void CloseDvr(void);
  virtual bool GetTSPacket(uchar *&Data);
#endif //SASC
public:
  cScDevice(int Adapter, int Frontend, int cafd);
  ~cScDevice();
#ifndef SASC
  virtual bool HasCi(void);
#endif //SASC
  };

#endif // ___DEVICE_H
