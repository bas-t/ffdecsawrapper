/*
 * menuitems.c: General purpose menu items
 *
 * See the main source file 'vdr.c' for copyright information and
 * how to reach the author.
 *
 * $Id: menuitems.c 1.19 2004/06/19 09:45:45 kls Exp $
 */

#include "include/vdr/menuitems.h"
#include <ctype.h>
#include "include/vdr/plugin.h"
#include "include/vdr/remote.h"
#include "include/vdr/skins.h"
#include "include/vdr/status.h"

const char *FileNameChars = " abcdefghijklmnopqrstuvwxyz0123456789-.#~";

// --- cMenuEditItem ---------------------------------------------------------

cMenuEditItem::cMenuEditItem(const char *Name)
{
  name = strdup(Name ? Name : "???");
}

cMenuEditItem::~cMenuEditItem()
{
  free(name);
}

void cMenuEditItem::SetValue(const char *Value)
{
  char *buffer = NULL;
  asprintf(&buffer, "%s:\t%s", name, Value);
  SetText(buffer, false);
}

// --- cMenuEditIntItem ------------------------------------------------------

cMenuEditIntItem::cMenuEditIntItem(const char *Name, int *Value, int Min, int Max, const char *MinString, const char *MaxString)
:cMenuEditItem(Name)
{
  value = Value;
  min = Min;
  max = Max;
  minString = MinString;
  maxString = MaxString;
  if (*value < min)
     *value = min;
  else if (*value > max)
     *value = max;
  Set();
}

void cMenuEditIntItem::Set(void)
{
  if (minString && *value == min)
     SetValue(minString);
  else if (maxString && *value == max)
     SetValue(maxString);
  else {
     char buf[16];
     snprintf(buf, sizeof(buf), "%d", *value);
     SetValue(buf);
     }
}

eOSState cMenuEditIntItem::ProcessKey(eKeys Key)
{
  eOSState state = cMenuEditItem::ProcessKey(Key);

  if (state == osUnknown) {
     int newValue = *value;
     Key = NORMALKEY(Key);
     switch (Key) {
       case kNone: break;
       case k0 ... k9:
            if (fresh) {
               *value = 0;
               fresh = false;
               }
            newValue = *value * 10 + (Key - k0);
            break;
       case kLeft: // TODO might want to increase the delta if repeated quickly?
            newValue = *value - 1;
            fresh = true;
            break;
       case kRight:
            newValue = *value + 1;
            fresh = true;
            break;
       default:
            if (*value < min) { *value = min; Set(); }
            if (*value > max) { *value = max; Set(); }
            return state;
       }
     if ((!fresh || min <= newValue) && newValue <= max) {
        *value = newValue;
        Set();
        }
     state = osContinue;
     }
  return state;
}

// --- cMenuEditBoolItem -----------------------------------------------------

cMenuEditBoolItem::cMenuEditBoolItem(const char *Name, int *Value, const char *FalseString, const char *TrueString)
:cMenuEditIntItem(Name, Value, 0, 1)
{
  falseString = FalseString ? FalseString : tr("no");
  trueString = TrueString ? TrueString : tr("yes");
  Set();
}

void cMenuEditBoolItem::Set(void)
{
  char buf[16];
  snprintf(buf, sizeof(buf), "%s", *value ? trueString : falseString);
  SetValue(buf);
}

// --- cMenuEditBitItem ------------------------------------------------------

cMenuEditBitItem::cMenuEditBitItem(const char *Name, uint *Value, uint Mask, const char *FalseString, const char *TrueString)
:cMenuEditBoolItem(Name, &bit, FalseString, TrueString)
{
  value = Value;
  bit = (*value & Mask) != 0;
  mask = Mask;
  Set();
}

void cMenuEditBitItem::Set(void)
{
  *value = bit ? *value | mask : *value & ~mask;
  cMenuEditBoolItem::Set();
}

// --- cMenuEditNumItem ------------------------------------------------------

cMenuEditNumItem::cMenuEditNumItem(const char *Name, char *Value, int Length, bool Blind)
:cMenuEditItem(Name)
{
  value = Value;
  length = Length;
  blind = Blind;
  Set();
}

void cMenuEditNumItem::Set(void)
{
  if (blind) {
     char buf[length + 1];
     int i;
     for (i = 0; i < length && value[i]; i++)
         buf[i] = '*';
     buf[i] = 0;
     SetValue(buf);
     }
  else
     SetValue(value);
}

