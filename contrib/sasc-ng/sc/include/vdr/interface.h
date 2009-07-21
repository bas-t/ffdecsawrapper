/*
 * interface.h: Abstract user interface layer
 *
 * See the main source file 'vdr.c' for copyright information and
 * how to reach the author.
 *
 * $Id: interface.h 1.31 2004/05/01 11:11:13 kls Exp $
 */

#ifndef __INTERFACE_H
#define __INTERFACE_H

#include "config.h"
#include "remote.h"
#include "skins.h"
#include "svdrp.h"

class cInterface {
private:
  bool interrupted;
  cSVDRP *SVDRP;
  bool QueryKeys(cRemote *Remote, cSkinDisplayMenu *DisplayMenu);
public:
  cInterface(int SVDRPport = 0);
  ~cInterface();
  bool HasSVDRPConnection(void) { return SVDRP && SVDRP->HasConnection(); }
  void Interrupt(void) { interrupted = true; }
  eKeys GetKey(bool Wait = true);
  eKeys Wait(int Seconds = 0, bool KeepChar = false);
  bool Confirm(const char *s, int Seconds = 10, bool WaitForTimeout = false);
  void LearnKeys(void);
  };

extern cInterface *Interface;

#endif //__INTERFACE_H
