/*
 * eit.h: EIT section filter
 *
 * See the main source file 'vdr.c' for copyright information and
 * how to reach the author.
 *
 * $Id: eit.h 1.30 2003/12/21 14:51:50 kls Exp $
 */

#ifndef __EIT_H
#define __EIT_H

#include "filter.h"

class cEitFilter : public cFilter {
protected:
  virtual void Process(u_short Pid, u_char Tid, const u_char *Data, int Length);
public:
  cEitFilter(void);
  };

#endif //__EIT_H
