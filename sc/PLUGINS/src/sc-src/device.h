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
#include <vdr/dvbdevice.h>
#include <vdr/thread.h>
#include "misc.h"

class cDeCSA;

// ----------------------------------------------------------------

#if APIVERSNUM < 10723
#define DEV_DVB_BASE     "/dev/dvb"
#define DEV_DVB_ADAPTER  "adapter"
#define DEV_DVB_FRONTEND "frontend"
#define DEV_DVB_DVR      "dvr"
#define DEV_DVB_DEMUX    "demux"
#define DEV_DVB_CA       "ca"
#define DEV_DVB_OSD      "osd"
#endif

#if APIVERSNUM >= 10711
#define DVB_DEV_SPEC adapter,frontend
#else
#define DVB_DEV_SPEC CardIndex(),0
#endif

// ----------------------------------------------------------------

#ifndef SASC
class cDeCsaTSBuffer : public cThread {
private:
  int f;
  int cardIndex, size;
  bool delivered;
  cRingBufferLinear *ringBuffer;
  //
  cDeCSA *decsa;
  bool scActive;
  //
  virtual void Action(void);
public:
  cDeCsaTSBuffer(int File, int Size, int CardIndex, cDeCSA *DeCsa, bool ScActive);
  ~cDeCsaTSBuffer();
  uchar *Get(void);
  void SetActive(bool ScActive);
  };
#endif

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

class cScDevicePlugin : public cSimpleItem {
public:
  cScDevicePlugin(void);
  virtual ~cScDevicePlugin();
  virtual cDevice *Probe(int Adapter, int Frontend, uint32_t SubSystemId)=0;
  virtual bool LateInit(cDevice *dev)=0;
  virtual bool EarlyShutdown(cDevice *dev)=0;
  virtual bool SetCaDescr(cDevice *dev, ca_descr_t *ca_descr, bool initial) { return false; }
  virtual bool SetCaPid(cDevice *dev, ca_pid_t *ca_pid) { return false; }
  virtual bool DumpAV(cDevice *dev) { return false; }
  };

#endif // ___DEVICE_H
