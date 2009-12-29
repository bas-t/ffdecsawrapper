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

#ifndef ___OVERRIDE_H
#define ___OVERRIDE_H

#include "data.h"

// ----------------------------------------------------------------

class cValidityRange {
private:
  int fromCaid, toCaid;
  int fromSource, toSource;
  int fromFreq, toFreq;
  bool wildcard;
  //
  bool ParseCaidRange(const char *str);
  bool ParseSourceRange(const char *str);
  bool ParseFreqRange(const char *str);
protected:
  char *Parse3(char *s);
  char *Parse2(char *s, bool wildAllow=false);
  cString Print(void);
public:
  cValidityRange(void);
  bool Match(int caid, int source, int freq) const;
  };

// ----------------------------------------------------------------

class cRewriter {
private:
  unsigned char *mem;
  int mlen;
protected:
  const char *name;
  int id;
  //
  unsigned char *Alloc(int len);
public:
  cRewriter(const char *Name, int Id);
  virtual ~cRewriter();
  virtual bool Rewrite(unsigned char *&data, int &len)=0;
  int Id(void) const { return id; }
  };

// ----------------------------------------------------------------

class cRewriters {
public:
  static cRewriter *CreateById(int id);
  static int GetIdByName(const char *name);
  };

// ----------------------------------------------------------------

class cOverride : public cStructItem, public cValidityRange {
protected:
  int type;
public:
  virtual ~cOverride() {}
  virtual bool Parse(char *str)=0;
  int Type(void) { return type; }
  };

// ----------------------------------------------------------------

class cOverrides : public cStructList<cOverride> {
private:
  bool caidTrigger;
protected:
  cOverride *Find(int type, int caid, int source, int transponder, cOverride *ov=0);
  virtual cOverride *ParseLine(char *line);
  virtual void PreLoad(void);
  virtual void PostLoad(void);
public:
  cOverrides(void);
  int GetCat(int source, int transponder, unsigned char *buff, int len);
  void UpdateEcm(cEcmInfo *ecm, bool log);
  bool AddEmmPids(int caid, int source, int transponder, cPids *pids, int pid);
  bool Ignore(int source, int transponder, int caid);
  int GetEcmPrio(int source, int transponder, int caid, int prov);
  };

extern cOverrides overrides;

#endif //___OVERRIDE_H
