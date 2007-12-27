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

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <vdr/tools.h>

#include "data.h"
#include "misc.h"
#include "scsetup.h"
#include "log-core.h"

#define KEY_FILE     "SoftCam.Key"
#define CACACHE_FILE "ca.cache"
#define EXT_AU_INT   (15*60*1000) // ms interval for external AU
#define EXT_AU_MIN   ( 2*60*1000) // ms min. interval for external AU

// -- cFileMap -----------------------------------------------------------------

cFileMap::cFileMap(const char *Filename, bool Rw)
{
  filename=strdup(Filename);
  rw=Rw;
  fd=-1; count=len=0; addr=0; failed=false;
}

cFileMap::~cFileMap()
{
  Clean();
  free(filename);
}

bool cFileMap::IsFileMap(const char *Name, bool Rw)
{
  return (!strcmp(Name,filename) && (!Rw || rw));
}

void cFileMap::Clean(void)
{
  if(addr) { munmap(addr,len); addr=0; len=0; }
  if(fd>=0) { close(fd); fd=-1; }
}

bool cFileMap::Map(void)
{
  cMutexLock lock(this);
  if(addr) { count++; return true; }
  if(!failed) {
    struct stat64 ds;
    if(!stat64(filename,&ds)) {
      if(S_ISREG(ds.st_mode)) {
        fd=open(filename,rw ? O_RDWR : O_RDONLY);
        if(fd>=0) {
          unsigned char *map=(unsigned char *)mmap(0,ds.st_size,rw ? (PROT_READ|PROT_WRITE):(PROT_READ),MAP_SHARED,fd,0);
          if(map!=MAP_FAILED) {
            addr=map; len=ds.st_size; count=1;
            return true;
            }
          else PRINTF(L_GEN_ERROR,"mapping failed on %s: %s",filename,strerror(errno));
          close(fd); fd=-1;
          }
        else PRINTF(L_GEN_ERROR,"error opening filemap %s: %s",filename,strerror(errno));
        }
      else PRINTF(L_GEN_ERROR,"filemap %s is not a regular file",filename);
      }
    else PRINTF(L_GEN_ERROR,"can't stat filemap %s: %s",filename,strerror(errno));
    failed=true; // don't try this one over and over again
    }
  return false;
}

bool cFileMap::Unmap(void)
{
  cMutexLock lock(this);
  if(addr) {
    if(!(--count)) { Clean(); return true; }
    else Sync();
    }
  return false;
}

void cFileMap::Sync(void)
{
  cMutexLock lock(this);
  if(addr) msync(addr,len,MS_ASYNC);
}

// -- cFileMaps ----------------------------------------------------------------

cFileMaps filemaps;

cFileMaps::cFileMaps(void)
{
  cfgDir=0;
}

cFileMaps::~cFileMaps()
{
  Clear();
  free(cfgDir);
}

void cFileMaps::SetCfgDir(const char *CfgDir)
{
  free(cfgDir);
  cfgDir=strdup(CfgDir);
}

cFileMap *cFileMaps::GetFileMap(const char *name, const char *domain, bool rw)
{
  cMutexLock lock(this);
  char path[256];
  snprintf(path,sizeof(path),"%s/%s/%s",cfgDir,domain,name);
  cFileMap *fm=First();
  while(fm) {
    if(fm->IsFileMap(path,rw)) return fm;
    fm=Next(fm);
    }
  fm=new cFileMap(path,rw);
  Add(fm);
  return fm;  
}

// -- cConfRead ----------------------------------------------------------------

