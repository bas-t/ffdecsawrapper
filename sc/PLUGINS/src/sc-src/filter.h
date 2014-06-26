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

#ifndef ___FILTER_H
#define ___FILTER_H

#include <vdr/thread.h>
#include "misc.h"

class cDevice;
class cPidFilter;

// ----------------------------------------------------------------

#define MAX_SECT_SIZE 4096

class cAction : protected cThread {
private:
  const char *devId;
  int unique, pri;
  cSimpleList<cPidFilter> filters;
protected:
  cDevice *device;
  char *id;
  //
  virtual void Process(cPidFilter *filter, unsigned char *data, int len)=0;
  virtual void Action(void);
  virtual cPidFilter *CreateFilter(int Num, int IdleTime);
  //
  cPidFilter *NewFilter(int IdleTime);
  cPidFilter *IdleFilter(void);
  void DelFilter(cPidFilter *filter);
  void DelAllFilter(void);
  void Priority(int Pri);
public:
  cAction(const char *Id, cDevice *Device, const char *DevId);
  virtual ~cAction();
  };

// ----------------------------------------------------------------

class cPidFilter : public cSimpleItem, private cMutex {
friend class cAction;
private:
  cDevice *device;
  unsigned int idleTime;
  cTimeMs lastTime;
  bool forceRun;
protected:
  char *id;
  int fd;
  int pid;
public:
  void *userData;
  //
  cPidFilter(const char *Id, int Num, cDevice *Device, unsigned int IdleTime);
  virtual ~cPidFilter();
  void Flush(void);
  virtual void Start(int Pid, int Section, int Mask);
  void Stop(void);
  void SetBuffSize(int BuffSize);
  void Wakeup(void);
  int SetIdleTime(unsigned int IdleTime);
  int Pid(void);
  bool Active(void) { return fd>=0; }
  };

#endif //___FILTER_H
