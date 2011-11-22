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

#define SASC

#include <stdlib.h>
#include <stdio.h>

#include "include/vdr/dvbdevice.h"
#include "include/vdr/channels.h"
#include "cam.h"
#include "device.h"
#include "sasccam.h"
#include "scsetup.h"

// -- cScSascDevice ------------------------------------------------------------

#define SCDEVICE cScSascDevice
#define DVBDEVICE cDvbDevice
#define OWN_SETCA
#include "device-tmpl.c"

extern void _SetCaDescr(int adapter, ca_descr_t *ca_descr);
extern void _SetCaPid(int adapter, ca_pid_t *ca_pid);

bool cScSascDevice::SetCaDescr(ca_descr_t *ca_descr, bool initial)
{
  //printf("Called cScSascDevice::SetCaDescr\n");
  _SetCaDescr(cardIndex,ca_descr);
  return true;
}

bool cScSascDevice::SetCaPid(ca_pid_t *ca_pid)
{
  //printf("Called cScSascDevice::SetCaPid\n");
  _SetCaPid(cardIndex,ca_pid);
  return true;
}

// -- cScSascDevicePlugin ------------------------------------------------------

class cScSascDevicePlugin : public cScDevicePlugin {
public:
  virtual cDevice *Probe(int Adapter, int Frontend, uint32_t SubSystemId);
  virtual bool LateInit(cDevice *dev);
  virtual bool EarlyShutdown(cDevice *dev);
  virtual bool SetCaDescr(cDevice *dev, ca_descr_t *ca_descr, bool initial);
  virtual bool SetCaPid(cDevice *dev, ca_pid_t *ca_pid);
  };

cDevice *cScSascDevicePlugin::Probe(int Adapter, int Frontend, uint32_t SubSystemId)
{
  return 0;
}

bool cScSascDevicePlugin::LateInit(cDevice *dev)
{
  return false;
}

bool cScSascDevicePlugin::EarlyShutdown(cDevice *dev)
{
  return false;
}

bool cScSascDevicePlugin::SetCaDescr(cDevice *dev, ca_descr_t *ca_descr, bool initial)
{
  cScSascDevice *d=dynamic_cast<cScSascDevice *>(dev);
  if(d) return d->SetCaDescr(ca_descr,initial);
  return false;
}

bool cScSascDevicePlugin::SetCaPid(cDevice *dev, ca_pid_t *ca_pid)
{
  cScSascDevice *d=dynamic_cast<cScSascDevice *>(dev);
  if(d) return d->SetCaPid(ca_pid);
  return false;
}

// -----------------------------------------------------------------------------

//Functions to communicate with the cam from the outside world
//Initialize the cam
sascCam::sascCam(int devnum)
{
  dev = new cScSascDevice(new cScSascDevicePlugin,devnum,0,-1);
  cam = dev->Cam();
  ScSetup.ConcurrentFF=8;
}
void sascCam::Tune(cChannel *ch)
{ 
  cam->Tune(ch);
}
void sascCam::Stop()
{
  cam->Stop();
}
void sascCam::AddPrg(int sid, int *epid, const unsigned char *pmt, int pmtlen)
{
  int i = 0;
  if(! epid)
    return;
  cPrg *prg=new cPrg(sid, 1); 
  if(pmt) prg->caDescr.Set(pmt,pmtlen);
  while(epid[i]) {
    cPrgPid *pid=new cPrgPid(5, epid[i++]);
    prg->pids.Add(pid);
  }
  cam->AddPrg(prg);
}