bool cConfRead::ConfRead(const char *type, const char *filename, bool missingok)
{
  bool res=false;
  FILE *f=fopen(filename,"r");
  if(f) {
    res=true;
    PRINTF(L_GEN_INFO,"loading %s from %s",type,filename);
    char buff[1024];
    while(fgets(buff,sizeof(buff),f)) {
      if(!index(buff,'\n') && !feof(f))
        PRINTF(L_GEN_ERROR,"confread %s fgets readbuffer overflow",type);
      char *line=skipspace(stripspace(buff));
      if(line[0]==0 || line[0]==';' || line[0]=='#') continue; // skip empty & comment lines
      if(!ParseLine(line,false)) {
        PRINTF(L_GEN_ERROR,"file '%s' has error in line '%s'",filename,buff);
        res=false;
        }
      }
    fclose(f);
    }
  else if(!missingok) PRINTF(L_GEN_ERROR,"Failed to open file '%s': %s",filename,strerror(errno));
  return res;
}

// -- cLineDummy ---------------------------------------------------------------

class cLineDummy : public cSimpleItem {
private:
  char *store;
public:
  cLineDummy(void);
  ~cLineDummy();
  bool Parse(const char *line);
  bool Save(FILE *f);
  };

cLineDummy::cLineDummy(void)
{
  store=0;
}

cLineDummy::~cLineDummy()
{
  free(store);
}

bool cLineDummy::Parse(const char *line)
{
  free(store);
  store=strdup(line);
  return store!=0;
}

bool cLineDummy::Save(FILE *f)
{
  fprintf(f,"%s",store);
  return ferror(f)==0;
}

// -- cLoaderDummy -------------------------------------------------------------

class cLoaderDummy : public cSimpleList<cLineDummy>, public cLoader {
public:
  cLoaderDummy(const char *Id);
  virtual bool ParseLine(const char *line, bool fromCache);
  virtual bool Save(FILE *f);
  };

cLoaderDummy::cLoaderDummy(const char *id)
:cLoader(id)
{}

bool cLoaderDummy::ParseLine(const char *line, bool fromCache)
{
  if(fromCache) {
    cLineDummy *k=new cLineDummy;
    if(k) {
      if(k->Parse(line)) Add(k);
      else delete k;
      return true;
      }
    PRINTF(L_GEN_ERROR,"not enough memory for %s loader dummy!",Id());
    }
  return false;
}

bool cLoaderDummy::Save(FILE *f)
{
  bool res=true;
  for(cLineDummy *k=First(); k; k=Next(k))
    if(!k->Save(f)) { res=false; break; }
  Modified(!res);
  return res;
}

// -- cLoader ------------------------------------------------------------------

cLoader::cLoader(const char *Id)
{
  id=Id; modified=false;
  cLoaders::Register(this);
}

// -- cLoaders -----------------------------------------------------------------

cLoader *cLoaders::first=0;
cMutex cLoaders::lock;
char *cLoaders::cacheFile=0;

void cLoaders::Register(cLoader *ld)
{
  PRINTF(L_CORE_DYN,"loaders: registering loader %s",ld->id);
  ld->next=first;
  first=ld;
}

void cLoaders::LoadCache(const char *cfgdir)
{
  lock.Lock();
  cacheFile=strdup(AddDirectory(cfgdir,CACACHE_FILE));
  if(access(cacheFile,F_OK)==0) {
    PRINTF(L_GEN_INFO,"loading ca cache from %s",cacheFile);
    FILE *f=fopen(cacheFile,"r");
    if(f) {
      char buf[512];
      cLoader *ld=0;
      while(fgets(buf,sizeof(buf),f)) {
        if(!index(buf,'\n'))
          PRINTF(L_GEN_ERROR,"loaders fgets readbuffer overflow");
        if(buf[0]=='#') continue;
        if(!strncmp(buf,":::",3)) { // new loader section
          ld=FindLoader(stripspace(&buf[3]));
          if(!ld) {
            PRINTF(L_CORE_LOAD,"unknown loader section '%s', adding dummy",&buf[3]);
            ld=new cLoaderDummy(strdup(&buf[3]));
            }
          }
        else if(ld) {
          if(!ld->ParseLine(buf,true)) {
            PRINTF(L_CORE_LOAD,"loader '%s' failed on line '%s'",ld->Id(),buf);
            }
          }
        }
      fclose(f);
      }
    else LOG_ERROR_STR(cacheFile);
    }
  lock.Unlock();
}

