/*
 * osdbase.c: Basic interface to the On Screen Display
 *
 * See the main source file 'vdr.c' for copyright information and
 * how to reach the author.
 *
 * $Id: osdbase.c 1.14 2004/07/17 13:29:13 kls Exp $
 */

#include "include/vdr/osdbase.h"
#include <string.h>
#include "include/vdr/device.h"
#include "include/vdr/i18n.h"
#include "include/vdr/remote.h"
#include "include/vdr/status.h"

// --- cOsdItem --------------------------------------------------------------

cOsdItem::cOsdItem(eOSState State)
{
  text = NULL;
  state = State;
  selectable = true;
  fresh = true;
}

cOsdItem::cOsdItem(const char *Text, eOSState State, bool Selectable)
{
  text = NULL;
  state = State;
  selectable = Selectable;
  fresh = true;
  SetText(Text);
}

cOsdItem::~cOsdItem()
{
  free(text);
}

void cOsdItem::SetText(const char *Text, bool Copy)
{
  free(text);
  text = Copy ? strdup(Text) : (char *)Text; // text assumes ownership!
}

void cOsdItem::SetSelectable(bool Selectable)
{
  selectable = Selectable;
}

void cOsdItem::SetFresh(bool Fresh)
{
  fresh = Fresh;
}

eOSState cOsdItem::ProcessKey(eKeys Key)
{
  return Key == kOk ? state : osUnknown;
}

void cOsdObject::Show(void)
{
}
// --- cOsdMenu --------------------------------------------------------------

//cSkinDisplayMenu *cOsdMenu::displayMenu = NULL;
//int cOsdMenu::displayMenuCount = 0;
//int cOsdMenu::displayMenuItems = 0;//XXX dynamic???

cOsdMenu::cOsdMenu(const char *Title, int c0, int c1, int c2, int c3, int c4)
{
}

cOsdMenu::~cOsdMenu()
{
}

const char *cOsdMenu::hk(const char *s)
{
  return NULL;
}

void cOsdMenu::SetStatus(const char *s)
{
}

void cOsdMenu::SetTitle(const char *Title)
{
}

void cOsdMenu::SetHelp(const char *Red, const char *Green, const char *Yellow, const char *Blue)
{
}

void cOsdMenu::Del(int Index)
{
}

void cOsdMenu::Add(cOsdItem *Item, bool Current, cOsdItem *After)
{
}

void cOsdMenu::Ins(cOsdItem *Item, bool Current, cOsdItem *Before)
{
}

void cOsdMenu::Display(void)
{
}

void cOsdMenu::SetCurrent(cOsdItem *Item)
{
}

void cOsdMenu::RefreshCurrent(void)
{
}

void cOsdMenu::DisplayCurrent(bool Current)
{
}

void cOsdMenu::Clear(void)
{
}

bool cOsdMenu::SelectableItem(int idx)
{
  return true;
}

void cOsdMenu::CursorUp(void)
{
}

void cOsdMenu::CursorDown(void)
{
}

void cOsdMenu::PageUp(void)
{
}

void cOsdMenu::PageDown(void) 
{
}

void cOsdMenu::Mark(void)
{
}

eOSState cOsdMenu::HotKey(eKeys Key)
{
  return osUnknown;
}

eOSState cOsdMenu::AddSubMenu(cOsdMenu *SubMenu)
{
  return osUnknown;
}

eOSState cOsdMenu::CloseSubMenu()
{
  return osUnknown;
}

eOSState cOsdMenu::ProcessKey(eKeys Key)
{
  return osUnknown;
}

