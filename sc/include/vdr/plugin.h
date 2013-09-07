/*
 * plugin.h: The VDR plugin interface
 *
 * See the main source file 'vdr.c' for copyright information and
 * how to reach the author.
 *
 * $Id: plugin.h 1.14 2007/02/24 13:45:28 kls Exp $
 */

#ifndef __PLUGIN_H
#define __PLUGIN_H

#include "i18n.h"
#include "menuitems.h"
#include "osdbase.h"
#include "tools.h"

#define VDRPLUGINCREATOR(PluginClass) extern "C" void *VDRPluginCreator(void) { return new PluginClass; }

class cPlugin {
  friend class cDll;
  friend class cPluginManager;
private:
  static char *configDirectory;
  const char *name;
  bool started;
  void SetName(const char *s);
public:
  cPlugin(void);
  virtual ~cPlugin();

  const char *Name(void) { return name; }
  virtual const char *Version(void) = 0;
  virtual const char *Description(void) = 0;
  virtual const char *CommandLineHelp(void);

  virtual bool ProcessArgs(int argc, char *argv[]);
  virtual bool Initialize(void);
  virtual bool Start(void);
  virtual void Stop(void);
  virtual void Housekeeping(void);
  virtual void MainThreadHook(void);
  virtual cString Active(void);
  virtual time_t WakeupTime(void);

  virtual const char *MainMenuEntry(void);
  virtual cOsdObject *MainMenuAction(void);

  virtual cMenuSetupPage *SetupMenu(void);
  virtual bool SetupParse(const char *Name, const char *Value);
  void SetupStore(const char *Name, const char *Value = NULL);
  void SetupStore(const char *Name, int Value);

  void RegisterI18n(const tI18nPhrase * const Phrases);

  virtual bool Service(const char *Id, void *Data = NULL);
  virtual const char **SVDRPHelpPages(void);
  virtual cString SVDRPCommand(const char *Command, const char *Option, int &ReplyCode);

  static void SetConfigDirectory(const char *Dir);
  static const char *ConfigDirectory(const char *PluginName = NULL);
  };

class cDll : public cListObject {
private:
  char *fileName;
  char *args;
  void *handle;
  cPlugin *plugin;
public:
  cDll(const char *FileName, const char *Args);
  virtual ~cDll();
  bool Load(bool Log = false);
  cPlugin *Plugin(void) { return plugin; }
  };

class cDlls : public cList<cDll> {};

class cPluginManager {
private:
  static cPluginManager *pluginManager;
  char *directory;
  time_t lastHousekeeping;
  int nextHousekeeping;
  cDlls dlls;
public:
  cPluginManager(const char *Directory);
  virtual ~cPluginManager();
  void SetDirectory(const char *Directory);
  void AddPlugin(const char *Args);
  bool LoadPlugins(bool Log = false);
  bool InitializePlugins(void);
  bool StartPlugins(void);
  void Housekeeping(void);
  void MainThreadHook(void);
  static bool Active(const char *Prompt = NULL);
  static cPlugin *GetNextWakeupPlugin(void);
  static bool HasPlugins(void);
  static cPlugin *GetPlugin(int Index);
  static cPlugin *GetPlugin(const char *Name);
  static cPlugin *CallFirstService(const char *Id, void *Data = NULL);
  static bool CallAllServices(const char *Id, void *Data = NULL);
  void StopPlugins(void);
  void Shutdown(bool Log = false);
  };

#endif //__PLUGIN_H