void cLoaders::SaveCache(void)
{
  lock.Lock();
  if(cacheFile && IsModified()) {
    cSafeFile f(cacheFile);
    if(f.Open()) {
      fprintf(f,"## This is a generated file. DO NOT EDIT!!\n"
                "## This file will be OVERWRITTEN WITHOUT WARNING!!\n");

      cLoader *ld=first;
      while(ld) {
        fprintf(f,":::%s\n",ld->Id());
        if(!ld->Save(f)) break;
        ld=ld->next;
        }
      f.Close();
      PRINTF(L_CORE_LOAD,"saved cache to file");
      }
    }
  lock.Unlock();
}

bool cLoaders::IsModified(void)
{
  bool res=false;
  lock.Lock();
  cLoader *ld=first;
  while(ld) {
    if(ld->IsModified()) { 
      res=true; break;
      }
    ld=ld->next;
    }
  lock.Unlock();
  return res;
}

cLoader *cLoaders::FindLoader(const char *id)
{
  lock.Lock();
  cLoader *ld=first;
  while(ld) {
    if(!strcmp(id,ld->Id())) break;
    ld=ld->next;
    }
  lock.Unlock();
  return ld;
}

// -- cPid ---------------------------------------------------------------------

cPid::cPid(int Pid, int Section, int Mask, int Mode)
{
  pid=Pid;
  sct=Section;
  mask=Mask;
  mode=Mode;
  filter=0;
}

// -- cPids --------------------------------------------------------------------

void cPids::AddPid(int Pid, int Section, int Mask, int Mode)
{
  if(!HasPid(Pid,Section,Mask,Mode)) {
    cPid *pid=new cPid(Pid,Section,Mask,Mode);
    Add(pid);
    }
}

bool cPids::HasPid(int Pid, int Section, int Mask, int Mode)
{
  for(cPid *pid=First(); pid; pid=Next(pid))
    if(pid->pid==Pid && pid->sct==Section && pid->mask==Mask && pid->mode==Mode)
      return true;
  return false;
}

// -- cEcmInfo -----------------------------------------------------------------

cEcmInfo::cEcmInfo(void)
{
  Setup();
}

cEcmInfo::cEcmInfo(const cEcmInfo *e)
{
  Setup(); SetName(e->name);
  ecm_pid=e->ecm_pid;
  ecm_table=e->ecm_table;
  caId=e->caId;
  emmCaId=e->emmCaId;
  provId=e->provId;
  Update(e);
  prgId=e->prgId;
  source=e->source;
  transponder=e->transponder;
}

cEcmInfo::cEcmInfo(const char *Name, int Pid, int CaId, int ProvId)
{
  Setup(); SetName(Name);
  ecm_pid=Pid;
  caId=CaId;
  provId=ProvId;
}

cEcmInfo::~cEcmInfo()
{
  ClearData();
  free(name);
}

void cEcmInfo::Setup(void)
{
  cached=failed=false;
  name=0; data=0;
  prgId=source=transponder=-1;
  ecm_table=0x80; emmCaId=0;
}

bool cEcmInfo::Compare(const cEcmInfo *e)
{
  return prgId==e->prgId && source==e->source && transponder==e->transponder &&
         caId==e->caId && ecm_pid==e->ecm_pid && provId==e->provId;
}

bool cEcmInfo::Update(const cEcmInfo *e)
{
  return (e->data && (!data || e->dataLen!=dataLen)) ? AddData(e->data,e->dataLen) : false;
}

void cEcmInfo::SetSource(int PrgId, int Source, int Transponder)
{
  prgId=PrgId;
  source=Source;
  transponder=Transponder;
}

void cEcmInfo::ClearData(void)
{
  free(data); data=0;
}

