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

#include <malloc.h>
#include <stdlib.h>
#include <limits.h>
#include <getopt.h>
#include <typeinfo>
#ifndef STATICBUILD
#include <dlfcn.h>
#include <dirent.h>
#include <fnmatch.h>
#endif

#include <vdr/plugin.h>
#include <vdr/menuitems.h>
#include <vdr/status.h>
#include <vdr/dvbdevice.h>
#include <vdr/channels.h>
#include <vdr/interface.h>
#include <vdr/menu.h>
#include <vdr/tools.h>
#include <vdr/config.h>

#include "scsetup.h"
#include "filter.h"
#include "system.h"
#include "cam.h"
#include "global.h"
#include "device.h"
#include "smartcard.h"
#include "data.h"
#include "network.h"
#include "misc.h"
#include "opts.h"
#include "i18n.h"
#include "log-core.h"
#include "version.h"

#define MIN_VERS   1 // required VDR version
#define MIN_MAJOR  6
#define MIN_MINOR  0
#define MINAPIVERSNUM 10600

// some sanity checks
#ifdef HAVE_SOFTCSA
#error softcsa/ffdecsa patch MUST NOT be applied. Next time read the README first.
#endif
#ifdef VDR_IS_SC_PATCHED
#error You MUST NOT patch the VDR core. Next time read the README first.
#endif
#if APIVERSNUM<MINAPIVERSNUM
#error Your VDR API version is too old. See README.
#endif

static cPlugin *ScPlugin;
static cOpts *ScOpts, *LogOpts;
static const char * const cfgsub="sc";

static const struct LogModule lm_core = {
  (LMOD_ENABLE|L_CORE_ALL)&LOPT_MASK,
  (LMOD_ENABLE|L_CORE_LOAD|L_CORE_ECM|L_CORE_PIDS|L_CORE_AU|L_CORE_AUSTATS|L_CORE_NET|L_CORE_CI|L_CORE_SC|L_CORE_HOOK|L_CORE_OVER)&LOPT_MASK,
  "core",
  { "load","action","ecm","ecmProc","pids","au","auStats","auExtra","auExtern",
    "caids","keys","dynamic","csa","ci","av7110","net","netData","msgcache",
    "serial","smartcard","hook","ciFull","csaVerb","override" }
  };
ADD_MODULE(L_CORE,lm_core)

// --- cMenuEditCapItem --------------------------------------------------------

class cMenuEditCapItem : public cMenuEditIntItem {
protected:
  virtual void Set(void);
public:
  cMenuEditCapItem(const char *Name, int *Value);
  eOSState ProcessKey(eKeys Key);
  };

cMenuEditCapItem::cMenuEditCapItem(const char *Name, int *Value)
:cMenuEditIntItem(Name, Value, 0)
{
  Set();
}

void cMenuEditCapItem::Set(void)
{
  if(!*value) SetValue(tr("off"));
  else cMenuEditIntItem::Set();
}

eOSState cMenuEditCapItem::ProcessKey(eKeys Key)
{
  eOSState state = cMenuEditItem::ProcessKey(Key);
  if(state==osUnknown)
    state=cMenuEditIntItem::ProcessKey(Key);
  return state;
}

// --- cMenuEditHexItem ------------------------------------------------------

class cMenuEditHexItem : public cMenuEditItem {
private:
  bool abc, isOn;
  //
  void SetButtons(bool on);
protected:
  int *value;
  int min, max;
  //
  virtual void Set(void);
public:
  cMenuEditHexItem(const char *Name, int *Value, int Min=0, int Max=INT_MAX);
  virtual eOSState ProcessKey(eKeys Key);
  };

cMenuEditHexItem::cMenuEditHexItem(const char *Name, int *Value, int Min, int Max)
:cMenuEditItem(Name)
{
  value=Value; min=Min; max=Max;
  if(*value<min) *value=min;
  else if(*value>max) *value=max;
  Set();
  abc=true; isOn=false;
}

void cMenuEditHexItem::SetButtons(bool on)
{
  if(on) {
    if(abc) cSkinDisplay::Current()->SetButtons("A","B","C","D-F");
    else    cSkinDisplay::Current()->SetButtons("D","E","F","A-C");
    isOn=true;
    }
  else {
    cSkinDisplay::Current()->SetButtons(0);
    isOn=false;
    }
}

void cMenuEditHexItem::Set(void)
{
   char buf[16];
   snprintf(buf,sizeof(buf),"%X",*value);
   SetValue(buf);
}

eOSState cMenuEditHexItem::ProcessKey(eKeys Key)
{
  switch(NORMALKEY(Key)) {
    case kUp:
    case kDown:
      if(isOn) SetButtons(false);
      break;
    default:
      if(!isOn) SetButtons(true);
      break;
    }
  eOSState state=cMenuEditItem::ProcessKey(Key);
  if(state!=osUnknown) return state;

  int newValue=*value;
  bool IsRepeat=Key & k_Repeat;
  Key=NORMALKEY(Key);
  switch(Key) {
    case kBlue:
      abc=!abc; SetButtons(true);
      break;
    case kRed:
    case kGreen:
    case kYellow:
    case k0 ... k9:
      {
      if(fresh) { newValue=0; fresh=false; }
      int add;
      if(Key>=kRed && Key<=kYellow) add=(abc ? 10:13)+(Key-kRed);
      else                          add=(Key-k0);
      newValue=newValue*16+add;
      break;
      }
    case kLeft:
      newValue=*value-1; fresh=true;
      if(!IsRepeat && newValue<min) newValue=max;
      break;
    case kRight:
      newValue=*value+1; fresh=true;
      if(!IsRepeat && newValue>max) newValue=min;
      break;
    default:
      if(*value<min) { *value=min; Set(); }
      if(*value>max) { *value=max; Set(); }
      return osUnknown;
    }
  if(newValue!=*value && (!fresh || min<=newValue) && newValue<=max) {
     *value=newValue;
     Set();
     }
  return osContinue;
}

