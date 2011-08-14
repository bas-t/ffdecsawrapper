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

// -- cScDevice ----------------------------------------------------------------

class SCDEVICE : public DVBDEVICE {
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
  bool ScActive(void);
#endif //SASC
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
  SCDEVICE(int Adapter, int Frontend, int cafd);
  ~SCDEVICE();
#ifndef SASC
  virtual bool HasCi(void);
  void LateInit(void);
  void EarlyShutdown(void);
#endif //SASC
  };

SCDEVICE::SCDEVICE(int Adapter, int Frontend, int cafd)
#if APIVERSNUM >= 10711
:DVBDEVICE(Adapter,Frontend)
#else
:DVBDEVICE(Adapter)
#endif
{
  tsBuffer=0; softcsa=fullts=false;
  cam=0; hwciadapter=0;
  fd_ca=cafd; fd_ca2=dup(fd_ca); fd_dvr=-1;
#ifdef SASC
  cam=new cCam(this,Adapter);
#endif // !SASC
#if APIVERSNUM >= 10711
  snprintf(devId,sizeof(devId),"%d/%d",Adapter,Frontend);
#else
  snprintf(devId,sizeof(devId),"%d",Adapter);
#endif
}

SCDEVICE::~SCDEVICE()
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

void SCDEVICE::EarlyShutdown(void)
{
  SetCamSlot(0);
  delete cam; cam=0;
  delete hwciadapter; hwciadapter=0;
}

void SCDEVICE::LateInit(void)
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

bool SCDEVICE::HasCi(void)
{
  return cam || hwciadapter;
}

bool SCDEVICE::Ready(void)
{
  return (cam         ? cam->Ready():true) &&
         (hwciadapter ? hwciadapter->Ready():true);
}

bool SCDEVICE::SetPid(cPidHandle *Handle, int Type, bool On)
{
  if(cam) cam->SetPid(Type,Handle->pid,On);
  tsMutex.Lock();
  if(tsBuffer) tsBuffer->SetActive(ScActive());
  tsMutex.Unlock();
  return DVBDEVICE::SetPid(Handle,Type,On);
}

bool SCDEVICE::SetChannelDevice(const cChannel *Channel, bool LiveView)
{
  if(cam) cam->Tune(Channel);
  bool ret=DVBDEVICE::SetChannelDevice(Channel,LiveView);
  if(ret && cam) cam->PostTune();
  return ret;
}

bool SCDEVICE::ScActive(void)
{
  return cam && cam->OwnSlot(CamSlot());
}

bool SCDEVICE::OpenDvr(void)
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

void SCDEVICE::CloseDvr(void)
{
  tsMutex.Lock();
  delete tsBuffer; tsBuffer=0;
  tsMutex.Unlock();
  if(fd_dvr>=0) { close(fd_dvr); fd_dvr=-1; }
}

bool SCDEVICE::GetTSPacket(uchar *&Data)
{
  if(tsBuffer) { Data=tsBuffer->Get(); return true; }
  return false;
}

#endif // !SASC
