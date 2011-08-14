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
#include "device-tmpl.c"
#undef SCDEVICE
#undef DVBDEVICE

// -- cScSdDevicePlugin --------------------------------------------------------

class cScSdDevicePlugin : public cScDevicePlugin {
public:
  virtual cDevice *Probe(int Adapter, int Frontend, uint32_t SubSystemId);
  virtual bool LateInit(cDevice *dev);
  virtual bool EarlyShutdown(cDevice *dev);
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
      return new cScDvbSdFfDevice(Adapter,Frontend,cScDevices::DvbOpen(DEV_DVB_CA,Adapter,Frontend,O_RDWR));
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

#endif //WITH_SDDVB
#endif //APIVERSNUM >= 10711
#endif //SASC