// --- cScInfoItem -------------------------------------------------------------

class cScInfoItem : public cOsdItem {
private:
  void SetValue(const char *Name, const char *Value);
  //
  int ident;
public:
  cScInfoItem(const char *Name, int Value, eOSState State=osUnknown);
  cScInfoItem(const char *Name, const char *Value=0, eOSState State=osUnknown);
  void Ident(int id) { ident=id; }
  int Ident(void) { return ident; }
  };

cScInfoItem::cScInfoItem(const char *Name, int Value, eOSState State)
:cOsdItem(State)
{
  char buf[16];
  snprintf(buf,sizeof(buf),"%d",Value);
  SetValue(Name,buf);
  if(State==osUnknown) SetSelectable(false);
}

cScInfoItem::cScInfoItem(const char *Name, const char *Value, eOSState State)
:cOsdItem(State)
{
  SetValue(Name,Value);
  if(State==osUnknown) SetSelectable(false);
}

void cScInfoItem::SetValue(const char *Name, const char *Value)
{
  char *buff=bprintf(Value ? "%s:\t%s":"%s",Name,Value);
  SetText(buff,false);
  cStatus::MsgOsdCurrentItem(buff);
  ident=-1;
}

// --- cOpt --------------------------------------------------------------------

cOpt::cOpt(const char *Name, const char *Title)
{
  name=Name; title=Title;
  fullname=0; persistant=true;
}

cOpt::~cOpt()
{
  free(fullname);
}

const char *cOpt::FullName(const char *PreStr)
{
  if(PreStr) {
    free(fullname);
    fullname=bprintf("%s.%s",PreStr,name);
    return fullname;
    }
  else return name;
}

// --- cOptInt -----------------------------------------------------------------

cOptInt::cOptInt(const char *Name, const char *Title, int *Storage, int Min, int Max)
:cOpt(Name,Title)
{
  storage=Storage; min=Min; max=Max;
}

void cOptInt::Parse(const char *Value)
{
  *storage=atoi(Value);
}

void cOptInt::Backup(void)
{
  value=*storage;
}

bool cOptInt::Set(void)
{
  if(value!=*storage) { *storage=value; return true; }
  return false;
}

void cOptInt::Store(const char *PreStr)
{
  ScPlugin->SetupStore(FullName(PreStr),*storage);
}

void cOptInt::Create(cOsdMenu *menu)
{
  menu->Add(new cMenuEditIntItem(tr(title),&value,min,max));
}

// --- cOptSel -----------------------------------------------------------------

cOptSel::cOptSel(const char *Name, const char *Title, int *Storage, int NumStr, const char * const *Strings)
:cOptInt(Name,Title,Storage,0,NumStr)
{
  strings=Strings;
  trStrings=0;
}

cOptSel::~cOptSel()
{
  free(trStrings);
}

void cOptSel::Create(cOsdMenu *menu)
{
  free(trStrings);
  if((trStrings=MALLOC(const char *,max))) {
    for(int i=0; i<max ; i++) trStrings[i]=tr(strings[i]);
    menu->Add(new cMenuEditStraItem(tr(title),&value,max,trStrings));
    }
}

// --- cOptBool -----------------------------------------------------------------

cOptBool::cOptBool(const char *Name, const char *Title, int *Storage)
:cOptInt(Name,Title,Storage,0,1)
{}

void cOptBool::Create(cOsdMenu *menu)
{
  menu->Add(new cMenuEditBoolItem(tr(title),&value));
}

// --- cOptStr -----------------------------------------------------------------

cOptStr::cOptStr(const char *Name, const char *Title, char *Storage, int Size, const char *Allowed)
:cOpt(Name,Title)
{
  storage=Storage; size=Size; allowed=Allowed;
  value=MALLOC(char,size);
}

cOptStr::~cOptStr()
{
  free(value);
}

void cOptStr::Parse(const char *Value)
{
  strn0cpy(storage,Value,size);
}

void cOptStr::Backup(void)
{
  strn0cpy(value,storage,size);
}

bool cOptStr::Set(void)
{
  if(strcmp(value,storage)) { strn0cpy(storage,value,size); return true; }
  return false;
}

void cOptStr::Store(const char *PreStr)
{
  ScPlugin->SetupStore(FullName(PreStr),storage);
}

void cOptStr::Create(cOsdMenu *menu)
{
  menu->Add(new cMenuEditStrItem(tr(title),value,size,allowed));
}

// --- cOptMInt ----------------------------------------------------------------

class cOptMInt : public cOpt {
protected:
  int *storage, *value;
  int size, mode, len;
public:
  cOptMInt(const char *Name, const char *Title, int *Storage, int Size, int Mode);
  virtual ~cOptMInt();
  virtual void Parse(const char *Value);
  virtual void Backup(void);
  virtual bool Set(void);
  virtual void Store(const char *PreStr);
  virtual void Create(cOsdMenu *menu);
  };

// mode: 0-Cap 1-Int 2-Hex

cOptMInt::cOptMInt(const char *Name, const char *Title, int *Storage, int Size, int Mode)
:cOpt(Name,Title)
{
  storage=Storage; size=Size; mode=Mode; len=sizeof(int)*size;
  value=MALLOC(int,size);
}

cOptMInt::~cOptMInt()
{
  free(value);
}

void cOptMInt::Parse(const char *Value)
{
  memset(storage,0,len);
  int i=0;
  while(1) {
    char *p;
    const int c=strtol(Value,&p,mode>1 ? 16:10);
    if(p==Value || i>=size) return;
    if(c>0) storage[i++]=c;
    Value=p;
    }
}

void cOptMInt::Backup(void)
{
  memcpy(value,storage,len);
}

bool cOptMInt::Set(void)
{
  if(memcmp(value,storage,len)) {
    memset(storage,0,len);
    for(int i=0, k=0; i<size; i++) if(value[i]>0) storage[k++]=value[i];
    return true;
    }
  return false;
}