bool cEcmInfo::AddData(const unsigned char *Data, int DataLen)
{
  ClearData();
  data=MALLOC(unsigned char,DataLen);
  if(data) {
    memcpy(data,Data,DataLen);
    dataLen=DataLen;
    }
  else PRINTF(L_GEN_ERROR,"malloc failed in cEcmInfo::AddData()");
  return (data!=0);
}

void cEcmInfo::SetName(const char *Name)
{
  free(name);
  name=strdup(Name);
}

// -- cPlainKey ----------------------------------------------------------------

cPlainKey::cPlainKey(bool CanSupersede)
{
  au=del=false;
  super=CanSupersede;
}

bool cPlainKey::Set(int Type, int Id, int Keynr, void *Key, int Keylen)
{
  type=Type; id=Id; keynr=Keynr;
  return SetKey(Key,Keylen);
}

cString cPlainKey::PrintKeyNr(void)
{
  return cString::sprintf("%02X",keynr);
}

int cPlainKey::IdSize(void)
{
  return id>0xFF ? (id>0xFFFF ? 6 : 4) : 2;
}

bool cPlainKey::Save(FILE *f)
{
  fprintf(f,"%s\n",*ToString(false));
  return ferror(f)==0;
}

cString cPlainKey::ToString(bool hide)
{
  return cString::sprintf(hide ? "%c %.*X %s %.4s..." : "%c %.*X %s %s",type,IdSize(),id,*PrintKeyNr(),*Print());
}

void cPlainKey::FormatError(const char *type, const char *sline)
{
  PRINTF(L_GEN_WARN,"%s key: bad format '%.15s%s'\n",type,sline,(strlen(sline)>15)?"...":"");
}

// -- cMutableKey --------------------------------------------------------------

cMutableKey::cMutableKey(bool Super)
:cPlainKey(Super)
{
  real=0;
}

cMutableKey::~cMutableKey()
{
  delete real;
}

bool cMutableKey::SetKey(void *Key, int Keylen)
{
  delete real;
  return (real=Alloc()) && real->SetKey(Key,Keylen);
}

bool cMutableKey::SetBinKey(unsigned char *Mem, int Keylen)
{
  delete real;
  return (real=Alloc()) && real->SetBinKey(Mem,Keylen);
}

int cMutableKey::Size(void)
{
  return real->Size();
}

bool cMutableKey::Cmp(void *Key, int Keylen)
{
  return real->Cmp(Key,Keylen);
}

bool cMutableKey::Cmp(cPlainKey *k)
{
  cMutableKey *mk=dynamic_cast<cMutableKey *>(k); // high magic ;)
  return real->Cmp(mk ? mk->real : k);
}

void cMutableKey::Get(void *mem)
{
  real->Get(mem);
}

cString cMutableKey::Print(void)
{
  return real->Print();
}

// ----------------------------------------------------------------

class cPlainKeyDummy : public cPlainKey {
private:
  char *str;
  int len;
protected:
  virtual cString Print(void);
  virtual cString PrintKeyNr(void) { return ""; }
public:
  cPlainKeyDummy(void);
  ~cPlainKeyDummy();
  virtual bool Parse(const char *line);
  virtual bool Cmp(void *Key, int Keylen) { return false; }
  virtual bool Cmp(cPlainKey *k) { return false; }
  virtual void Get(void *mem) {}
  virtual int Size(void) { return len; }
  virtual bool SetKey(void *Key, int Keylen);
  virtual bool SetBinKey(unsigned char *Mem, int Keylen);
  };

cPlainKeyDummy::cPlainKeyDummy(void):
cPlainKey(false)
{
  str=0;
}

cPlainKeyDummy::~cPlainKeyDummy()
{
  free(str);
}

bool cPlainKeyDummy::SetKey(void *Key, int Keylen)
{
  len=Keylen;
  free(str);
  str=MALLOC(char,len+1);
  if(str) strn0cpy(str,(char *)Key,len+1);
  return str!=0;
}

bool cPlainKeyDummy::SetBinKey(unsigned char *Mem, int Keylen)
{
  return SetKey(Mem,Keylen);
}

