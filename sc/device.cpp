/*
 * device.c: The basic device interface
 *
 * See the main source file 'vdr.c' for copyright information and
 * how to reach the author.
 *
 * $Id: device.c 1.56 2004/06/19 08:51:05 kls Exp $
 */

#include "include/vdr/device.h"
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "include/vdr/channels.h"
#include "include/vdr/i18n.h"

// --- cDevice ---------------------------------------------------------------

// The default priority for non-primary devices:
#define DEFAULTPRIORITY  -1

int cDevice::numDevices = 0;
int cDevice::useDevice = 0;
int cDevice::nextCardIndex = 0;
int cDevice::currentChannel = 1;
cDevice *cDevice::device[MAXDEVICES] = { NULL };
cDevice *cDevice::primaryDevice = NULL;

cDevice::cDevice(void)
{
  cardIndex = nextCardIndex++;

  SetDescription("receiver on device %d", CardIndex() + 1);

  mute = false;

  sectionHandler = NULL;
  eitFilter = NULL;
  patFilter = NULL;
  sdtFilter = NULL;
  nitFilter = NULL;

  player = NULL;

  for (int i = 0; i < MAXRECEIVERS; i++)
      receiver[i] = NULL;

  if (numDevices < MAXDEVICES)
     device[numDevices++] = this;
  else
     esyslog("ERROR: too many devices!");
}

cDevice::~cDevice()
{
}

void cDevice::SetUseDevice(int n)
{
  if (n < MAXDEVICES)
     useDevice |= (1 << n);
}

int cDevice::NextCardIndex(int n)
{
  if (n > 0) {
     nextCardIndex += n;
     if (nextCardIndex >= MAXDEVICES)
        esyslog("ERROR: nextCardIndex too big (%d)", nextCardIndex);
     }
  else if (n < 0)
     esyslog("ERROR: illegal value in IncCardIndex(%d)", n);
  return nextCardIndex;
}

int cDevice::DeviceNumber(void) const
{
  for (int i = 0; i < numDevices; i++) {
      if (device[i] == this)
         return i;
      }
  return -1;
}

void cDevice::MakePrimaryDevice(bool On)
{
}

bool cDevice::SetPrimaryDevice(int n)
{
  n--;
  if (0 <= n && n < numDevices && device[n]) {
     isyslog("setting primary device to %d", n + 1);
     if (primaryDevice)
        primaryDevice->MakePrimaryDevice(false);
     primaryDevice = device[n];
     primaryDevice->MakePrimaryDevice(true);
     return true;
     }
  esyslog("ERROR: invalid primary device number: %d", n + 1);
  return false;
}

bool cDevice::HasDecoder(void) const
{
  return false;
}

cSpuDecoder *cDevice::GetSpuDecoder(void)
{
  return NULL;
}

cDevice *cDevice::ActualDevice(void)
{
  return NULL;
}

cDevice *cDevice::GetDevice(int Index)
{
  int i;
  for (i = 0; i < numDevices; i++)
    if (device[i]->CardIndex() == Index)
      return device[i];
  return NULL;
}

cDevice *cDevice::GetDevice(const cChannel *Channel, int Priority, bool LiveView)
{
  return NULL;
}

bool cDevice::HasCi(void)
{
  return false;
}

void cDevice::SetCamSlot(cCamSlot *CamSlot)
{
}

void cDevice::Shutdown(void)
{
}

uchar *cDevice::GrabImage(int &Size, bool Jpeg, int Quality, int SizeX, int SizeY)
{
  return false;
}

void cDevice::SetVideoDisplayFormat(eVideoDisplayFormat VideoDisplayFormat)
{
}

void cDevice::SetVideoFormat(bool VideoFormat16_9)
{
}

eVideoSystem cDevice::GetVideoSystem(void)
{
  return vsNTSC;
}

#define PRINTPIDS(s)

bool cDevice::HasPid(int Pid) const
{
  for (int i = 0; i < MAXPIDHANDLES; i++) {
      if (pidHandles[i].pid == Pid)
         return true;
      }
  return false;
}

bool cDevice::AddPid(int Pid, ePidType PidType)
{
  if (Pid || PidType == ptPcr) {
     int n = -1;
     int a = -1;
     if (PidType != ptPcr) { // PPID always has to be explicit
        for (int i = 0; i < MAXPIDHANDLES; i++) {
            if (i != ptPcr) {
               if (pidHandles[i].pid == Pid)
                  n = i;
               else if (a < 0 && i >= ptOther && !pidHandles[i].used)
                  a = i;
               }
            }
        }
     if (n >= 0) {
        // The Pid is already in use
        if (++pidHandles[n].used == 2 && n <= ptTeletext) {
           // It's a special PID that may have to be switched into "tap" mode
           PRINTPIDS("A");
           return SetPid(&pidHandles[n], n, true);
           }
        PRINTPIDS("a");
        return true;
        }
     else if (PidType < ptOther) {
        // The Pid is not yet in use and it is a special one
        n = PidType;
        }
     else if (a >= 0) {
        // The Pid is not yet in use and we have a free slot
        n = a;
        }
     else
        esyslog("ERROR: no free slot for PID %d", Pid);
     if (n >= 0) {
        pidHandles[n].pid = Pid;
        pidHandles[n].used = 1;
        PRINTPIDS("C");
        return SetPid(&pidHandles[n], n, true);
        }
     }
  return true;
}

