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
#include <sys/ioctl.h>

#ifndef SASC

#include <vdr/channels.h>
#include <vdr/ci.h>
#include <vdr/dvbci.h>

#include "device.h"
#include "cam.h"
#include "scsetup.h"
#include "log-core.h"
#include "version.h"

SCAPIVERSTAG();

#if APIVERSNUM >= 10711
#ifdef WITH_SDDVB

// -- cScDvbSdFfDevice ---------------------------------------------------------

#include "../dvbsddevice/dvbsdffdevice.h"
#define SCDEVICE cScDvbSdFfDevice
#define DVBDEVICE cDvbSdFfDevice
#define OWN_FULLTS
#define OWN_SETCA
#define OWN_DUMPAV
#if APIVERSNUM >= 10721
#define OWN_DEVPARAMS false
#endif
#include "device-tmpl.c"

bool cScDvbSdFfDevice::CheckFullTs(void)
{
  return IsPrimaryDevice() && HasDecoder();
}

bool cScDvbSdFfDevice::SetCaDescr(ca_descr_t *ca_descr, bool initial)
{
  cMutexLock lock(&cafdMutex);
  return ioctl(fd_ca,CA_SET_DESCR,ca_descr)>=0;
}

bool cScDvbSdFfDevice::SetCaPid(ca_pid_t *ca_pid)
{
  cMutexLock lock(&cafdMutex);
  return ioctl(fd_ca,CA_SET_PID,ca_pid)>=0;
}

static unsigned int av7110_read(int fd, unsigned int addr)
{
  ca_pid_t arg;
  arg.pid=addr;
  ioctl(fd,CA_GET_MSG,&arg);
  return arg.index;
}

#if 0
static void av7110_write(int fd, unsigned int addr, unsigned int val)
{
  ca_pid_t arg;
  arg.pid=addr;
  arg.index=val;
  ioctl(fd,CA_SEND_MSG,&arg);
}
#endif

void cScDvbSdFfDevice::DumpAV(void)
{
  if(LOG(L_CORE_AV7110)) {
#define CODEBASE (0x2e000404+0x1ce00)
    cMutexLock lock(&cafdMutex);
    if(HasDecoder() && lastDump.Elapsed()>20000) {
      lastDump.Set();
      static unsigned int handles=0, hw_handles=0;
      static const unsigned int code[] = {
        0xb5100040,0x4a095a12,0x48094282,0xd00b4b09,0x20000044,
        0x5b1c4294,0xd0033001,0x281cdbf8,0xe001f7fe,0xfd14bc10
        };
      static const unsigned int mask[] = {
        0xffffffff,0xffffffff,0xffffffff,0xffffffff,0xffffffff,
        0xffffffff,0xffffffff,0xffffffff,0xfffff800,0xf800ffff
        };
      if(!handles) {
        handles=1;
        PRINTF(L_CORE_AV7110,"searching handle tables");
        for(int i=0; i<0x2000; i+=4) {
          int j;
          for(j=0; j<20; j+=4) {
            int r=av7110_read(fd_ca,CODEBASE+i+j);
            if((r&mask[j/4])!=(code[j/4]&mask[j/4])) break;
            }
          if(j==20) {
            handles=av7110_read(fd_ca,CODEBASE+i+44);
            hw_handles=av7110_read(fd_ca,CODEBASE+i+52);
            PRINTF(L_CORE_AV7110,"found handles=%08x hw_handles=%08x at 0x%08x",handles,hw_handles,CODEBASE+i);
            if((handles>>16)!=0x2e08 || (hw_handles>>16)!=0x2e08) {
              PRINTF(L_CORE_AV7110,"seems to be invalid");
              }
            break;
            }
          }
        }

      unsigned int hdl=0, hwhdl=0;
      PRINTF(L_CORE_AV7110,"         : 64000080 64000400");
      for(int i=0; i<=31; i++) {
        unsigned int off80 =av7110_read(fd_ca,0x64000080+i*4);
        unsigned int off400=av7110_read(fd_ca,0x64000400+i*8);
        LBSTART(L_CORE_AV7110);
        LBPUT("handle %2d: %08x %08x %s pid=%04x idx=%d",
          i,off80,off400,off80&0x2000?"ACT":"---",off80&0x1fff,(off400&0x1e)>>1);
        if(handles>1 && i<=27) {
          if((i&1)==0) {
            hdl=av7110_read(fd_ca,handles+i*2);
            hwhdl=av7110_read(fd_ca,hw_handles+i*2);
            }
          unsigned int s=((~i)&1)<<4;
          LBPUT(" | %02d hdl=%04x hwfilt=%04x",i,(hdl>>s)&0xffff,(hwhdl>>s)&0xffff);
          }
        LBEND();
        }
      }
    }
}