void cOptMInt::Store(const char *PreStr)
{
  char b[256];
  int p=0;
  for(int i=0; i<size; i++)
    if(storage[i] || mode==0) p+=snprintf(b+p,sizeof(b)-p,mode>1 ? "%x ":"%d ",storage[i]);
  ScPlugin->SetupStore(FullName(PreStr),p>0?b:0);
}

void cOptMInt::Create(cOsdMenu *menu)
{
  for(int i=0; i<size; i++) {
    const char *buff=tr(title);
    switch(mode) {
      case 0: menu->Add(new cMenuEditCapItem(buff,&value[i])); break;
      case 1: menu->Add(new cMenuEditIntItem(buff,&value[i],0,65535)); break;
      case 2: menu->Add(new cMenuEditHexItem(buff,&value[i],0,65535)); break;
      }
    if(value[i]==0) break;
    }
}

// --- cOpts -------------------------------------------------------------------

cOpts::cOpts(const char *PreStr, int NumOpts)
{
  preStr=PreStr;
  numOpts=NumOpts; numAdd=0;
  if((opts=MALLOC(cOpt *,numOpts))) memset(opts,0,sizeof(cOpt *)*numOpts);
}

cOpts::~cOpts()
{
  if(opts) {
    for(int i=0; i<numOpts; i++) delete opts[i];
    free(opts);
    }
}

void cOpts::Add(cOpt *opt)
{
  if(opts && numAdd<numOpts) opts[numAdd++]=opt;
}

bool cOpts::Parse(const char *Name, const char *Value)
{
  if(opts) {
    for(int i=0; i<numAdd; i++)
      if(opts[i] && opts[i]->Persistant() && !strcasecmp(Name,opts[i]->Name())) {
        opts[i]->Parse(Value);
        return true;
        }
    }
  return false;
}

bool cOpts::Store(bool AsIs)
{
  bool res=false;
  if(opts) {
    for(int i=0; i<numAdd; i++)
      if(opts[i]) {
        if(!AsIs && opts[i]->Set()) res=true;
        if(opts[i]->Persistant()) opts[i]->Store(preStr);
        }
    }
  return res;
}

void cOpts::Backup(void)
{
  if(opts) {
    for(int i=0; i<numAdd; i++)
      if(opts[i]) opts[i]->Backup();
    }
}

void cOpts::Create(cOsdMenu *menu)
{
  if(opts) {
    for(int i=0; i<numAdd; i++)
      if(opts[i]) opts[i]->Create(menu);
    }
}

// --- cSoftCAM ---------------------------------------------------------------

class cSoftCAM {
public:
  static bool Load(const char *cfgdir);
  static void Shutdown(void);
  };

bool cSoftCAM::Load(const char *cfgdir)
{
  if(!Feature.KeyFile()) keys.Disable();
  if(!Feature.SmartCard()) smartcards.Disable();
  cStructLoaders::Load(false);
  if(Feature.KeyFile() && keys.Count()<1)
    PRINTF(L_GEN_ERROR,"no keys loaded for softcam!");
  if(!cSystems::Init(cfgdir)) return false;
  srand(time(0));
  return true;
}

void cSoftCAM::Shutdown(void)
{
  cStructLoaders::Save(true);
  cSystems::Clean();
  smartcards.Shutdown();
  keys.SafeClear();
}

// --- cMenuInfoSc -------------------------------------------------------------

class cMenuInfoSc : public cOsdMenu {
public:
  cMenuInfoSc(void);
  virtual eOSState ProcessKey(eKeys Key);
  };

cMenuInfoSc::cMenuInfoSc(void)
:cOsdMenu(tr("SoftCAM"),25)
{
  Add(new cScInfoItem(tr("Current keys:")));
  int d=0, n;
  do {
    n=0;
    char *ks;
    do {
      const char *id;
      if((ks=cGlobal::CurrKeyStr(d,n,&id))) {
        char buffer[32];
        snprintf(buffer,sizeof(buffer),"  [%s.%d]",id,n);
        Add(new cScInfoItem(buffer,ks));
        free(ks);
        n++;
        }
      } while(ks);
    d++;
    } while(n>0);
  if(Feature.KeyFile()) {
    Add(new cScInfoItem(tr("Key update status:")));
    int fk, nk;
    cSystem::KeyStats(fk,nk);
    // TRANSLATORS: 2 leading spaces!
    Add(new cScInfoItem(tr("  [Seen keys]"),fk));
    // TRANSLATORS: 2 leading spaces!
    Add(new cScInfoItem(tr("  [New keys]"), nk));
    }
  Display();
}

eOSState cMenuInfoSc::ProcessKey(eKeys Key)
{
  eOSState state=cOsdMenu::ProcessKey(Key);
  if(state==osUnknown && Key==kOk) state=osBack;
  return state;
}

// --- cMenuInfoCard -----------------------------------------------------------

class cMenuInfoCard : public cMenuText {
private:
  int port;
  char infoStr[4096];
public:
  cMenuInfoCard(int Port);
  virtual eOSState ProcessKey(eKeys Key);
  };

cMenuInfoCard::cMenuInfoCard(int Port)
:cMenuText(tr("Smartcard"),0,fontFix)
{
  port=Port;
  smartcards.CardInfo(port,infoStr,sizeof(infoStr));
  SetText(infoStr);
  SetHelp(tr("Reset card"));
  Display();
}

eOSState cMenuInfoCard::ProcessKey(eKeys Key)
{
  if(Key==kRed && Interface->Confirm(tr("Really reset card?"))) {
    smartcards.CardReset(port);
    return osEnd;
    }
  eOSState state=cMenuText::ProcessKey(Key);
  if(state==osUnknown) state=osContinue;
  return state;
}

// --- cLogOptItem -------------------------------------------------------------

class cLogOptItem : public cMenuEditBoolItem {
private:
  int o;
public:
  cLogOptItem(const char *Name, int O, int *val);
  int Option(void) { return o; }
  };

