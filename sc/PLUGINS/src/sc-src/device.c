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

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <dlfcn.h>

#include <vdr/channels.h>
#include <vdr/ci.h>
#include <vdr/dvbdevice.h>
#include <vdr/thread.h>

#ifndef SASC
#include <vdr/dvbci.h>
#include "FFdecsa/FFdecsa.h"
#endif //!SASC

#include "device.h"
#include "cam.h"
#include "scsetup.h"
#include "system.h"
#include "data.h"
#include "override.h"
#include "misc.h"
#include "log-core.h"

// -- cDeCsaTSBuffer -----------------------------------------------------------

#ifndef SASC

cDeCsaTSBuffer::cDeCsaTSBuffer(int File, int Size, int CardIndex, cDeCSA *DeCsa, bool ScActive)
{
  SetDescription("TS buffer on device %d", CardIndex);
  f=File; size=Size; cardIndex=CardIndex; decsa=DeCsa;
  delivered=false;
  ringBuffer=new cRingBufferLinear(Size,TS_SIZE,true,"FFdecsa-TS");
  ringBuffer->SetTimeouts(100,100);
  if(decsa) decsa->SetActive(true);
  SetActive(ScActive);
  Start();
}

cDeCsaTSBuffer::~cDeCsaTSBuffer()
{
  Cancel(3);
  if(decsa) decsa->SetActive(false);
  delete ringBuffer;
}

void cDeCsaTSBuffer::SetActive(bool ScActive)
{
  scActive=ScActive;
}

void cDeCsaTSBuffer::Action(void)
{
  if(ringBuffer) {
    bool firstRead=true;
    cPoller Poller(f);
    while(Running()) {
      if(firstRead || Poller.Poll(100)) {
        firstRead=false;
        int r=ringBuffer->Read(f);
        if(r<0 && FATALERRNO) {
          if(errno==EOVERFLOW)
            esyslog("ERROR: driver buffer overflow on device %d",cardIndex);
          else { LOG_ERROR; break; }
          }
        }
      }
    }
}

uchar *cDeCsaTSBuffer::Get(void)
{
  int Count=0;
  if(delivered) { ringBuffer->Del(TS_SIZE); delivered=false; }
  uchar *p=ringBuffer->Get(Count);
  if(p && Count>=TS_SIZE) {
    if(*p!=TS_SYNC_BYTE) {
      for(int i=1; i<Count; i++)
        if(p[i]==TS_SYNC_BYTE &&
           (i+TS_SIZE==Count || (i+TS_SIZE>Count && p[i+TS_SIZE]==TS_SYNC_BYTE)) ) { Count=i; break; }
      ringBuffer->Del(Count);
      esyslog("ERROR: skipped %d bytes to sync on TS packet on device %d",Count,cardIndex);
      return NULL;
      }

    if(scActive && (p[3]&0xC0)) {
      if(decsa) {
        if(!decsa->Decrypt(p,Count,false)) {
          cCondWait::SleepMs(20);
          return NULL;
          }
        }
      else p[3]&=~0xC0; // FF hack
      }

    delivered=true;
    return p;
    }
  return NULL;
}

#endif //!SASC

// --- cScDevicePlugin ---------------------------------------------------------

static cSimpleList<cScDevicePlugin> devplugins;

cScDevicePlugin::cScDevicePlugin(void)
{
  devplugins.Add(this);
}

cScDevicePlugin::~cScDevicePlugin()
{
  devplugins.Del(this,false);
}

// -- cScDvbDevice -------------------------------------------------------------

#ifndef SASC

#define SCDEVICE cScDvbDevice
#define DVBDEVICE cDvbDevice
#if APIVERSNUM < 10711
#define OWN_FULLTS
#define OWN_SETCA
#endif
#include "device-tmpl.c"

#if APIVERSNUM < 10711
bool cScDvbDevice::CheckFullTs(void)
{
  return IsPrimaryDevice() && HasDecoder();
}

bool cScDvbDevice::SetCaDescr(ca_descr_t *ca_descr, bool initial)
{
  cMutexLock lock(&cafdMutex);
  return ioctl(fd_ca,CA_SET_DESCR,ca_descr)>=0;
}

bool cScDvbDevice::SetCaPid(ca_pid_t *ca_pid)
{
  cMutexLock lock(&cafdMutex);
  return ioctl(fd_ca,CA_SET_PID,ca_pid)>=0;
}
#endif //APIVERSNUM < 10711

// -- cScDvbDevicePlugin -------------------------------------------------------

class cScDvbDevicePlugin : public cScDevicePlugin {
public:
  virtual cDevice *Probe(int Adapter, int Frontend, uint32_t SubSystemId);
  virtual bool LateInit(cDevice *dev);
  virtual bool EarlyShutdown(cDevice *dev);
#if APIVERSNUM < 10711
  virtual bool SetCaDescr(cDevice *dev, ca_descr_t *ca_descr, bool initial);
  virtual bool SetCaPid(cDevice *dev, ca_pid_t *ca_pid);
#endif
  };