eOSState cMenuEditNumItem::ProcessKey(eKeys Key)
{
  eOSState state = cMenuEditItem::ProcessKey(Key);

  if (state == osUnknown) {
     Key = NORMALKEY(Key);
     switch (Key) {
       case kLeft: {
            int l = strlen(value);
            if (l > 0)
               value[l - 1] = 0;
            }
            break;
       case k0 ... k9: {
            int l = strlen(value);
            if (l < length) {
               value[l] = Key - k0 + '0';
               value[l + 1] = 0;
               }
            }
            break;
       default: return state;
       }
     Set();
     state = osContinue;
     }
  return state;
}

// --- cMenuEditChrItem ------------------------------------------------------

cMenuEditChrItem::cMenuEditChrItem(const char *Name, char *Value, const char *Allowed)
:cMenuEditItem(Name)
{
  value = Value;
  allowed = strdup(Allowed);
  current = strchr(allowed, *Value);
  if (!current)
     current = allowed;
  Set();
}

cMenuEditChrItem::~cMenuEditChrItem()
{
  free(allowed);
}

void cMenuEditChrItem::Set(void)
{
  char buf[2];
  snprintf(buf, sizeof(buf), "%c", *value);
  SetValue(buf);
}

eOSState cMenuEditChrItem::ProcessKey(eKeys Key)
{
  return osContinue;
}

// --- cMenuEditStrItem ------------------------------------------------------

cMenuEditStrItem::cMenuEditStrItem(const char *Name, char *Value, int Length, const char *Allowed)
:cMenuEditItem(Name)
{
}

cMenuEditStrItem::~cMenuEditStrItem()
{
}

void cMenuEditStrItem::SetHelpKeys(void)
{
}

void cMenuEditStrItem::Set(void)
{
}

uint cMenuEditStrItem::Inc(uint c, bool Up)
{
  return '\0';
}

eOSState cMenuEditStrItem::ProcessKey(eKeys Key)
{
  return osContinue;
}

// --- cMenuEditStraItem -----------------------------------------------------

cMenuEditStraItem::cMenuEditStraItem(const char *Name, int *Value, int NumStrings, const char * const *Strings)
:cMenuEditIntItem(Name, Value, 0, NumStrings - 1)
{
}

void cMenuEditStraItem::Set(void)
{
}

// --- cMenuEditChanItem -----------------------------------------------------

cMenuEditChanItem::cMenuEditChanItem(const char *Name, int *Value, const char *NoneString)
:cMenuEditIntItem(Name, Value, NoneString ? 0 : 1, Channels.MaxNumber())
{
  noneString = NoneString;
  Set();
}

void cMenuEditChanItem::Set(void)
{
}

eOSState cMenuEditChanItem::ProcessKey(eKeys Key)
{
  return osContinue;
}

// --- cMenuEditTranItem -----------------------------------------------------

cMenuEditTranItem::cMenuEditTranItem(const char *Name, int *Value, int *Source)
:cMenuEditChanItem(Name, Value)
{
}

eOSState cMenuEditTranItem::ProcessKey(eKeys Key)
{
  return osContinue;
}

// --- cMenuEditDateItem -----------------------------------------------------

cMenuEditDateItem::cMenuEditDateItem(const char *Name, time_t *Value, int *WeekDays)
:cMenuEditItem(Name)
{
}

void cMenuEditDateItem::Set(void)
{
}

eOSState cMenuEditDateItem::ProcessKey(eKeys Key)
{
  return osContinue;
}

// --- cMenuEditTimeItem -----------------------------------------------------

cMenuEditTimeItem::cMenuEditTimeItem(const char *Name, int *Value)
:cMenuEditItem(Name)
{
}

void cMenuEditTimeItem::Set(void)
{
}

eOSState cMenuEditTimeItem::ProcessKey(eKeys Key)
{
  return osContinue;
}

// --- cMenuSetupPage --------------------------------------------------------

cMenuSetupPage::cMenuSetupPage(void)
:cOsdMenu("", 33)
{
  plugin = NULL;
}

void cMenuSetupPage::SetSection(const char *Section)
{
}

eOSState cMenuSetupPage::ProcessKey(eKeys Key)
{
  return osContinue;
}

void cMenuSetupPage::SetPlugin(cPlugin *Plugin)
{
}

void cMenuSetupPage::SetupStore(const char *Name, const char *Value)
{
}

void cMenuSetupPage::SetupStore(const char *Name, int Value)
{
}