cString cPlainKeyDummy::Print(void)
{
  return str;
}

bool cPlainKeyDummy::Parse(const char *line)
{
  unsigned char sid[3];
  int len;
  if(GetChar(line,&type,1) && (len=GetHex(line,sid,3,false))) {
    type=toupper(type); id=Bin2Int(sid,len);
    char *l=strdup(line);
    line=skipspace(stripspace(l));
    SetKey((void *)line,strlen(line));
    free(l);
    return true;
    }
  return false;
}

// -- cPlainKeyTypeDummy -------------------------------------------------------

class cPlainKeyTypeDummy : public cPlainKeyType {
public:
  cPlainKeyTypeDummy(int Type):cPlainKeyType(Type,false) {}
  virtual cPlainKey *Create(void) { return new cPlainKeyDummy; }
  };

// -- cPlainKeyType ------------------------------------------------------------

cPlainKeyType::cPlainKeyType(int Type, bool Super)
{
  type=Type;
  cPlainKeys::Register(this,Super);
}

// -- cPlainKeys ---------------------------------------------------------------

const char *externalAU=0;

cPlainKeys keys;

cPlainKeyType *cPlainKeys::first=0;

cPlainKeys::cPlainKeys(void)
:cLoader("KEY")
//,cThread("ExternalAU")
{
  mark=0;
}

void cPlainKeys::Register(cPlainKeyType *pkt, bool Super)
{
  PRINTF(L_CORE_DYN,"registering key type %c%s",pkt->type,Super?" (super)":"");
  pkt->next=first;
  first=pkt;
}

cPlainKey *cPlainKeys::FindKey(int Type, int Id, int Keynr, int Size, cPlainKey *key)
{
  key=FindKeyNoTrig(Type,Id,Keynr,Size,key);
  if(!key) {
    static int lastType=-1, lastId=-1, lastKeynr=-1;
    if(externalAU && (lastType!=Type || lastId!=Id || lastKeynr!=Keynr)) {
      PRINTF(L_CORE_AUEXTERN,"triggered from findkey (type=%X id=%X keynr=%X)",Type,Id,Keynr);
      lastType=Type; lastId=Id; lastKeynr=Keynr;
      }
    ExternalUpdate();
    }
  return key;
}

cPlainKey *cPlainKeys::FindKeyNoTrig(int Type, int Id, int Keynr, int Size, cPlainKey *key)
{
  Lock();
  if(key) key=Next(key); else key=First();
  while(key) {
    if(!key->IsInvalid() && key->type==Type && key->id==Id && key->keynr==Keynr && (Size<0 || key->Size()==Size)) break;
    key=Next(key);
    }
  Unlock();
  return key;
}

bool cPlainKeys::NewKey(int Type, int Id, int Keynr, void *Key, int Keylen)
{
  cPlainKey *k=0;
  while((k=FindKeyNoTrig(Type,Id,Keynr,-1,k)))
    if(k->Cmp(Key,Keylen)) return false;

  cPlainKey *nk=NewFromType(Type);
  if(nk) {
    nk->Set(Type,Id,Keynr,Key,Keylen);
    AddNewKey(nk,2,true);
    return true;
    }
  else PRINTF(L_GEN_ERROR,"no memory for new key ID %c %.2x!",Type,Id);
  return false;
}

void cPlainKeys::AddNewKey(cPlainKey *nk, int mode, bool log)
{
  if(mode>=1) {
    nk->SetAuto();
    if(log) PRINTF(L_GEN_INFO,"key update for ID %s",*nk->ToString(true));
    if(nk->CanSupersede()) {
      cPlainKey *k=0;
      while((k=FindKeyNoTrig(nk->type,nk->id,nk->keynr,nk->Size(),k))) {
        if(!k->IsInvalid()) {
          k->SetInvalid();
          if(k->IsAuto()) Modified();
          if(log) PRINTF(L_GEN_INFO,"supersedes key: %s%s",*k->ToString(true),k->IsAuto()?" (auto)":"");
          }
        }
      }
    }
  Lock();
  switch(mode) {
    case 0: Add(nk); break;
    case 1: if(!mark) Ins(nk); else Add(nk,mark);
            mark=nk;
            break;
    case 2: Ins(nk); Modified(); break;
    }
  Unlock();
}

