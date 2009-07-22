#include "include/vdr/plugin.h"
#include "include/vdr/interface.h"
#include "include/vdr/status.h"
#include "include/vdr/skins.h"
#include "include/vdr/channels.h"
//#include "include/vdr/dvbci.h"
#include "include/vdr/menu.h"
#include "include/vdr/config.h"
#include <ctype.h>

cSetup Setup;

const char *I18nTranslate(const char *s, const char *Plugin) {
  return s;
}

cSetup::cSetup(void) {}
void cSetup::Store(const char *Name, const char *Value, const char *Plugin, bool AllowMultiple) {}
bool cSetup::Save(void) { return true;}

cPlugin::cPlugin() {}
cPlugin::~cPlugin() {}
const char* cPlugin::CommandLineHelp() {return NULL;}
bool cPlugin::ProcessArgs(int count, char**args) {return true;}
bool cPlugin::Initialize(void) {return true;}
bool cPlugin::Start(void) {return true;}
void cPlugin::Stop(void) {}
void cPlugin::Housekeeping(void) {}
void cPlugin::MainThreadHook(void) {}
cString cPlugin::Active(void) {return NULL;}
time_t cPlugin::WakeupTime(void) {return 0;}
const char *cPlugin::MainMenuEntry(void) {return NULL;}
cOsdObject *cPlugin::MainMenuAction(void) {return NULL;}
cMenuSetupPage *cPlugin::SetupMenu(void) {return NULL;}
bool cPlugin::SetupParse(const char *Name, const char *Value) {return true;}
void cPlugin::SetupStore(const char *Name, const char *Value) {}
void cPlugin::SetupStore(const char *Name, int Value) {}
void cPlugin::RegisterI18n(const tI18nPhrase * const Phrases){}
bool cPlugin::Service(const char *Id, void *Data) {return true;}
const char **cPlugin::SVDRPHelpPages(void) {return NULL;}
cString cPlugin::SVDRPCommand(const char *Command, const char *Option, int &ReplyCode) {return NULL;}
void cPlugin::SetConfigDirectory(const char *Dir) {}
const char cPlugin::*ConfigDirectory(const char *PluginName) {return NULL;}

bool cInterface::Confirm(char const*, int, bool)  {return true;}

//cDvbCiAdapter *cDvbCiAdapter::CreateCiAdapter(cDevice *Device, int Fd) {return NULL;}

cSkins::cSkins() {}
cSkins::~cSkins() {}
eKeys cSkins::Message(eMessageType, char const*, int)  { return kNone;}
void cSkins::Clear(void) {}

cChannels::cChannels() {
}

void cStatus::MsgOsdCurrentItem(char const* c) {}

/*
cCaDefinitions CaDefinitions;

const cCaDefinition *cCaDefinitions::Get(int Number)
{
  cCaDefinition *p = First();
  while (p) {
        if (p->Number() == Number)
           return p;
        p = (cCaDefinition *)p->Next();
        }
  return NULL;
}
*/
cSkins Skins;
cChannels Channels;
cInterface *Interface;

//const cCaDefinition *caDefinitions::Get(int Number) {return NULL;}

cString cSource::ToString(int Code)
{
  char buffer[16];
  char *q = buffer;
  switch (Code & st_Mask) {
    case stCable: *q++ = 'C'; break;
    case stSat:   *q++ = 'S';
                  {
                    int pos = Code & ~st_Mask;
                    q += snprintf(q, sizeof(buffer) - 2, "%u.%u", (pos & ~st_Neg) / 10, (pos & ~st_Neg) % 10); // can't simply use "%g" here since the silly 'locale' messes up the decimal point
                    *q++ = (Code & st_Neg) ? 'E' : 'W';
                  }
                  break;
    case stTerr:  *q++ = 'T'; break;
    default:      *q++ = Code + '0'; // backward compatibility
    }
  *q = 0;
  return buffer;
}

int cSource::FromString(const char *s)
{
  int type = stNone;
  switch (toupper(*s)) {
    case 'C': type = stCable; break;
    case 'S': type = stSat;   break;
    case 'T': type = stTerr;  break;
    case '0' ... '9': type = *s - '0'; break; // backward compatibility
    default: esyslog("ERROR: unknown source key '%c'", *s);
             return stNone;
    }
  int code = type;
  if (type == stSat) {
     int pos = 0;
     bool dot = false;
     bool neg = false;
     while (*++s) {
           switch (toupper(*s)) {
             case '0' ... '9': pos *= 10;
                               pos += *s - '0';
                               break;
             case '.':         dot = true;
                               break;
             case 'E':         neg = true; // fall through to 'W'
             case 'W':         if (!dot)
                                  pos *= 10;
                               break;
             default: esyslog("ERROR: unknown source character '%c'", *s);
                      return stNone;
             }
           }
     if (neg)
        pos |= st_Neg;
     code |= pos;
     }
  return code;
}


cMenuText::cMenuText(const char *Title, const char *Text, eDvbFont Font)
:cOsdMenu(Title)
{
}

cMenuText::~cMenuText()
{
}

void cMenuText::SetText(char const *Text)
{
}

void cMenuText::Display(void)
{
}

eOSState cMenuText::ProcessKey(eKeys Key)
{
  return osContinue;
}
