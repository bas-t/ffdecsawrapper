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
#endif //SASC

#include "device.h"
#include "cam.h"
#include "scsetup.h"
#include "system.h"
#include "data.h"
#include "override.h"
#include "misc.h"
#include "log-core.h"

// -- cDeCsaTSBuffer -----------------------------------------------------------

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

// --- cScDeviceProbe ----------------------------------------------------------

#define DEV_DVB_ADAPTER  "/dev/dvb/adapter"
#define DEV_DVB_FRONTEND "frontend"
#define DEV_DVB_DVR      "dvr"
#define DEV_DVB_DEMUX    "demux"
#define DEV_DVB_CA       "ca"

#if APIVERSNUM >= 10711

class cScDeviceProbe : public cDvbDeviceProbe {
private:
  static cScDeviceProbe *probe;
public:
  virtual bool Probe(int Adapter, int Frontend);
  static void Install(void);
  static void Remove(void);
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
  PRINTF(L_GEN_DEBUG,"capturing device %d/%d",Adapter,Frontend);
  new cScDevice(Adapter,Frontend,cScDevices::DvbOpen(DEV_DVB_CA,Adapter,Frontend,O_RDWR));
  return true;
}
#endif

// -- cScDevices ---------------------------------------------------------------

int cScDevices::budget=0;

void cScDevices::DvbName(const char *Name, int a, int f, char *buffer, int len)
{
  snprintf(buffer,len,"%s%d/%s%d",DEV_DVB_ADAPTER,a,Name,f);
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
          new cScDevice(i,0,cScDevices::DvbOpen(DEV_DVB_CA,i,0,O_RDWR));
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
    cScDevice *dev=dynamic_cast<cScDevice *>(cDevice::GetDevice(n));
    if(dev) dev->LateInit();
    }
}

void cScDevices::Shutdown(void)
{
  for(int n=cDevice::NumDevices(); --n>=0;) {
    cScDevice *dev=dynamic_cast<cScDevice *>(cDevice::GetDevice(n));
    if(dev) dev->EarlyShutdown();
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

#else //SASC

void cScDevices::OnPluginLoad(void) {}
void cScDevices::OnPluginUnload(void) {}
bool cScDevices::Initialize(void) { return true; }
void cScDevices::Startup(void) {}
void cScDevices::Shutdown(void) {}
void cScDevices::SetForceBudget(int n) {}
bool cScDevices::ForceBudget(int n) { return true; }

#endif //SASC

// -- cScDevice ----------------------------------------------------------------

#if APIVERSNUM >= 10711
#define DVB_DEV_SPEC adapter,frontend
#else
#define DVB_DEV_SPEC CardIndex(),0
#endif

cScDevice::cScDevice(int Adapter, int Frontend, int cafd)
#if APIVERSNUM >= 10711
:cDvbDevice(Adapter,Frontend)
#else
:cDvbDevice(Adapter)
#endif
{
  tsBuffer=0; softcsa=fullts=false;
  cam=0; hwciadapter=0;
  fd_ca=cafd; fd_ca2=dup(fd_ca); fd_dvr=-1;
#ifdef SASC
  cam=new cCam(this,Adapter);
#endif // !SASC
#if APIVERSNUM >= 10711
  snprintf(devId,sizeof(devId),"%d/%d",Adapter,FrontEnd);
#else
  snprintf(devId,sizeof(devId),"%d",Adapter);
#endif
}

cScDevice::~cScDevice()
{
#ifndef SASC
  DetachAllReceivers();
  Cancel(3);
  EarlyShutdown();
  if(fd_ca>=0) close(fd_ca);
  if(fd_ca2>=0) close(fd_ca2);
#else
  delete cam;
#endif // !SASC
}

#ifndef SASC

void cScDevice::EarlyShutdown(void)
{
  SetCamSlot(0);
  delete cam; cam=0;
  delete hwciadapter; hwciadapter=0;
}

void cScDevice::LateInit(void)
{
  int n=CardIndex();
  if(DeviceNumber()!=n)
    PRINTF(L_GEN_ERROR,"CardIndex - DeviceNumber mismatch! Put SC plugin first on VDR commandline!");
  softcsa=(fd_ca<0);
  if(softcsa) {
    if(HasDecoder()) PRINTF(L_GEN_ERROR,"Card %s is a full-featured card but no ca device found!",devId);
    }
  else if(cScDevices::ForceBudget(n)) {
    PRINTF(L_GEN_INFO,"Budget mode forced on card %s",devId);
    softcsa=true;
    }
  if(softcsa) {
    if(IsPrimaryDevice() && HasDecoder()) {
      PRINTF(L_GEN_INFO,"Enabling hybrid full-ts mode on card %s",devId);
      fullts=true;
      }
    else PRINTF(L_GEN_INFO,"Using software decryption on card %s",devId);
    }
  if(fd_ca2>=0) hwciadapter=cDvbCiAdapter::CreateCiAdapter(this,fd_ca2);
  cam=new cCam(this,DVB_DEV_SPEC,devId,fd_ca,softcsa,fullts);
}

bool cScDevice::HasCi(void)
{
  return cam || hwciadapter;
}

bool cScDevice::Ready(void)
{
  return (cam         ? cam->Ready():true) &&
         (hwciadapter ? hwciadapter->Ready():true);
}

bool cScDevice::SetPid(cPidHandle *Handle, int Type, bool On)
{
  if(cam) cam->SetPid(Type,Handle->pid,On);
  tsMutex.Lock();
  if(tsBuffer) tsBuffer->SetActive(ScActive());
  tsMutex.Unlock();
  return cDvbDevice::SetPid(Handle,Type,On);
}

bool cScDevice::SetChannelDevice(const cChannel *Channel, bool LiveView)
{
  if(cam) cam->Tune(Channel);
  bool ret=cDvbDevice::SetChannelDevice(Channel,LiveView);
  if(ret && cam) cam->PostTune();
  return ret;
}

bool cScDevice::ScActive(void)
{
  return cam && cam->OwnSlot(CamSlot());
}

bool cScDevice::OpenDvr(void)
{
  CloseDvr();
  fd_dvr=cScDevices::DvbOpen(DEV_DVB_DVR,DVB_DEV_SPEC,O_RDONLY|O_NONBLOCK,true);
  if(fd_dvr>=0) {
    cDeCSA *decsa=cam ? cam->DeCSA() : 0;
    tsMutex.Lock();
    tsBuffer=new cDeCsaTSBuffer(fd_dvr,MEGABYTE(ScSetup.DeCsaTsBuffSize),CardIndex()+1,decsa,ScActive());
    tsMutex.Unlock();
    }
  return fd_dvr>=0;
}

void cScDevice::CloseDvr(void)
{
  tsMutex.Lock();
  delete tsBuffer; tsBuffer=0;
  tsMutex.Unlock();
  if(fd_dvr>=0) { close(fd_dvr); fd_dvr=-1; }
}

bool cScDevice::GetTSPacket(uchar *&Data)
{
  if(tsBuffer) { Data=tsBuffer->Get(); return true; }
  return false;
}

#endif // !SASC
