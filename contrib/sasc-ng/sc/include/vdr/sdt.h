/*
 * sdt.h: SDT section filter
 *
 * See the main source file 'vdr.c' for copyright information and
 * how to reach the author.
 *
 * $Id: sdt.h 1.2 2004/01/05 14:30:14 kls Exp $
 */

#ifndef __SDT_H
#define __SDT_H

#include "filter.h"
#include "pat.h"

class cSdtFilter : public cFilter {
private:
  cSectionSyncer sectionSyncer;
  cPatFilter *patFilter;
protected:
  virtual void Process(u_short Pid, u_char Tid, const u_char *Data, int Length);
public:
  cSdtFilter(cPatFilter *PatFilter);
  virtual void SetStatus(bool On);
  };

#endif //__SDT_H