bool cPlainKeys::Load(const char *cfgdir)
{
  Lock();
  Clear(); mark=0;
  cString cname=AddDirectory(cfgdir,KEY_FILE);
  ConfRead("keys",cname);
  int n=Count();
  PRINTF(L_CORE_LOAD,"loaded %d keys from %s",n,*cname);
  if(n && LOG(L_CORE_KEYS)) {
    cPlainKey *dat=First();
    while(dat) {
      if(!dat->IsInvalid()) PRINTF(L_CORE_KEYS,"keys %s",*dat->ToString(false));
      dat=Next(dat);
      }
    }
  Unlock();
  return (n!=0);
}

void cPlainKeys::HouseKeeping(void)
{
  cLoaders::SaveCache();
  if(trigger.TimedOut()) {
    trigger.Set(EXT_AU_INT);
    if(externalAU) PRINTF(L_CORE_AUEXTERN,"triggered from housekeeping");
    ExternalUpdate();
    }
}

void cPlainKeys::ExternalUpdate(void)
{
  if(externalAU && ScSetup.AutoUpdate>0) {
    if(last.TimedOut()) {
      Lock();
      if(!Active())
        Start();
      else PRINTF(L_CORE_AUEXTERN,"still running");
      Unlock();
      }
    else PRINTF(L_CORE_AUEXTERN,"denied, min. timeout not expired");
    }
}

bool cPlainKeys::NewKeyParse(const char *line)
{
  cPlainKey *nk=NewFromType(toupper(line[0]));
  if(nk && nk->Parse(line)) {
    cPlainKey *k=0;
    while((k=FindKeyNoTrig(nk->type,nk->id,nk->keynr,-1,k)))
      if(k->Cmp(nk)) break;
    if(!k) {
      AddNewKey(nk,2,true);
      return true;
      }
    }
  return false;
}

void cPlainKeys::Action(void)
{
  last.Set(EXT_AU_MIN);
  PRINTF(L_CORE_AUEXTERN,"starting...");
  cTimeMs start;
  cPipe pipe;
  if(pipe.Open(externalAU,"r")) {
    char buff[1024];
    while(fgets(buff,sizeof(buff),pipe)) {
      char *line=skipspace(stripspace(buff));
      if(line[0]==0 || line[0]==';' || line[0]=='#') continue;
      NewKeyParse(line);
      }
    }
  pipe.Close();
  PRINTF(L_CORE_AUEXTERN,"done (elapsed %d)",(int)start.Elapsed());
}

cPlainKey *cPlainKeys::NewFromType(int type)
{
  cPlainKeyType *pkt=first;
  while(pkt) {
    if(pkt->type==type) return pkt->Create();
    pkt=pkt->next;
    }
  PRINTF(L_CORE_LOAD,"unknown key type '%c', adding dummy",type);
  pkt=new cPlainKeyTypeDummy(type);
  return pkt->Create();
}

bool cPlainKeys::ParseLine(const char *line, bool fromCache)
{
  char *s=skipspace(line);
  cPlainKey *k=NewFromType(toupper(*s));
  if(k) {
    if(k->Parse((char *)line)) AddNewKey(k,fromCache?1:0,false);
    else delete k;
    return true;
    }
  return false;
}

bool cPlainKeys::Save(FILE *f)
{
  bool res=true;
  Lock();
  cPlainKey *dat=First();
  while(dat) {
    if(dat->IsAuto() && !dat->IsInvalid()) {
      if(!dat->Save(f)) { res=false; break; }
      }
    dat=Next(dat);
    }
  Modified(!res);
  Unlock();
  return res;
}