cLogOptItem::cLogOptItem(const char *Name, int O, int *val)
:cMenuEditBoolItem(Name,val)
{
  o=O;
}

// --- cMenuLogMod -------------------------------------------------------------

class cMenuLogMod : public cOsdMenu {
private:
  int m;
  int v[LOPT_NUM], cfg[LOPT_NUM];
  //
  void Store(void);
public:
  cMenuLogMod(int M);
  virtual eOSState ProcessKey(eKeys Key);
  };

cMenuLogMod::cMenuLogMod(int M)
:cOsdMenu(tr("Module config"),33)
{
  m=M;
  Add(new cOsdItem(tr("Reset module to default"),osUser9));
  const char *name=cLogging::GetModuleName(LCLASS(m,0));
  int o=cLogging::GetModuleOptions(LCLASS(m,0));
  if(o>=0) {
    for(int i=0; i<LOPT_NUM; i++) {
      const char *opt;
      if(i==0) opt="enable";
      else opt=cLogging::GetOptionName(LCLASS(m,1<<i));
      if(opt) {
        char buff[64];
        snprintf(buff,sizeof(buff),"%s.%s",name,opt);
        cfg[i]=(o&(1<<i)) ? 1:0;
        v[i]=1;
        Add(new cLogOptItem(buff,i,&cfg[i]));
        }
      else v[i]=0;
      }
    }
  Display();
}

void cMenuLogMod::Store(void)
{
  int o=0;
  for(int i=0; i<LOPT_NUM; i++) if(v[i] && cfg[i]) o|=(1<<i);
  cLogging::SetModuleOptions(LCLASS(m,o));
  ScSetup.Store(false);
}

eOSState cMenuLogMod::ProcessKey(eKeys Key)
{
  eOSState state=cOsdMenu::ProcessKey(Key);
  switch(state) {
    case osUser9:
      if(Interface->Confirm(tr("Really reset module to default?"))) {
        cLogging::SetModuleDefault(LCLASS(m,0));
        ScSetup.Store(false); state=osBack;
        }
      break;

    case osContinue:
      if(NORMALKEY(Key)==kLeft || NORMALKEY(Key)==kRight) {
        cLogOptItem *item=dynamic_cast<cLogOptItem *>(Get(Current()));
        if(item) {
          int o=item->Option();
          cLogging::SetModuleOption(LCLASS(m,1<<o),cfg[o]);
          }
        }
      break;

    case osUnknown:
      if(Key==kOk) { Store(); state=osBack; }
      break;

    default:
      break;
    }
  return state;
}

// --- cLogModItem -------------------------------------------------------------

class cLogModItem : public cOsdItem {
private:
  int m;
public:
  cLogModItem(const char *Name, int M);
  int Module(void) { return m; }
  };

cLogModItem::cLogModItem(const char *Name, int M)
:cOsdItem(osUnknown)
{
  m=M;
  char buf[64];
  snprintf(buf,sizeof(buf),"%s '%s'...",tr("Module"),Name);
  SetText(buf,true);
}

// --- cMenuLogSys -------------------------------------------------------------

class cMenuLogSys : public cOsdMenu {
private:
  void Store(void);
public:
  cMenuLogSys(void);
  virtual eOSState ProcessKey(eKeys Key);
  };

cMenuLogSys::cMenuLogSys(void)
:cOsdMenu(tr("Message logging"),33)
{
  LogOpts->Backup(); LogOpts->Create(this);
  Add(new cOsdItem(tr("Disable ALL modules"),osUser9));
  Add(new cOsdItem(tr("Reset ALL modules to default"),osUser8));
  for(int m=1; m<LMOD_MAX; m++) {
    const char *name=cLogging::GetModuleName(LCLASS(m,0));
    if(name)
      Add(new cLogModItem(name,m));
    }
  Display();
}

void cMenuLogSys::Store(void)
{
  char *lf=strdup(logcfg.logFilename);
  ScSetup.Store(false);
  if(!lf || strcmp(lf,logcfg.logFilename))
    cLogging::ReopenLogfile();
  free(lf);
}

eOSState cMenuLogSys::ProcessKey(eKeys Key)
{
  eOSState state=cOsdMenu::ProcessKey(Key);
  switch(state) {
    case osUser9:
      if(Interface->Confirm(tr("Really disable ALL modules?"))) {
        for(int m=1; m<LMOD_MAX; m++)
          cLogging::SetModuleOption(LCLASS(m,LMOD_ENABLE),false);
        Store(); state=osBack;
        }
      break;

    case osUser8:
      if(Interface->Confirm(tr("Really reset ALL modules to default?"))) {
        for(int m=1; m<LMOD_MAX; m++)
          cLogging::SetModuleDefault(LCLASS(m,0));
        Store(); state=osBack;
        }
      break;

    case osUnknown:
      if(Key==kOk) {
        cLogModItem *item=dynamic_cast<cLogModItem *>(Get(Current()));
        if(item) state=AddSubMenu(new cMenuLogMod(item->Module()));
        else { Store(); state=osBack; }
        }
      break;

    default:
      break;
    }
  return state;
}

// --- cMenuSysOpts -------------------------------------------------------------

class cMenuSysOpts : public cOsdMenu {
public:
  cMenuSysOpts(void);
  virtual eOSState ProcessKey(eKeys Key);
  };

cMenuSysOpts::cMenuSysOpts(void)
:cOsdMenu(tr("Cryptsystem options"),33)
{
  for(cOpts *opts=0; (opts=cSystems::GetSystemOpts(opts==0));) {
    opts->Backup();
    opts->Create(this);
    }
  Display();
}

eOSState cMenuSysOpts::ProcessKey(eKeys Key)
{
  eOSState state=cOsdMenu::ProcessKey(Key);
  switch(state) {
    case osContinue:
      if(NORMALKEY(Key)==kUp || NORMALKEY(Key)==kDown) {
        cOsdItem *item=Get(Current());
        if(item) item->ProcessKey(kNone);
        }
      break;

    case osUnknown:
      if(Key==kOk) { ScSetup.Store(false); state=osBack; }
      break;

    default:
      break;
    }
  return state;
}

