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

#ifndef ___DATA_H
#define ___DATA_H

#include <vdr/thread.h>
#include <vdr/tools.h>

#include "misc.h"

class cStructLoaders;
class cLoaders;
class cPidFilter;
class cPlainKeys;

// ----------------------------------------------------------------

class cFileMap : public cSimpleItem, private cMutex {
private:
  char *filename;
  bool rw;
  //
  int fd, len, count;
  unsigned char *addr;
  bool failed;
  //
  void Clean(void);
public:
  cFileMap(const char *Filename, bool Rw);
  ~cFileMap();
  bool Map(void);
  bool Unmap(void);
  void Sync(void);
  int Size(void) const { return len; }
  unsigned char *Addr(void) const { return addr; }
  bool IsFileMap(const char *Name, bool Rw);
  };

// ----------------------------------------------------------------

class cFileMaps : public cSimpleList<cFileMap>, private cMutex {
private:
  char *cfgDir;
public:
  cFileMaps(void);
  ~cFileMaps();
  void SetCfgDir(const char *CfgDir);
  cFileMap *GetFileMap(const char *name, const char *domain, bool rw);
  };

extern cFileMaps filemaps;

//--------------------------------------------------------------

class cStructItem : public cSimpleItem {
private:
  char *comment;
  bool deleted, special;
protected:
  void SetSpecial(void) { special=true; }
public:
  cStructItem(void);
  virtual ~cStructItem();
  virtual cString ToString(bool hide=false)=0;
  bool Save(FILE *f);
  //
  void SetComment(const char *com);
  const char *Comment(void) const { return comment; }
  void Delete(void) { deleted=true; }
  bool Deleted(void) const { return deleted; }
  bool Special(void) const { return special; }
  bool Valid(void) const { return !deleted && !special; }
  };

//--------------------------------------------------------------

class cStructLoader : public cSimpleList<cStructItem> {
friend class cStructLoaders;
private:
  cStructLoader *next;
  //
  cRwLock lock;
  const char *type, *filename;
  char *path;
  time_t mtime;
  bool modified, readwrite, missingok, loaded, disabled, watch, verbose;
  //
  time_t MTime(void);
protected:
  virtual cStructItem *ParseLine(char *line)=0;
  void Modified(bool mod=true) { modified=mod; }
  bool IsModified(void) const { return modified; }
  void ListLock(bool rw) { lock.Lock(rw); }
  void ListUnlock(void) { lock.Unlock(); }
  virtual void PostLoad(void) {}
public:
  cStructLoader(const char *Type, const char *Filename, bool rw, bool miok, bool wat, bool verb);
  virtual ~cStructLoader();
  void AddItem(cStructItem *n, const char *com, cStructItem *ref);
  void DelItem(cStructItem *d, bool keep=false);
  //
  void SetCfgDir(const char *cfgdir);
  bool Load(bool reload);
  void Save(void);
  void Purge(void);
  void Disable(void) { disabled=true; }
  };

//--------------------------------------------------------------

template<class T> class cStructList : public cStructLoader {
public:
  cStructList<T>(const char *Type, const char *Filename, bool rw, bool miok, bool wat, bool verb):cStructLoader(Type,Filename,rw,miok,wat,verb) {}
  T *First(void) const { return (T *)cStructLoader::First(); }
  T *Last(void) const { return (T *)cStructLoader::Last(); }
  T *Next(const T *item) const { return (T *)cStructLoader::Next(item); }
  };

//--------------------------------------------------------------

class cStructLoaders {
friend class cStructLoader;
private:
  static cStructLoader *first;
  static cTimeMs lastReload, lastPurge, lastSave;
  //
  static void Register(cStructLoader *ld);
public:
  static void SetCfgDir(const char *cfgdir);
  static void Load(bool reload);
  static void Save(void);
  static void Purge(void);
  };

// ----------------------------------------------------------------

class cConfRead {
public:
  virtual ~cConfRead() {}
  bool ConfRead(const char *type, const char *filename, bool missingok=false);
  virtual bool ParseLine(const char *line, bool fromCache)=0;
  };

// ----------------------------------------------------------------

class cLoader {
friend class cLoaders;
private:
  cLoader *next;
  bool modified;
  const char *id;
protected:
  void Modified(bool mod=true) { modified=mod; }
public:
  cLoader(const char *Id);
  virtual ~cLoader() {}
  virtual bool ParseLine(const char *line, bool fromCache)=0;
  virtual bool Save(FILE *f)=0;
  bool IsModified(void) const { return modified; }
  const char *Id(void) const { return id; }
  };

// ----------------------------------------------------------------

class cLoaders {
friend class cLoader;
private:
  static cLoader *first;
  static cMutex lock;
  static char *cacheFile;
  //
  static void Register(cLoader *ld);
  static cLoader *FindLoader(const char *id);
  static bool IsModified(void);
public:
  static void LoadCache(const char *cfgdir);
  static void SaveCache(void);
  };

// ----------------------------------------------------------------

class cPid : public cSimpleItem {
public:
  int pid, sct, mask, mode;
  cPidFilter *filter;
  //
  cPid(int Pid, int Section, int Mask, int Mode);
  };

