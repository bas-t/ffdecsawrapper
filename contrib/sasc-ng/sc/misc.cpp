#include "include/vdr/channels.h"
#include "include/vdr/device.h"
#include <ctype.h>
#include <linux/dvb/ca.h>
int cChannel::Transponder() const
{
  int tf = frequency;
  while (tf > 20000)
        tf /= 1000;
  if (IsSat())
     tf = Transponder(tf, polarization);
  return tf;
}
int cChannel::Transponder(int Frequency, char Polarization)
{
  // some satellites have transponders at the same frequency, just with different polarization:
  switch (tolower(Polarization)) {
    case 'h': Frequency += 100000; break;
    case 'v': Frequency += 200000; break;
    case 'l': Frequency += 300000; break;
    case 'r': Frequency += 400000; break;
    }
  return Frequency;
}
cChannel* cChannels::GetByNumber(int a, int b) {
  cChannel *ch = new cChannel;
  ch->SetId(0,0,a);
  return ch;
}
void cChannel::SetId(int Nid, int Tid, int Sid, int Rid) {
  sid = Sid;
  tid = Tid;
}
bool cChannel::SetSatTransponderData(int Source, int Frequency, char Polarization, int Srate, int CoderateH) {
     source = Source;
     frequency = Frequency;
     polarization = Polarization;
     srate = Srate;
     coderateH = CoderateH;
     return true;
}
bool cChannel::SetCableTransponderData(int Source, int Frequency, int Modulation, int Srate, int CoderateH)
{
     source = Source;
     frequency = Frequency;
     modulation = Modulation;
     srate = Srate;
     coderateH = CoderateH;
     return true;
}
void cChannel::SetPids(int Vpid, int Ppid, int *Apids, char ALangs[][MAXLANGCODE2], int *Dpids, char DLangs[][MAXLANGCODE2], int Tpid)
{
  vpid = Vpid;
  ppid = Ppid;
  tpid = Tpid;
  for (int i = 0; i < MAXAPIDS; i++)
      apids[i] = Apids[i];
  apids[MAXAPIDS] = 0;
  for (int i = 0; i < MAXDPIDS; i++) {
     dpids[i] = Dpids[i];
  }
  dpids[MAXDPIDS] = 0;
}

cChannel::cChannel() {
  caids[0]=0x0101;
  caids[1]=0;
  source=1;
  sid=1;
  groupSep=0;
}
cChannel::~cChannel() {
}