// --- cMenuSetupSc ------------------------------------------------------------

class cMenuSetupSc : public cMenuSetupPage {
private:
  char *cfgdir;
protected:
  virtual void Store(void);
public:
  cMenuSetupSc(const char *CfgDir);
  virtual ~cMenuSetupSc();
  virtual eOSState ProcessKey(eKeys Key);
  };

cMenuSetupSc::cMenuSetupSc(const char *CfgDir)
{
  cfgdir=strdup(CfgDir);
  SetSection(tr("SoftCAM"));

  ScOpts->Backup(); LogOpts->Backup();
  for(cOpts *opts=0; (opts=cSystems::GetSystemOpts(opts==0));) opts->Backup();

  ScOpts->Create(this);
  Add(new cOsdItem(tr("Cryptsystem options..."),osUser5));
  Add(new cOsdItem(tr("Message logging..."),osUser6));
  if(Feature.SmartCard()) {
    char id[IDSTR_LEN];
    for(int i=0; smartcards.ListCard(i,id,sizeof(id)); i++) {
      char buff[32];
      snprintf(buff,sizeof(buff),"%s %d",tr("Smartcard interface"),i);
      cScInfoItem *ii;
      if(id[0]) ii=new cScInfoItem(buff,id,osUser4);
      else      ii=new cScInfoItem(buff,tr("(empty)"));
      ii->Ident(i);
      Add(ii);
      }
    }
  Add(new cOsdItem(tr("Status information..."),osUser8));
  Add(new cOsdItem(tr("Flush ECM cache"),osUser7));
  Add(new cOsdItem(tr("Reload files"),osUser9));
}

cMenuSetupSc::~cMenuSetupSc()
{
  free(cfgdir);
}

void cMenuSetupSc::Store(void)
{
  ScSetup.Store(false);
}

eOSState cMenuSetupSc::ProcessKey(eKeys Key)
{
  eOSState state = cOsdMenu::ProcessKey(Key);
  switch(state) {
    case osUser4:
      if(Feature.SmartCard()) {
        cScInfoItem *ii=dynamic_cast<cScInfoItem *>(Get(Current()));
        if(ii) return(AddSubMenu(new cMenuInfoCard(ii->Ident())));
        }
      state=osContinue;
      break;

    case osUser7:
      state=osContinue;
      if(Interface->Confirm(tr("Really flush ECM cache?"))) {
        ecmcache.Flush();
        state=osEnd;
        }
      break;

    case osUser8:
      return AddSubMenu(new cMenuInfoSc);

    case osUser6:
      return AddSubMenu(new cMenuLogSys);

    case osUser5:
      return AddSubMenu(new cMenuSysOpts);

    case osUser9:
      state=osContinue;
      if(!cGlobal::Active(true)) {
        if(Interface->Confirm(tr("Really reload files?"))) {
          Store();
          cSoftCAM::Load(cfgdir);
          state=osEnd;
          }
        }
      else 
        Skins.Message(mtError,tr("Active! Can't reload files now"));
      break;

    case osContinue:
      if(NORMALKEY(Key)==kUp || NORMALKEY(Key)==kDown) {
        cOsdItem *item=Get(Current());
        if(item) item->ProcessKey(kNone);
        }
      break;

    case osUnknown:
      if(Key==kOk) { Store(); state=osBack; }
      break;

    default:
      break;
    }
  return state;
}

// --- cScSetup ---------------------------------------------------------------

cScSetup ScSetup;

cScSetup::cScSetup(void)
{
  AutoUpdate=1;
  memset(ScCaps,0,sizeof(ScCaps));
  ScCaps[0]=1;
  ScCaps[1]=2;
  ConcurrentFF=0;
  LocalPriority=0;
  ForceTransfer=1;
  PrestartAU=0;
  SuperKeys=0;
  EcmCache=0;
  DeCsaTsBuffSize=4;
}

void cScSetup::Check(void)
{
  if(AutoUpdate==0)
    PRINTF(L_GEN_WARN,"Keys updates (AU) are disabled.");
  for(int i=0; i<MAXSCCAPS; i++)
    if(ScCaps[i]>=16) {
      PRINTF(L_GEN_WARN,"ScCaps contains unusual value. Check your config! (You can ignore this message if you have more than 16 dvb cards in your system ;)");
      break;
      }

  PRINTF(L_CORE_LOAD,"** Plugin config:");
  PRINTF(L_CORE_LOAD,"** Key updates (AU) are %s (%sprestart)",AutoUpdate?(AutoUpdate==1?"enabled (active CAIDs)":"enabled (all CAIDs)"):"DISABLED",PrestartAU?"":"no ");
  PRINTF(L_CORE_LOAD,"** Local systems %stake priority over cached remote",LocalPriority?"":"DON'T ");
  PRINTF(L_CORE_LOAD,"** Concurrent FF recordings are %sallowed",ConcurrentFF?"":"NOT ");
  PRINTF(L_CORE_LOAD,"** %sorce transfermode with digital audio",ForceTransfer?"F":"DON'T f");
  PRINTF(L_CORE_LOAD,"** ECM cache is set to %s",EcmCache ? (EcmCache==1?"READ-ONLY":"DISABLED"):"enabled");
  PRINTF(L_CORE_LOAD,"** TsBufferSize is %d MB",DeCsaTsBuffSize);
  LBSTART(L_CORE_LOAD);
  LBPUT("** ScCaps are"); for(int i=0; i<MAXSCCAPS ; i++) LBPUT(" %d",ScCaps[i]);
  LBEND();
}

void cScSetup::Store(bool AsIs)
{
  if(ScOpts) ScOpts->Store(AsIs);
  cSystems::ConfigStore(AsIs);
  if(LogOpts) LogOpts->Store(AsIs);
  cLineBuff lb(128);
  if(cLogging::GetConfig(&lb))
    ScPlugin->SetupStore("LogConfig",lb.Line());
}