void cDevice::DelPid(int Pid, ePidType PidType)
{
  if (Pid || PidType == ptPcr) {
     int n = -1;
     if (PidType == ptPcr)
        n = PidType; // PPID always has to be explicit
     else {
        for (int i = 0; i < MAXPIDHANDLES; i++) {
            if (pidHandles[i].pid == Pid) {
               n = i;
               break;
               }
            }
        }
     if (n >= 0 && pidHandles[n].used) {
        PRINTPIDS("D");
        if (--pidHandles[n].used < 2) {
           SetPid(&pidHandles[n], n, false);
           if (pidHandles[n].used == 0) {
              pidHandles[n].handle = -1;
              pidHandles[n].pid = 0;
              }
           }
        PRINTPIDS("E");
        }
     }
}

bool cDevice::SetPid(cPidHandle *Handle, int Type, bool On)
{
  return false;
}

void cDevice::StartSectionHandler(void)
{
}

int cDevice::OpenFilter(u_short Pid, u_char Tid, u_char Mask)
{
  return -1;
}

void cDevice::CloseFilter(int Handle)
{
  close(Handle);
}

void cDevice::AttachFilter(cFilter *Filter)
{
}

void cDevice::Detach(cFilter *Filter)
{
}

bool cDevice::ProvidesSource(int Source) const
{
  return false;
}

bool cDevice::ProvidesTransponder(const cChannel *Channel) const
{
  return false;
}

bool cDevice::ProvidesTransponderExclusively(const cChannel *Channel) const
{
  for (int i = 0; i < numDevices; i++) {
      if (device[i] && device[i] != this && device[i]->ProvidesTransponder(Channel))
         return false;
      }
  return true;
}

bool cDevice::ProvidesChannel(const cChannel *Channel, int Priority, bool *NeedsDetachReceivers) const
{
  return false;
}

bool cDevice::IsTunedToTransponder(const cChannel *Channel)
{
  return false;
}

bool cDevice::MaySwitchTransponder(void)
{
  return !Receiving(true) && !(pidHandles[ptAudio].pid || pidHandles[ptVideo].pid || pidHandles[ptDolby].pid);
}

bool cDevice::SwitchChannel(const cChannel *Channel, bool LiveView)
{
  return false;
}

bool cDevice::SwitchChannel(int Direction)
{
  return false;
}

eSetChannelResult cDevice::SetChannel(const cChannel *Channel, bool LiveView)
{
  return scrOk;
}

bool cDevice::SetChannelDevice(const cChannel *Channel, bool LiveView)
{
  return false;
}

bool cDevice::HasLock(int TimeoutMs)
{
  return true;
}

bool cDevice::HasProgramme(void)
{
  return false;
}

int cDevice::GetAudioChannelDevice(void)
{
  return 0;
}

void cDevice::SetAudioChannelDevice(int AudioChannel)
{
}

void cDevice::SetVolumeDevice(int Volume)
{
}

void cDevice::SetDigitalAudioDevice(bool On)
{
}

void cDevice::SetAudioTrackDevice(eTrackType Type)
{
}

bool cDevice::ToggleMute(void)
{
  return true;
}

void cDevice::SetVolume(int Volume, bool Absolute)
{
}

int cDevice::NumAudioTracks(void) const
{
  return 0;
}


bool cDevice::CanReplay(void) const
{
  return HasDecoder();
}

bool cDevice::SetPlayMode(ePlayMode PlayMode)
{
  return false;
}

int64_t cDevice::GetSTC(void)
{
  return -1;
}

void cDevice::TrickSpeed(int Speed)
{
}

void cDevice::Clear(void)
{
}

void cDevice::Play(void)
{
}

void cDevice::Freeze(void)
{
}

void cDevice::Mute(void)
{
}

void cDevice::StillPicture(const uchar *Data, int Length)
{
}

bool cDevice::Replaying(void) const
{
  return player != NULL;
}

bool cDevice::AttachPlayer(cPlayer *Player)
{
  return false;
}

void cDevice::Detach(cPlayer *Player)
{
}

void cDevice::StopReplay(void)
{
}

bool cDevice::Poll(cPoller &Poller, int TimeoutMs)
{
  return false;
}

bool cDevice::Flush(int TimeoutMs)
{
  return true;
}

int cDevice::PlayVideo(const uchar *Data, int Length)
{
  return -1;
}

int cDevice::PlayAudio(const uchar *Data, int Length, uchar Id)
{
  return -1;
}

int cDevice::PlayPesPacket(const uchar *Data, int Length, bool VideoOnly)
{
  return Length;
}

int cDevice::PlayPes(const uchar *Data, int Length, bool VideoOnly)
{
  return Length;
}

int cDevice::Priority(void) const
{
  return 0;
}

bool cDevice::Ready(void)
{
  return true;
}

bool cDevice::Receiving(bool CheckAny) const
{
  return false;
}

void cDevice::Action(void)
{
}

bool cDevice::OpenDvr(void)
{
  return false;
}

void cDevice::CloseDvr(void)
{
}

bool cDevice::GetTSPacket(uchar *&Data)
{
  return false;
}

bool cDevice::AttachReceiver(cReceiver *Receiver)
{
  return false;
}

void cDevice::Detach(cReceiver *Receiver)
{
}

void cDevice::DetachAllReceivers(void)
{
}
// --- cTSBuffer -------------------------------------------------------------
cTSBuffer::cTSBuffer(int File, int Size, int CardIndex)
{
}

cTSBuffer::~cTSBuffer()
{
}

void cTSBuffer::Action(void)
{
}

uchar *cTSBuffer::Get(void)
{
  return NULL;
}

