/*
 * svdrp.h: Simple Video Disk Recorder Protocol
 *
 * See the main source file 'vdr.c' for copyright information and
 * how to reach the author.
 *
 * $Id: svdrp.h 1.29 2007/04/30 12:28:28 kls Exp $
 */

#ifndef __SVDRP_H
#define __SVDRP_H

#include "recording.h"
#include "tools.h"

class cSocket {
private:
  int port;
  int sock;
  int queue;
  void Close(void);
public:
  cSocket(int Port, int Queue = 1);
  ~cSocket();
  bool Open(void);
  int Accept(void);
  };

class cPUTEhandler {
private:
  FILE *f;
  int status;
  const char *message;
public:
  cPUTEhandler(void);
  ~cPUTEhandler();
  bool Process(const char *s);
  int Status(void) { return status; }
  const char *Message(void) { return message; }
  };

class cSVDRP {
private:
  cSocket socket;
  cFile file;
  cRecordings Recordings;
  cPUTEhandler *PUTEhandler;
  int numChars;
  int length;
  char *cmdLine;
  time_t lastActivity;
  static char *grabImageDir;
  void Close(bool SendReply = false, bool Timeout = false);
  bool Send(const char *s, int length = -1);
  void Reply(int Code, const char *fmt, ...) __attribute__ ((format (printf, 3, 4)));
  void PrintHelpTopics(const char **hp);
  void CmdCHAN(const char *Option);
  void CmdCLRE(const char *Option);
  void CmdDELC(const char *Option);
  void CmdDELR(const char *Option);
  void CmdDELT(const char *Option);
  void CmdEDIT(const char *Option);
  void CmdGRAB(const char *Option);
  void CmdHELP(const char *Option);
  void CmdHITK(const char *Option);
  void CmdLSTC(const char *Option);
  void CmdLSTE(const char *Option);
  void CmdLSTR(const char *Option);
  void CmdLSTT(const char *Option);
  void CmdMESG(const char *Option);
  void CmdMODC(const char *Option);
  void CmdMODT(const char *Option);
  void CmdMOVC(const char *Option);
  void CmdMOVT(const char *Option);
  void CmdNEWC(const char *Option);
  void CmdNEWT(const char *Option);
  void CmdNEXT(const char *Option);
  void CmdPLAY(const char *Option);
  void CmdPLUG(const char *Option);
  void CmdPUTE(const char *Option);
  void CmdREMO(const char *Option);
  void CmdSCAN(const char *Option);
  void CmdSTAT(const char *Option);
  void CmdUPDT(const char *Option);
  void CmdVOLU(const char *Option);
  void Execute(char *Cmd);
public:
  cSVDRP(int Port);
  ~cSVDRP();
  bool HasConnection(void) { return file.IsOpen(); }
  bool Process(void);
  static void SetGrabImageDir(const char *GrabImageDir);
  };

#endif //__SVDRP_H
