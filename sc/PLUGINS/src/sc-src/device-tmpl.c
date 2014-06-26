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
#ifndef SASC
  cDeCsaTSBuffer *tsBuffer;
  cMutex tsMutex;
#endif //!SASC
  cCam *cam;
  cScDevicePlugin *devplugin;
#ifndef SASC
  cCiAdapter *hwciadapter;
  cTimeMs lastDump;
#endif //!SASC
  int fd_dvr, fd_ca, fd_ca2;
  cMutex cafdMutex;
  bool softcsa, fullts;
  char devId[8];
  //
#ifndef SASC
  bool ScActive(void);
#endif //!SASC
protected:
#ifndef SASC
  virtual bool Ready(void);
  virtual bool SetPid(cPidHandle *Handle, int Type, bool On);
  virtual bool SetChannelDevice(const cChannel *Channel, bool LiveView);
  virtual bool OpenDvr(void);
  virtual void CloseDvr(void);
  virtual bool GetTSPacket(uchar *&Data);
#endif //!SASC
public:
  SCDEVICE(cScDevicePlugin *DevPlugin, int Adapter, int Frontend, int cafd);
  ~SCDEVICE();
  bool SetCaDescr(ca_descr_t *ca_descr, bool initial);
  bool SetCaPid(ca_pid_t *ca_pid);
  void DumpAV(void);
#ifndef SASC
  virtual bool HasCi(void);
  void LateInit(void);
  void EarlyShutdown(void);
  bool CheckFullTs(void);
#else
  cCam *Cam(void) { return cam; }
#endif //!SASC
  };

SCDEVICE::SCDEVICE(cScDevicePlugin *DevPlugin, int Adapter, int Frontend, int cafd)
#if APIVERSNUM >= 10711
#ifdef OWN_DEVPARAMS
:DVBDEVICE(Adapter,Frontend,OWN_DEVPARAMS)
#else
:DVBDEVICE(Adapter,Frontend)
#endif //OWN_DEVPARAMS
#else
:DVBDEVICE(Adapter)
#endif //APIVERSNUM >= 10711
{
#ifndef SASC
  tsBuffer=0; hwciadapter=0;
#endif
  cam=0; devplugin=DevPlugin; softcsa=fullts=false;
  fd_ca=cafd; fd_ca2=dup(fd_ca); fd_dvr=-1;
#if APIVERSNUM >= 10711
  snprintf(devId,sizeof(devId),"%d/%d",Adapter,Frontend);
#else
  snprintf(devId,sizeof(devId),"%d",Adapter);
#endif
#ifdef SASC
  cam=new cCam(this,Adapter,0,devId,devplugin,softcsa,fullts);
#endif // !SASC
}

SCDEVICE::~SCDEVICE()
{
#ifndef SASC
  DetachAllReceivers();
  Cancel(3);
  EarlyShutdown();
#endif
  if(fd_ca>=0) close(fd_ca);
  if(fd_ca2>=0) close(fd_ca2);
#ifdef SASC
  delete cam;
#endif
}

#ifndef OWN_SETCA
bool SCDEVICE::SetCaDescr(ca_descr_t *ca_descr, bool initial)
{
  return false;
}

bool SCDEVICE::SetCaPid(ca_pid_t *ca_pid)
{
  return false;
}
#endif //!OWN_SETCA

#ifndef OWN_DUMPAV
void SCDEVICE::DumpAV(void)
{}
#endif

#ifndef SASC

void SCDEVICE::EarlyShutdown(void)
{
  SetCamSlot(0);
  delete cam; cam=0;
  delete hwciadapter; hwciadapter=0;
}

#ifndef OWN_FULLTS
bool SCDEVICE::CheckFullTs(void)
{
  return false;
}
#endif //!OWN_FULLTS

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
    fullts=CheckFullTs();
    if(fullts) PRINTF(L_GEN_INFO,"Enabling hybrid full-ts mode on card %s",devId);
    else PRINTF(L_GEN_INFO,"Using software decryption on card %s",devId);
    }
  if(fd_ca2>=0) hwciadapter=cDvbCiAdapter::CreateCiAdapter(this,fd_ca2);
  cam=new cCam(this,DVB_DEV_SPEC,devId,devplugin,softcsa,fullts);
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
  if(LiveView && IsPrimaryDevice() && Channel->Ca()>=CA_ENCRYPTED_MIN &&
     !Transferring() &&
     softcsa && !fullts) {
    PRINTF(L_GEN_INFO,"Forcing transfermode on card %s",devId);
    DVBDEVICE::SetChannelDevice(Channel,false); // force transfermode
    }
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

#undef SCDEVICE
#undef DVBDEVICE
#undef OWN_SETCA
#undef OWN_DUMPAV
#undef OWN_FULLTS
#undef OWN_DEVPARAMS
