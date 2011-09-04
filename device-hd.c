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
#ifdef WITH_HDDVB

// -- cScDvbHdFfDevice ---------------------------------------------------------

#include "../dvbhddevice/dvbhdffdevice.h"
#define SCDEVICE cScDvbHdFfDevice
#define DVBDEVICE cDvbHdFfDevice
#define OWN_SETCA
#include "device-tmpl.c"

bool cScDvbHdFfDevice::SetCaDescr(ca_descr_t *ca_descr, bool initial)
{
  if(!initial) cCondWait::SleepMs(150);
  cMutexLock lock(&cafdMutex);
  return ioctl(fd_ca,CA_SET_DESCR,ca_descr)>=0;
}

bool cScDvbHdFfDevice::SetCaPid(ca_pid_t *ca_pid)
{
  cMutexLock lock(&cafdMutex);
  return ioctl(fd_ca,CA_SET_PID,ca_pid)>=0;
}

// -- cScHdDevicePlugin --------------------------------------------------------

class cScHdDevicePlugin : public cScDevicePlugin {
public:
  virtual cDevice *Probe(int Adapter, int Frontend, uint32_t SubSystemId);
  virtual bool LateInit(cDevice *dev);
  virtual bool EarlyShutdown(cDevice *dev);
  virtual bool SetCaDescr(cDevice *dev, ca_descr_t *ca_descr, bool initial);
  virtual bool SetCaPid(cDevice *dev, ca_pid_t *ca_pid);
  };

static cScHdDevicePlugin _hddevplugin;

cDevice *cScHdDevicePlugin::Probe(int Adapter, int Frontend, uint32_t SubSystemId)
{
  static uint32_t SubsystemIds[] = {
    0x13C23009, // Technotrend S2-6400 HDFF development samples
    0x13C2300A, // Technotrend S2-6400 HDFF production version
    0x00000000
    };
  for(uint32_t *sid=SubsystemIds; *sid; sid++) {
    if(*sid==SubSystemId) {
      int fd=cScDevices::DvbOpen(DEV_DVB_OSD,Adapter,0,O_RDWR);
      if(fd>=0) {
        close(fd);
        PRINTF(L_GEN_DEBUG,"creating HD-FF device %d/%d",Adapter,Frontend);
        return new cScDvbHdFfDevice(this,Adapter,Frontend,cScDevices::DvbOpen(DEV_DVB_CA,Adapter,Frontend,O_RDWR));
        }
      }
    }
  return 0;
}

bool cScHdDevicePlugin::LateInit(cDevice *dev)
{
  cScDvbHdFfDevice *d=dynamic_cast<cScDvbHdFfDevice *>(dev);
  if(d) d->LateInit();
  return d!=0;
}

bool cScHdDevicePlugin::EarlyShutdown(cDevice *dev)
{
  cScDvbHdFfDevice *d=dynamic_cast<cScDvbHdFfDevice *>(dev);
  if(d) d->EarlyShutdown();
  return d!=0;
}

bool cScHdDevicePlugin::SetCaDescr(cDevice *dev, ca_descr_t *ca_descr, bool initial)
{
  cScDvbHdFfDevice *d=dynamic_cast<cScDvbHdFfDevice *>(dev);
  if(d) return d->SetCaDescr(ca_descr,initial);
  return false;
}

bool cScHdDevicePlugin::SetCaPid(cDevice *dev, ca_pid_t *ca_pid)
{
  cScDvbHdFfDevice *d=dynamic_cast<cScDvbHdFfDevice *>(dev);
  if(d) return d->SetCaPid(ca_pid);
  return false;
}

#endif //WITH_HDDVB
#endif //APIVERSNUM >= 10711
#endif //SASC