cDevice *cScDvbDevicePlugin::Probe(int Adapter, int Frontend, uint32_t SubSystemId)
{
  PRINTF(L_GEN_DEBUG,"creating standard device %d/%d",Adapter,Frontend);
  return new cScDvbDevice(this,Adapter,Frontend,cScDevices::DvbOpen(DEV_DVB_CA,Adapter,Frontend,O_RDWR));
}

bool cScDvbDevicePlugin::LateInit(cDevice *dev)
{
  cScDvbDevice *d=dynamic_cast<cScDvbDevice *>(dev);
  if(d) d->LateInit();
  return d!=0;
}

bool cScDvbDevicePlugin::EarlyShutdown(cDevice *dev)
{
  cScDvbDevice *d=dynamic_cast<cScDvbDevice *>(dev);
  if(d) d->EarlyShutdown();
  return d!=0;
}

#if APIVERSNUM < 10711
bool cScDvbDevicePlugin::SetCaDescr(cDevice *dev, ca_descr_t *ca_descr, bool initial)
{
  cScDvbDevice *d=dynamic_cast<cScDvbDevice *>(dev);
  if(d) return d->SetCaDescr(ca_descr,initial);
  return false;
}

bool cScDvbDevicePlugin::SetCaPid(cDevice *dev, ca_pid_t *ca_pid)
{
  cScDvbDevice *d=dynamic_cast<cScDvbDevice *>(dev);
  if(d) return d->SetCaPid(ca_pid);
  return d!=0;
}
#endif //APIVERSNUM < 10711

// --- cScDeviceProbe ----------------------------------------------------------

#if APIVERSNUM >= 10711

class cScDeviceProbe : public cDvbDeviceProbe {
private:
  static cScDeviceProbe *probe;
public:
  virtual bool Probe(int Adapter, int Frontend);
  static void Install(void);
  static void Remove(void);
#if APIVERSNUM < 10719
  uint32_t GetSubsystemId(int Adapter, int Frontend);
#endif
  };

cScDeviceProbe *cScDeviceProbe::probe=0;

void cScDeviceProbe::Install(void)
{
  if(!probe) probe=new cScDeviceProbe;
}

void cScDeviceProbe::Remove(void)
{
  delete probe; probe=0;
}

bool cScDeviceProbe::Probe(int Adapter, int Frontend)
{
  uint32_t subid=GetSubsystemId(Adapter,Frontend);
  PRINTF(L_GEN_DEBUG,"capturing device %d/%d (subsystem ID %08x)",Adapter,Frontend,subid);
  for(cScDevicePlugin *dp=devplugins.First(); dp; dp=devplugins.Next(dp))
    if(dp->Probe(Adapter,Frontend,subid)) return true;
  return false;
}

#if APIVERSNUM < 10719
uint32_t cScDeviceProbe::GetSubsystemId(int Adapter, int Frontend)
{
  cString FileName;
  cReadLine ReadLine;
  FILE *f = NULL;
  uint32_t SubsystemId = 0;
  FileName = cString::sprintf("/sys/class/dvb/dvb%d.frontend%d/device/subsystem_vendor", Adapter, Frontend);
  if ((f = fopen(FileName, "r")) != NULL) {
     if (char *s = ReadLine.Read(f))
        SubsystemId = strtoul(s, NULL, 0) << 16;
     fclose(f);
     }
  FileName = cString::sprintf("/sys/class/dvb/dvb%d.frontend%d/device/subsystem_device", Adapter, Frontend);
  if ((f = fopen(FileName, "r")) != NULL) {
     if (char *s = ReadLine.Read(f))
        SubsystemId |= strtoul(s, NULL, 0);
     fclose(f);
     }
  return SubsystemId;
}
#endif //APIVERSNUM < 10719

#endif //APIVERSNUM >= 10711

#endif //!SASC

// -- cScDevices ---------------------------------------------------------------

int cScDevices::budget=0;

void cScDevices::DvbName(const char *Name, int a, int f, char *buffer, int len)
{
  snprintf(buffer,len,"%s/%s%d/%s%d",DEV_DVB_BASE,DEV_DVB_ADAPTER,a,Name,f);
}

int cScDevices::DvbOpen(const char *Name, int a, int f, int Mode, bool ReportError)
{
  char FileName[128];
  DvbName(Name,a,f,FileName,sizeof(FileName));
  int fd=open(FileName,Mode);
  if(fd<0 && ReportError) LOG_ERROR_STR(FileName);
  return fd;
}

#ifndef SASC

#if APIVERSNUM < 10711
static int *vdr_nci=0, *vdr_ud=0, vdr_save_ud;
#endif