// -- cScSdDevicePlugin --------------------------------------------------------

class cScSdDevicePlugin : public cScDevicePlugin {
public:
  virtual cDevice *Probe(int Adapter, int Frontend, uint32_t SubSystemId);
  virtual bool LateInit(cDevice *dev);
  virtual bool EarlyShutdown(cDevice *dev);
  virtual bool SetCaDescr(cDevice *dev, ca_descr_t *ca_descr, bool initial);
  virtual bool SetCaPid(cDevice *dev, ca_pid_t *ca_pid);
  virtual bool DumpAV(cDevice *dev);
  };

static cScSdDevicePlugin _sddevplugin;

cDevice *cScSdDevicePlugin::Probe(int Adapter, int Frontend, uint32_t SubSystemId)
{
  static uint32_t SubsystemIds[] = {
    0x110A0000, // Fujitsu Siemens DVB-C
    0x13C20000, // Technotrend/Hauppauge WinTV DVB-S rev1.X or Fujitsu Siemens DVB-C
    0x13C20001, // Technotrend/Hauppauge WinTV DVB-T rev1.X
    0x13C20002, // Technotrend/Hauppauge WinTV DVB-C rev2.X
    0x13C20003, // Technotrend/Hauppauge WinTV Nexus-S rev2.X
    0x13C20004, // Galaxis DVB-S rev1.3
    0x13C20006, // Fujitsu Siemens DVB-S rev1.6
    0x13C20008, // Technotrend/Hauppauge DVB-T
    0x13C2000A, // Technotrend/Hauppauge WinTV Nexus-CA rev1.X
    0x13C2000E, // Technotrend/Hauppauge WinTV Nexus-S rev2.3
    0x13C21002, // Technotrend/Hauppauge WinTV DVB-S rev1.3 SE
    0x00000000
    };
  for(uint32_t *sid=SubsystemIds; *sid; sid++) {
    if(*sid==SubSystemId) {
      PRINTF(L_GEN_DEBUG,"creating SD-FF device %d/%d",Adapter,Frontend);
      return new cScDvbSdFfDevice(this,Adapter,Frontend,cScDevices::DvbOpen(DEV_DVB_CA,Adapter,Frontend,O_RDWR));
      }
    }
  return 0;
}

bool cScSdDevicePlugin::LateInit(cDevice *dev)
{
  cScDvbSdFfDevice *d=dynamic_cast<cScDvbSdFfDevice *>(dev);
  if(d) d->LateInit();
  return d!=0;
}

bool cScSdDevicePlugin::EarlyShutdown(cDevice *dev)
{
  cScDvbSdFfDevice *d=dynamic_cast<cScDvbSdFfDevice *>(dev);
  if(d) d->EarlyShutdown();
  return d!=0;
}

bool cScSdDevicePlugin::SetCaDescr(cDevice *dev, ca_descr_t *ca_descr, bool initial)
{
  cScDvbSdFfDevice *d=dynamic_cast<cScDvbSdFfDevice *>(dev);
  if(d) return d->SetCaDescr(ca_descr,initial);
  return false;
}

bool cScSdDevicePlugin::SetCaPid(cDevice *dev, ca_pid_t *ca_pid)
{
  cScDvbSdFfDevice *d=dynamic_cast<cScDvbSdFfDevice *>(dev);
  if(d) return d->SetCaPid(ca_pid);
  return d!=0;
}

bool cScSdDevicePlugin::DumpAV(cDevice *dev)
{
  cScDvbSdFfDevice *d=dynamic_cast<cScDvbSdFfDevice *>(dev);
  if(d) d->DumpAV();
  return d!=0;
}

#endif //WITH_SDDVB
#endif //APIVERSNUM >= 10711
#endif //SASC