// ----------------------------------------------------------------

class cPids : public cSimpleList<cPid> {
public:
  void AddPid(int Pid, int Section, int Mask, int Mode=0);
  bool HasPid(int Pid, int Section, int Mask, int Mode=0);
  };

// ----------------------------------------------------------------

class cEcmInfo : public cSimpleItem {
private:
  bool cached, failed;
  //
  void Setup(void);
protected:
  int dataLen;
  unsigned char *data;
  //
  void ClearData(void);
public:
  char *name;
  int ecm_pid, ecm_table;
  int caId, provId, emmCaId;
  int prgId, source, transponder;
  //
  cEcmInfo(void);
  cEcmInfo(const cEcmInfo *e);
  cEcmInfo(const char *Name, int Pid, int CaId, int ProvId);
  ~cEcmInfo();
  bool Compare(const cEcmInfo *e);
  bool Update(const cEcmInfo *e);
  void SetSource(int PrgId, int Source, int Transponder);
  void SetName(const char *Name);
  bool AddData(const unsigned char *Data, int DataLen);
  const unsigned char *Data(void) const { return data; }
  void Fail(bool st) { failed=st; }
  bool Failed(void) const { return failed; }
  void SetCached(void) { cached=true; }
  bool Cached(void) const { return cached; }
  };

// ----------------------------------------------------------------

class cMutableKey;

class cPlainKey : public cStructItem {
friend class cPlainKeys;
friend class cMutableKey;
private:
  bool super;
protected:
  void SetSupersede(bool val) { super=val; }
  bool CanSupersede(void) const { return super; }
  virtual int IdSize(void);
  virtual cString Print(void)=0;
  virtual cString PrintKeyNr(void);
  void FormatError(const char *type, const char *sline);
public:
  int type, id, keynr;
  //
  cPlainKey(bool CanSupersede);
  virtual bool Parse(const char *line)=0;
  virtual cString ToString(bool hide=false);
  virtual bool Cmp(void *Key, int Keylen)=0;
  virtual bool Cmp(cPlainKey *k)=0;
  virtual void Get(void *mem)=0;
  virtual int Size(void)=0;
  bool Set(int Type, int Id, int Keynr, void *Key, int Keylen);
  virtual bool SetKey(void *Key, int Keylen)=0;
  virtual bool SetBinKey(unsigned char *Mem, int Keylen)=0;
  };

// ----------------------------------------------------------------

class cMutableKey : public cPlainKey {
private:
  cPlainKey *real;
protected:
  virtual cString Print(void);
  virtual cPlainKey *Alloc(void) const=0;
public:
  cMutableKey(bool Super);
  virtual ~cMutableKey();
  virtual bool Cmp(void *Key, int Keylen);
  virtual bool Cmp(cPlainKey *k);
  virtual void Get(void *mem);
  virtual int Size(void);
  virtual bool SetKey(void *Key, int Keylen);
  virtual bool SetBinKey(unsigned char *Mem, int Keylen);
  };

// ----------------------------------------------------------------

class cPlainKeyType {
friend class cPlainKeys;
private:
  int type;
  cPlainKeyType *next;
public:
  cPlainKeyType(int Type, bool Super);
  virtual ~cPlainKeyType() {}
  virtual cPlainKey *Create(void)=0;
  };

// ----------------------------------------------------------------

template<class PKT, int KT, bool SUP=true> class cPlainKeyTypeReg : public cPlainKeyType {
public:
  cPlainKeyTypeReg(void):cPlainKeyType(KT,SUP) {}
  virtual cPlainKey *Create(void) { return new PKT(SUP); }
  };

// ----------------------------------------------------------------

class cLastKey {
private:
  int lastType, lastId, lastKeynr;
public:
  cLastKey(void);
  bool NotLast(int Type, int Id, int Keynr);
  };

// ----------------------------------------------------------------

extern const char *externalAU;

class cPlainKeys : private cThread, public cStructList<cPlainKey> {
friend class cPlainKeyType;
private:
  static cPlainKeyType *first;
  cTimeMs trigger, last;
  cLastKey lastkey;
  //
  static void Register(cPlainKeyType *pkt, bool Super);
  cPlainKey *NewFromType(int type);
  bool AddNewKey(cPlainKey *nk, const char *reason);
  void ExternalUpdate(void);
protected:
  virtual void Action(void);
  virtual void PostLoad(void);
public:
  cPlainKeys(void);
  virtual cPlainKey *ParseLine(char *line);
  cPlainKey *FindKey(int Type, int Id, int Keynr, int Size, cPlainKey *key=0);
  cPlainKey *FindKeyNoTrig(int Type, int Id, int Keynr, int Size, cPlainKey *key=0);
  void Trigger(int Type, int Id, int Keynr);
  cString KeyString(int Type, int Id, int Keynr);
  bool NewKey(int Type, int Id, int Keynr, void *Key, int Keylen);
  bool NewKeyParse(char *line, const char *reason);
  void HouseKeeping(void);
  };

extern cPlainKeys keys;

#endif //___DATA_H