void cScDevices::OnPluginLoad(void)
{
#if APIVERSNUM >= 10711
  PRINTF(L_GEN_DEBUG,"using new 1.7.11+ capture code");
  cScDeviceProbe::Install();
#else
  PRINTF(L_GEN_DEBUG,"using old pre1.7.11 capture code");
/*
  This is an extremly ugly hack to access VDRs device scan parameters, which are
  protected in this context. Heavily dependant on the actual symbol names
  created by the compiler. May fail in any future version!

  To get the actual symbol names of your VDR binary you may use the command:
  objdump -T <path-to-vdr>/vdr | grep -E "(useDevice|nextCardIndex)"
  Insert the symbol names below.
*/
#if __GNUC__ >= 3
  vdr_nci=(int *)dlsym(RTLD_DEFAULT,"_ZN7cDevice13nextCardIndexE");
  vdr_ud =(int *)dlsym(RTLD_DEFAULT,"_ZN7cDevice9useDeviceE");
#else
  vdr_nci=(int *)dlsym(RTLD_DEFAULT,"_7cDevice.nextCardIndex");
  vdr_ud =(int *)dlsym(RTLD_DEFAULT,"_7cDevice.useDevice");
#endif
  if(vdr_nci && vdr_ud) { vdr_save_ud=*vdr_ud; *vdr_ud=1<<30; }
#endif
  // default device plugin must be last in the list
  new cScDvbDevicePlugin;
}

void cScDevices::OnPluginUnload(void)
{
#if APIVERSNUM >= 10711
  cScDeviceProbe::Remove();
#endif
}

bool cScDevices::Initialize(void)
{
#if APIVERSNUM >= 10711
  return true;
#else
  if(!vdr_nci || !vdr_ud) {
    PRINTF(L_GEN_ERROR,"Failed to locate VDR symbols. Plugin not operable");
    return false;
    }
  if(NumDevices()>0) {
    PRINTF(L_GEN_ERROR,"Number of devices != 0 on init. Put SC plugin first on VDR commandline! Aborting.");
    return false;
    }
  *vdr_nci=0; *vdr_ud=vdr_save_ud;

  int i, found=0;
  for(i=0; i<MAXDVBDEVICES; i++) {
    if(UseDevice(NextCardIndex())) {
      char name[128];
      cScDevices::DvbName(DEV_DVB_FRONTEND,i,0,name,sizeof(name));
      if(access(name,F_OK)==0) {
        PRINTF(L_GEN_DEBUG,"probing %s",name);
        int f=open(name,O_RDONLY);
        if(f>=0) {
          close(f);
          PRINTF(L_GEN_DEBUG,"capturing device %d",i);
          devplugins.First()->Probe(i,0,0);
          found++;
          }
        else {
          if(errno!=ENODEV && errno!=EINVAL) PRINTF(L_GEN_ERROR,"open %s failed: %s",name,strerror(errno));
          break;
          }
        }
      else {
        if(errno!=ENOENT) PRINTF(L_GEN_ERROR,"access %s failed: %s",name,strerror(errno));
        break;
        }
      }
    else NextCardIndex(1);
    }
  NextCardIndex(MAXDVBDEVICES-i);
  if(found>0) PRINTF(L_GEN_INFO,"captured %d video device%s",found,found>1 ? "s" : "");
  else PRINTF(L_GEN_INFO,"no DVB device captured");
  return found>0;
#endif
}

void cScDevices::Startup(void)
{
  if(ScSetup.ForceTransfer)
    SetTransferModeForDolbyDigital(2);
  for(int n=cDevice::NumDevices(); --n>=0;) {
    cDevice *dev=cDevice::GetDevice(n);
    for(cScDevicePlugin *dp=devplugins.First(); dp; dp=devplugins.Next(dp))
      if(dp->LateInit(dev)) break;
    }
}

void cScDevices::Shutdown(void)
{
  for(int n=cDevice::NumDevices(); --n>=0;) {
    cDevice *dev=cDevice::GetDevice(n);
    for(cScDevicePlugin *dp=devplugins.First(); dp; dp=devplugins.Next(dp))
      if(dp->EarlyShutdown(dev)) break;
    }
}

void cScDevices::SetForceBudget(int n)
{
   if(n>=0 && n<MAXDVBDEVICES) budget|=(1<<n);
}

bool cScDevices::ForceBudget(int n)
{
   return budget && (budget&(1<<n));
}

#else //!SASC

void cScDevices::OnPluginLoad(void) {}
void cScDevices::OnPluginUnload(void) {}
bool cScDevices::Initialize(void) { return true; }
void cScDevices::Startup(void) {}
void cScDevices::Shutdown(void) {}
void cScDevices::SetForceBudget(int n) {}
bool cScDevices::ForceBudget(int n) { return true; }

#endif //!SASC
