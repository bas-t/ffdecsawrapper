/*
 * sections.h: Section data handling
 *
 * See the main source file 'vdr.c' for copyright information and
 * how to reach the author.
 *
 * $Id: sections.h 1.5 2005/08/13 11:23:55 kls Exp $
 */

#ifndef __SECTIONS_H
#define __SECTIONS_H

#include <time.h>
#include "filter.h"
#include "thread.h"
#include "tools.h"

class cDevice;
class cChannel;
class cFilterHandle;
class cSectionHandlerPrivate;

class cSectionHandler : public cThread {
  friend class cFilter;
private:
  cSectionHandlerPrivate *shp;
  cDevice *device;
  int statusCount;
  bool on, waitForLock;
  time_t lastIncompleteSection;
  cList<cFilter> filters;
  cList<cFilterHandle> filterHandles;
  void Add(const cFilterData *FilterData);
  void Del(const cFilterData *FilterData);
  virtual void Action(void);
public:
  cSectionHandler(cDevice *Device);
  virtual ~cSectionHandler();
  int Source(void);
  int Transponder(void);
  const cChannel *Channel(void);
  void Attach(cFilter *Filter);
  void Detach(cFilter *Filter);
  void SetChannel(const cChannel *Channel);
  void SetStatus(bool On);
  };

#endif //__SECTIONS_H
