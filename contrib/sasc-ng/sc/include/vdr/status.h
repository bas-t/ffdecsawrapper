/*
 * status.h: Status monitoring
 *
 * See the main source file 'vdr.c' for copyright information and
 * how to reach the author.
 *
 * $Id: status.h 1.9 2005/12/31 15:15:25 kls Exp $
 */

#ifndef __STATUS_H
#define __STATUS_H

#include "config.h"
#include "device.h"
#include "player.h"
#include "tools.h"

class cStatus : public cListObject {
private:
  static cList<cStatus> statusMonitors;
protected:
  // These functions can be implemented by derived classes to receive status information:
  virtual void ChannelSwitch(const cDevice *Device, int ChannelNumber) {}
               // Indicates a channel switch on the given DVB device.
               // If ChannelNumber is 0, this is before the channel is being switched,
               // otherwise ChannelNumber is the number of the channel that has been switched to.
  virtual void Recording(const cDevice *Device, const char *Name, const char *FileName, bool On) {}
               // The given DVB device has started (On = true) or stopped (On = false) recording Name.
               // Name is the name of the recording, without any directory path. The full file name
               // of the recording is given in FileName, which may be NULL in case there is no
               // actual file involved. If On is false, Name may be NULL.
  virtual void Replaying(const cControl *Control, const char *Name, const char *FileName, bool On) {}
               // The given player control has started (On = true) or stopped (On = false) replaying Name.
               // Name is the name of the recording, without any directory path. In case of a player that can't provide
               // a name, Name can be a string that identifies the player type (like, e.g., "DVD").
               // The full file name of the recording is given in FileName, which may be NULL in case there is no
               // actual file involved. If On is false, Name may be NULL.
  virtual void SetVolume(int Volume, bool Absolute) {}
               // The volume has been set to the given value, either
               // absolutely or relative to the current volume.
  virtual void SetAudioTrack(int Index, const char * const *Tracks) {}
               // The audio track has been set to the one given by Index, which
               // points into the Tracks array of strings.
  virtual void SetAudioChannel(int AudioChannel) {}
               // The audio channel has been set to the given value.
               // 0=stereo, 1=left, 2=right, -1=no information available.
  virtual void OsdClear(void) {}
               // The OSD has been cleared.
  virtual void OsdTitle(const char *Title) {}
               // Title has been displayed in the title line of the menu.
  virtual void OsdStatusMessage(const char *Message) {}
               // Message has been displayed in the status line of the menu.
               // If Message is NULL, the status line has been cleared.
  virtual void OsdHelpKeys(const char *Red, const char *Green, const char *Yellow, const char *Blue) {}
               // The help keys have been set to the given values (may be NULL).
  virtual void OsdItem(const char *Text, int Index) {}
               // The OSD displays the given single line Text as menu item at Index.
  virtual void OsdCurrentItem(const char *Text) {}
               // The OSD displays the given single line Text as the current menu item.
  virtual void OsdTextItem(const char *Text, bool Scroll) {}
               // The OSD displays the given multi line text. If Text points to an
               // actual string, that text shall be displayed and Scroll has no
               // meaning. If Text is NULL, Scroll defines whether the previously
               // received text shall be scrolled up (true) or down (false) and
               // the text shall be redisplayed with the new offset.
  virtual void OsdChannel(const char *Text) {}
               // The OSD displays the single line Text with the current channel information.
  virtual void OsdProgramme(time_t PresentTime, const char *PresentTitle, const char *PresentSubtitle, time_t FollowingTime, const char *FollowingTitle, const char *FollowingSubtitle) {}
               // The OSD displays the given programme information.
public:
  cStatus(void);
  virtual ~cStatus();
  // These functions are called whenever the related status information changes:
  static void MsgChannelSwitch(const cDevice *Device, int ChannelNumber);
  static void MsgRecording(const cDevice *Device, const char *Name, const char *FileName, bool On);
  static void MsgReplaying(const cControl *Control, const char *Name, const char *FileName, bool On);
  static void MsgSetVolume(int Volume, bool Absolute);
  static void MsgSetAudioTrack(int Index, const char * const *Tracks);
  static void MsgSetAudioChannel(int AudioChannel);
  static void MsgOsdClear(void);
  static void MsgOsdTitle(const char *Title);
  static void MsgOsdStatusMessage(const char *Message);
  static void MsgOsdHelpKeys(const char *Red, const char *Green, const char *Yellow, const char *Blue);
  static void MsgOsdItem(const char *Text, int Index);
  static void MsgOsdCurrentItem(const char *Text);
  static void MsgOsdTextItem(const char *Text,  bool Scroll = false);
  static void MsgOsdChannel(const char *Text);
  static void MsgOsdProgramme(time_t PresentTime, const char *PresentTitle, const char *PresentSubtitle, time_t FollowingTime, const char *FollowingTitle, const char *FollowingSubtitle);
  };

#endif //__STATUS_H