bool cScSetup::CapCheck(int n)
{
  for(int j=0; j<MAXSCCAPS; j++)
    if(ScCaps[j] && ScCaps[j]==n+1) return true;
  return false;
}

// --- cScHousekeeper ----------------------------------------------------------

class cScHousekeeper : public cThread {
protected:
  virtual void Action(void);
public:
  cScHousekeeper(void);
  ~cScHousekeeper();
  };

cScHousekeeper::cScHousekeeper(void)
:cThread("SC housekeeper")
{
  Start();
}

cScHousekeeper::~cScHousekeeper()
{
  Cancel(3);
}

void cScHousekeeper::Action(void)
{
  int c=0;
  while(Running()) {
    if(++c==20) {
      c=0;
      cGlobal::HouseKeeping();
      }
    if(Feature.KeyFile()) keys.HouseKeeping();
    if(!cGlobal::Active(false)) cStructLoaders::Purge();
    cStructLoaders::Load(true);
    cStructLoaders::Save();
    cCondWait::SleepMs(987);
    }
}

#ifndef STATICBUILD

// --- cScDll ------------------------------------------------------------------

class cScDll : public cSimpleItem {
private:
  char *fileName;
  void *handle;
public:
  cScDll(const char *FileName);
  ~cScDll();
  bool Load(bool check);
  };

cScDll::cScDll(const char *FileName)
{
  fileName=strdup(FileName);
  handle=0;
}

cScDll::~cScDll()
{
  if(handle) dlclose(handle);
  free(fileName);
}

bool cScDll::Load(bool check)
{
  char *base=rindex(fileName,'/');
  if(base) base++; else base=fileName;
  PRINTF(L_CORE_DYN,"loading library: %s",base);
  if(!handle) {
    handle=dlopen(fileName,RTLD_NOW|(check?RTLD_LOCAL:RTLD_GLOBAL));
    if(handle) {
      if(!check) return true;
      dlerror();
      int *libapivers=(int *)dlsym(handle,"ScLibApiVersion");
      const char *error=dlerror();
      if(!error && *libapivers==SCAPIVERS) return true;
      else PRINTF(L_GEN_ERROR,"dload: %s: SCAPI version doesn't match (plugin=%d, library=%d)",base,SCAPIVERS,!error?*libapivers:0);
      }
    else PRINTF(L_GEN_ERROR,"dload: %s: %s",base,dlerror());
    }
  return false;
}

// --- cScDlls -----------------------------------------------------------------

#define LIBVDR_PREFIX "libvdr-"
#define LIBSC_PREFIX  "libsc-"
#define SO_INDICATOR   ".so."

class cScDlls : public cSimpleList<cScDll> {
private:
  void *handle;
public:
  cScDlls(void);
  ~cScDlls();
  bool Load(void);
  };

cScDlls::cScDlls(void)
{
  handle=0;
}

cScDlls::~cScDlls()
{
  Clear();
  if(handle) dlclose(handle);
  PRINTF(L_CORE_DYN,"unload done");
}

bool cScDlls::Load(void)
{
  Dl_info info;
  static int marker=0;
  if(!dladdr((void *)&marker,&info)) {
    PRINTF(L_GEN_ERROR,"dladdr: %s",dlerror());
    return false;
    }

  // we have to re-dlopen our selfs as VDR doesn't use RTLD_GLOBAL
  // but our symbols have to be available to the sub libs.
  handle=dlopen(info.dli_fname,RTLD_NOW|RTLD_GLOBAL);
  if(!handle) {
    PRINTF(L_GEN_ERROR,"dlopen myself: %s",dlerror());
    return false;
    }

  char *path=strdup(info.dli_fname);
  char *p;
  if((p=rindex(path,'/'))) *p=0;
  PRINTF(L_CORE_DYN,"library path %sn",path);

  char pat[64];
#if APIVERSNUM >= 10711
#ifdef WITH_SDDVB
  {
  snprintf(pat,sizeof(pat),"%s%s%s%s",LIBVDR_PREFIX,"dvbsddevice",SO_INDICATOR,APIVERSION);
  cScDll *dll=new cScDll(AddDirectory(path,pat));
  if(dll) {
    if(dll->Load(false)) {
      PRINTF(L_GEN_INFO,"VDR dvbsddevice plugin loaded as subplugin");
      }
    Ins(dll);
    }
  }
#endif //WITH_SDDVB
#ifdef WITH_HDDVB
  {
  snprintf(pat,sizeof(pat),"%s%s%s%s",LIBVDR_PREFIX,"dvbhddevice",SO_INDICATOR,APIVERSION);
  cScDll *dll=new cScDll(AddDirectory(path,pat));
  if(dll) {
    if(dll->Load(false)) {
      PRINTF(L_GEN_INFO,"VDR dvbhddevice plugin loaded as subplugin");
      }
    Ins(dll);
    }
  }
#endif //WITH_HDDVB
#endif //APIVERSNUM >= 10711

  snprintf(pat,sizeof(pat),"%s*-%d%s%s",LIBSC_PREFIX,SCAPIVERS,SO_INDICATOR,APIVERSION);
  bool res=true;
  cReadDir dir(path);
  struct dirent *e;
  while((e=dir.Next())) {
    if(!fnmatch(pat,e->d_name,FNM_PATHNAME|FNM_NOESCAPE)) {
      cScDll *dll=new cScDll(AddDirectory(path,e->d_name));
      if(dll) {
        if(!dll->Load(true)) res=false;
        Ins(dll);
        }
      }
    }
  free(path);
  return res;
}

#endif //STATICBUILD

// --- cScPlugin ---------------------------------------------------------------

