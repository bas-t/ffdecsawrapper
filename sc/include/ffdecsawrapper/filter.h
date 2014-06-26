/*
 * filter.h: Section filter
 *
 * See the main source file 'vdr.c' for copyright information and
 * how to reach the author.
 *
 * $Id: filter.h 1.3 2004/01/11 13:31:59 kls Exp $
 */

#ifndef __FILTER_H
#define __FILTER_H

#include <sys/types.h>
#include "tools.h"

class cSectionSyncer {
private:
  int lastVersion;
  bool synced;
public:
  cSectionSyncer(void);
  void Reset(void);
  bool Sync(uchar Version, int Number, int LastNumber);
  };

class cFilterData : public cListObject {
public:
  u_short pid;
  u_char tid;
  u_char mask;
  bool sticky;
  cFilterData(void);
  cFilterData(u_short Pid, u_char Tid, u_char Mask, bool Sticky);
  bool Is(u_short Pid, u_char Tid, u_char Mask);
  bool Matches(u_short Pid, u_char Tid);
  };

class cChannel;
class cSectionHandler;

class cFilter : public cListObject {
  friend class cSectionHandler;
private:
  cSectionHandler *sectionHandler;
  cList<cFilterData> data;
  bool on;
protected:
  cFilter(void);
  cFilter(u_short Pid, u_char Tid, u_char Mask = 0xFF);
  virtual ~cFilter();
  virtual void SetStatus(bool On);
       ///< Turns this filter on or off, depending on the value of On.
       ///< If the filter is turned off, any filter data that has been
       ///< added without the Sticky parameter set to 'true' will be
       ///< automatically deleted. Those parameters that have been added
       ///< with Sticky set to 'true' will be automatically reused when
       ///< SetStatus(true) is called.
  virtual void Process(u_short Pid, u_char Tid, const u_char *Data, int Length) = 0;
       ///< Processes the data delivered to this filter.
       ///< Pid and Tid is one of the combinations added to this filter by
       ///< a previous call to Add(), Data is a pointer to Length bytes of
       ///< data. This function will be called from the section handler's
       ///< thread, so it has to use proper locking mechanisms in case it
       ///< accesses any global data. It is guaranteed that if several cFilters
       ///< are attached to the same cSectionHandler, only _one_ of them has
       ///< its Process() function called at any given time. It is allowed
       ///< that more than one cFilter are set up to receive the same Pid/Tid.
       ///< The Process() function must return as soon as possible.
  int Source(void);
       ///< Returns the source of the data delivered to this filter.
  int Transponder(void);
       ///< Returns the transponder of the data delivered to this filter.
  const cChannel *Channel(void);
       ///< Returns the channel of the data delivered to this filter.
  bool Matches(u_short Pid, u_char Tid);
       ///< Indicates whether this filter wants to receive data from the given Pid/Tid.
  void Set(u_short Pid, u_char Tid, u_char Mask = 0xFF);
       ///< Sets the given filter data by calling Add() with Sticky = true.
  void Add(u_short Pid, u_char Tid, u_char Mask = 0xFF, bool Sticky = false);
       ///< Adds the given filter data to this filter.
       ///< If Sticky is true, this will survive a status change, otherwise
       ///< it will be automatically deleted.
  void Del(u_short Pid, u_char Tid, u_char Mask = 0xFF);
       ///< Deletes the given filter data from this filter.
  };

#endif //__FILTER_H
