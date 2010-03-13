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

// -- cSascDvbDevice -------------------------------------------------------------
class cSascDvbDevice : public cScDevice {
private:
  int cardidx;
public:
  cSascDvbDevice(int n, int cafd) :cScDevice(n, 0, cafd) {cardidx = n;}
  ~cSascDvbDevice() {};
  bool SetCaDescr(ca_descr_t *ca_descr, bool initial);
  bool SetCaPid(ca_pid_t *ca_pid);
  };

extern void _SetCaDescr(int adapter, ca_descr_t *ca_descr);
bool cSascDvbDevice::SetCaDescr(ca_descr_t *ca_descr, bool initial)
{
  printf("Called cSascDvbDevice::SetCaDescr\n");
  _SetCaDescr(cardidx, ca_descr);
  return true;
}

extern void _SetCaPid(int adapter, ca_pid_t *ca_pid);
bool cSascDvbDevice::SetCaPid(ca_pid_t *ca_pid)
{
  printf("Called cSascDvbDevice::SetCaPid\n");
  _SetCaPid(cardidx, ca_pid);
  return true;
}

//Functions to communicate with the cam from the outside world
//Initialize the cam
sascCam::sascCam(int devnum)
{
  dev = new cSascDvbDevice(devnum, -1);
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
    