class cScPlugin : public cPlugin {
private:
#ifndef STATICBUILD
  cScDlls dlls;
#endif
  cScHousekeeper *keeper;
  bool dllSuccess;
public:
  cScPlugin(void);
  virtual ~cScPlugin();
  virtual const char *Version(void);
  virtual const char *Description(void);
  virtual const char *CommandLineHelp(void);
  virtual bool ProcessArgs(int argc, char *argv[]);
  virtual bool Initialize(void);
  virtual bool Start(void);
  virtual void Stop(void);
#ifndef SASC
  virtual void MainThreadHook(void);
#endif
  virtual cMenuSetupPage *SetupMenu(void);
  virtual bool SetupParse(const char *Name, const char *Value);
  virtual const char **SVDRPHelpPages(void);
  virtual cString SVDRPCommand(const char *Command, const char *Option, int &ReplyCode);
  };

cScPlugin::cScPlugin(void)
{
  static const char *logg[] = { trNOOP("off"),trNOOP("active CAIDs"),trNOOP("all CAIDs") };
  static const char *skey[] = { trNOOP("comment out"),trNOOP("remove") };
  static const char *ecache[] = { trNOOP("enabled"),trNOOP("read-only"),trNOOP("off") };
  ScOpts=new cOpts(0,10);
  ScOpts->Add(new cOptSel  ("AutoUpdate"   ,trNOOP("Update keys (AU)")     ,&ScSetup.AutoUpdate,3,logg));
  ScOpts->Add(new cOptBool ("PrestartAU"   ,trNOOP("Start AU on EPG scan") ,&ScSetup.PrestartAU));
  ScOpts->Add(new cOptSel  ("SuperKeys"    ,trNOOP("Superseded keys")      ,&ScSetup.SuperKeys,2,skey));
  ScOpts->Add(new cOptBool ("ConcurrentFF" ,trNOOP("Concurrent FF streams"),&ScSetup.ConcurrentFF));
  ScOpts->Add(new cOptBool ("ForceTranfer" ,trNOOP("Force TransferMode")   ,&ScSetup.ForceTransfer));
  ScOpts->Add(new cOptBool ("LocalPriority",trNOOP("Prefer local systems") ,&ScSetup.LocalPriority));
  ScOpts->Add(new cOptSel  ("EcmCache"     ,trNOOP("ECM cache")            ,&ScSetup.EcmCache,3,ecache));
  ScOpts->Add(new cOptInt  ("DeCsaTsBuffSize",trNOOP("TS buffer size MB")  ,&ScSetup.DeCsaTsBuffSize,4,15));
  ScOpts->Add(new cOptMInt ("ScCaps"       ,trNOOP("Active on DVB card")   , ScSetup.ScCaps,MAXSCCAPS,0));
  LogOpts=new cOpts(0,6);
  LogOpts->Add(new cOptBool ("LogConsole"  ,trNOOP("Log to console")      ,&logcfg.logCon));
  LogOpts->Add(new cOptBool ("LogFile"     ,trNOOP("Log to file")         ,&logcfg.logFile));
  LogOpts->Add(new cOptStr  ("LogFileName" ,trNOOP("Filename")            ,logcfg.logFilename,sizeof(logcfg.logFilename),FileNameChars));
  LogOpts->Add(new cOptInt  ("LogFileLimit",trNOOP("Filesize limit (KB)") ,&logcfg.maxFilesize,0,2000000));
  LogOpts->Add(new cOptBool ("LogSyslog"   ,trNOOP("Log to syslog")       ,&logcfg.logSys));
  LogOpts->Add(new cOptBool ("LogUserMsg"  ,trNOOP("Show user messages")  ,&logcfg.logUser));
#ifndef STATICBUILD
  dllSuccess=dlls.Load();
#else
  dllSuccess=true;
#endif
  if(dllSuccess) cScDevices::OnPluginLoad();
  keeper=0;
}

cScPlugin::~cScPlugin()
{
  delete keeper;
  delete ScOpts;
  delete LogOpts;
  cScDevices::OnPluginUnload();
}

bool cScPlugin::Initialize(void)
{
  PRINTF(L_GEN_INFO,"SC version %s initializing (VDR %s)",ScVersion,VDRVERSION);
  return dllSuccess && cScDevices::Initialize();
}

bool cScPlugin::Start(void)
{
  PRINTF(L_GEN_INFO,"SC version %s starting (VDR %s)",ScVersion,VDRVERSION);
  if(APIVERSNUM<MINAPIVERSNUM) {
    PRINTF(L_GEN_ERROR,"SC plugin needs at least VDR API version %d.%d.%d",MIN_VERS,MIN_MAJOR,MIN_MINOR);
    return false;
    }
  if(sizeof(int)!=4) {
    PRINTF(L_GEN_ERROR,"compiled with 'int' as %d bit. Only supporting 32 bit.",(int)sizeof(int)*8);
    return false;
    }
  if(sizeof(long long)!=8) {
    PRINTF(L_GEN_ERROR,"compiled with 'long long' as %d bit. Only supporting 64 bit.",(int)sizeof(long long)*8);
    return false;
    }
    
  ScPlugin=this;
  const char *cfgdir=ConfigDirectory(cfgsub);
  filemaps.SetCfgDir(cfgdir);
  cStructLoaders::SetCfgDir(cfgdir);
  ScSetup.Check();
  if(!cSoftCAM::Load(cfgdir)) return false;
  cScDevices::Startup();
  keeper=new cScHousekeeper;
  return true;
}

void cScPlugin::Stop(void)
{
  delete keeper; keeper=0;
  cScDevices::Shutdown();
  LogStatsDown();
  cSoftCAM::Shutdown();
  PRINTF(L_GEN_DEBUG,"SC cleanup done");
}

const char *cScPlugin::Version(void)
{
  return ScVersion;
}

const char *cScPlugin::Description(void)
{
  return tr("A software emulated CAM");
}

const char *cScPlugin::CommandLineHelp(void)
{
  static char *help_str=0;
  
  free(help_str);    //                                     for easier orientation, this is column 80|
  help_str=bprintf(  "  -B N    --budget=N        forces DVB device N to budget mode (using FFdecsa)\n"
                     "  -E CMD  --external-au=CMD script for external key updates\n"
                     );
  return help_str;
}

bool cScPlugin::ProcessArgs(int argc, char *argv[])
{
  static struct option long_options[] = {
      { "external-au", required_argument, NULL, 'E' },
      { "budget",      required_argument, NULL, 'B' },
      { NULL }
    };

  int c, option_index=0;
  while((c=getopt_long(argc,argv,"B:E:",long_options,&option_index))!=-1) {
    switch (c) {
      case 'E': externalAU=optarg; break;
      case 'B': cScDevices::SetForceBudget(atoi(optarg)); break;
      default:  return false;
      }
    }
  return true;
}

cMenuSetupPage *cScPlugin::SetupMenu(void)
{
  return new cMenuSetupSc(ConfigDirectory(cfgsub));
}

bool cScPlugin::SetupParse(const char *Name, const char *Value)
{
  if((ScOpts && ScOpts->Parse(Name,Value)) ||
     (LogOpts && LogOpts->Parse(Name,Value)) ||
     cSystems::ConfigParse(Name,Value)) ;
  else if(!strcasecmp(Name,"LogConfig")) cLogging::ParseConfig(Value);
  else return false;
  return true;
}

#ifndef SASC

void cScPlugin::MainThreadHook(void)
{
  int n=0;
  cUserMsg *um;
  while(++n<=10 && (um=ums.GetQueuedMsg())) {
    Skins.QueueMessage(mtInfo,um->Message());
    delete um;
    }
}

#endif //SASC

const char **cScPlugin::SVDRPHelpPages(void)
{
  static const char *HelpPages[] = {
    "RELOAD\n"
    "    Reload all configuration files.",
    "KEY <string>\n",
    "    Add key to the key database (as if it was received from EMM stream).",
    "LOG <on|off> <class>[,<class>...]\n"
    "    Turn the given message class(es) on or off.",
    "LOGCFG\n"
    "    Display available message classes and their status.",
    "LOGFILE <on|off> [<filename>]\n"
    "    Enables/disables logging to file and optionaly sets the filename.",
    NULL
    };
  return HelpPages;
}

cString cScPlugin::SVDRPCommand(const char *Command, const char *Option, int &ReplyCode)
{
  if(!strcasecmp(Command,"RELOAD")) {
    if(cGlobal::Active(true)) {
      ReplyCode=550;
      return "Softcam active. Can't reload files now";
      }
    else {
      if(cSoftCAM::Load(ConfigDirectory(cfgsub)))
        return "Files reloaded successfully";
      else {
        ReplyCode=901;
        return "Reloading files not entirely successfull";
        }
      }
    }
  else if(!strcasecmp(Command,"KEY")) {
    if(Option && *Option) {
      if(keys.NewKeyParse(skipspace(Option),"from SVDR"))
        return "Key update successfull";
      else {
        ReplyCode=901;
        return "Key already known or invalid key format";
        }
      }
    else { ReplyCode=501; return "Missing args"; }
    }
  else if(!strcasecmp(Command,"LOG")) {
    if(Option && *Option) {
      char tmp[1024];
      strn0cpy(tmp,Option,sizeof(tmp));
      char *opt=tmp;
      opt=skipspace(opt);
      bool mode;
      if(!strncasecmp(opt,"ON ",3)) { mode=true; opt+=3; }
      else if(!strncasecmp(opt,"OFF ",4)) { mode=false; opt+=4; }
      else { ReplyCode=501; return "Bad mode, valid: on off"; }
      do {
        char *s=index(opt,',');
        if(s) *s++=0;
        int c=cLogging::GetClassByName(opt);
        if(c>=0) cLogging::SetModuleOption(c,mode);
        else { ReplyCode=501; return "Unknown message class"; }
        opt=s;
        } while(opt);
      ScSetup.Store(true);
      Setup.Save();
      return "Done";
      }
    else { ReplyCode=501; return "Missing args"; }
    }
  else if(!strcasecmp(Command,"LOGCFG")) {
    cLineBuff lb(256);
    for(int m=1; m<LMOD_MAX; m++) {
      const char *name=cLogging::GetModuleName(LCLASS(m,0));
      if(name) {
        int o=cLogging::GetModuleOptions(LCLASS(m,0));
        if(o>=0) {
          for(int i=0; i<LOPT_NUM; i++) {
            const char *opt;
            if(i==0) opt="enable";
            else opt=cLogging::GetOptionName(LCLASS(m,1<<i));
            if(opt)
              lb.Printf("%s.%s %s\n",name,opt,(o&(1<<i))?"on":"off");
            }
          }
        }
      }
    if(lb.Length()>0) return lb.Line();
    ReplyCode=901; return "No config available";
    }
  else if(!strcasecmp(Command,"LOGFILE")){
    if(Option && *Option) {
      char tmp[1024];
      strn0cpy(tmp,Option,sizeof(tmp));
      char *opt=tmp;
      opt=skipspace(opt);
      bool mode;
      if(!strncasecmp(opt,"ON",2)) { mode=true; opt+=2; }
      else if(!strncasecmp(opt,"OFF",3)) { mode=false; opt+=3; }
      else { ReplyCode=501; return "Bad mode, valid: on off"; }
      cLineBuff lb(256);
      if(mode) {
        logcfg.logFile=true;
        if(*opt==' ' || *opt=='\t') {
          opt=stripspace(skipspace(opt));
          strn0cpy(logcfg.logFilename,opt,sizeof(logcfg.logFilename));
          }
        lb.Printf("logging to file enabled, file %s",logcfg.logFilename);
        }
      else {
        logcfg.logFile=false;
        lb.Printf("logging to file disabled");
        }
      ScSetup.Store(true);
      Setup.Save();
      return lb.Line();
      }
    else { ReplyCode=501; return "Missing args"; }
    }
  return NULL;
}

VDRPLUGINCREATOR(cScPlugin); // Don't touch this!
