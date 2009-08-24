/*
 * skins.h: The optical appearance of the OSD
 *
 * See the main source file 'vdr.c' for copyright information and
 * how to reach the author.
 *
 * $Id: skins.h 1.15 2007/01/04 13:08:55 kls Exp $
 */

#ifndef __SKINS_H
#define __SKINS_H

#include "channels.h"
#include "epg.h"
#include "keys.h"
#include "osd.h"
#include "recording.h"
#include "themes.h"
#include "thread.h"
#include "tools.h"

enum eMessageType { mtStatus = 0, mtInfo, mtWarning, mtError }; // will be used to calculate color offsets!

class cSkinDisplay {
private:
  static cSkinDisplay *current;
  int editableWidth; //XXX this is not nice, but how else could we know this value?
public:
  cSkinDisplay(void);
  virtual ~cSkinDisplay();
  int EditableWidth(void) { return editableWidth; }
  void SetEditableWidth(int Width) { editableWidth = Width; }
       ///< If an item is set through a call to cSkinDisplayMenu::SetItem(), this
       ///< function shall be called to set the width of the rightmost tab separated
       ///< field. This information will be used for editable items.
  virtual void SetButtons(const char *Red, const char *Green = NULL, const char *Yellow = NULL, const char *Blue = NULL) {}
       ///< Sets the color buttons to the given strings, provided this cSkinDisplay
       ///< actually has a color button display.
  virtual void SetMessage(eMessageType Type, const char *Text) {}
       ///< Sets a one line message Text, with the given Type. Type can be used
       ///< to determine, e.g., the colors for displaying the Text.
  virtual void Flush(void) {}
       ///< Actually draws the OSD display to the output device.
  static cSkinDisplay *Current(void) { return NULL; }
       ///< Returns the currently active cSkinDisplay.
  };

class cSkinDisplayChannel : public cSkinDisplay {
       ///< This class is used to display the current channel, together with
       ///< the present and following EPG even. How and to what extent this
       ///< is done is totally up to the derived class.
public:
  virtual void SetChannel(const cChannel *Channel, int Number) = 0;
       ///< Sets the current channel to Channel. If Number is not 0, the
       ///< user is in the process of entering a channel number, which must
       ///< be displayed accordingly.
  virtual void SetEvents(const cEvent *Present, const cEvent *Following) = 0;
       ///< Sets the Present and Following EPG events. If either of these
       ///< is not available, NULL will be given.
  virtual void SetMessage(eMessageType Type, const char *Text) = 0;
       ///< Sets a one line message Text, with the given Type. Type can be used
       ///< to determine, e.g., the colors for displaying the Text.
       ///< If Text is NULL, any previously displayed message must be removed, and
       ///< any previous contents overwritten by the message must be restored.
  /*TODO
  SetButtons
    Red    = Video options
    Green  = Info now
    Yellow = Info next
  */
  };

class cSkinDisplayMenu : public cSkinDisplay {
       ///< This class implements the general purpose menu display, which is
       ///< used throughout the program to display information and let the
       ///< user interact with items.
       ///< A menu consists of the following fields, each of which is explicitly
       ///< set by calls to the member functions below:
       ///< - Title: a single line of text, indicating what this menu displays
       ///< - Color buttons: the red, green, yellow and blue buttons, used for
       ///<   various functions
       ///< - Message: a one line message, indicating a Status, Info, Warning,
       ///<   or Error condition
       ///< - Central area: the main central area of the menu, used to display
       ///<   one of the following:
       ///<   - Items: a list of single line items, of which the user may be
       ///<     able to select one
       ///<   - Event: the full information about one EPG event
       ///<   - Text: a multi line, scrollable text
public:
  enum { MaxTabs = 6 };
private:
  int tabs[MaxTabs];
protected:
  cTextScroller textScroller;
  int Tab(int n) { return (n >= 0 && n < MaxTabs) ? tabs[n] : 0; }
       ///< Returns the offset of the given tab from the left border of the
       ///< item display area. The value returned is in pixel.
  const char *GetTabbedText(const char *s, int Tab);
       ///< Returns the part of the given string that follows the given
       ///< Tab (where 0 indicates the beginning of the string). If no such
       ///< part can be found, NULL will be returned.
public:
  cSkinDisplayMenu(void);
  virtual void SetTabs(int Tab1, int Tab2 = 0, int Tab3 = 0, int Tab4 = 0, int Tab5 = 0);
       ///< Sets the tab columns to the given values, which are the number of
       ///< characters in each column.
  virtual void Scroll(bool Up, bool Page);
       ///< If this menu contains a text area that can be scrolled, this function
       ///< will be called to actually scroll the text. Up indicates whether the
       ///< text shall be scrolled up or down, and Page is true if it shall be
       ///< scrolled by a full page, rather than a single line. An object of the
       ///< cTextScroller class can be used to implement the scrolling text area.
  virtual int MaxItems(void) = 0;
       ///< Returns the maximum number of items the menu can display.
  virtual void Clear(void) = 0;
       ///< Clears the entire central area of the menu.
  virtual void SetTitle(const char *Title) = 0;
       ///< Sets the title of this menu to Title.
  virtual void SetButtons(const char *Red, const char *Green = NULL, const char *Yellow = NULL, const char *Blue = NULL) = 0;
       ///< Sets the color buttons to the given strings. If any of the values is
       ///< NULL, any previous text must be removed from the related button.
  virtual void SetMessage(eMessageType Type, const char *Text) = 0;
       ///< Sets a one line message Text, with the given Type. Type can be used
       ///< to determine, e.g., the colors for displaying the Text.
       ///< If Text is NULL, any previously displayed message must be removed, and
       ///< any previous contents overwritten by the message must be restored.
  virtual void SetItem(const char *Text, int Index, bool Current, bool Selectable) = 0;
       ///< Sets the item at the given Index to Text. Index is between 0 and the
       ///< value returned by MaxItems(), minus one. Text may contain tab characters ('\t'),
       ///< which shall be used to separate the text into several columns, according to the
       ///< values set by a prior call to SetTabs(). If Current is true, this item shall
       ///< be drawn in a way indicating to the user that it is the currently selected
       ///< one. Selectable can be used to display items differently that can't be
       ///< selected by the user.
       ///< Whenever the current status is moved from one item to another,
       ///< this function will be first called for the old current item
       ///< with Current set to false, and then for the new current item
       ///< with Current set to true.
  /*TODO
  virtual void SetItem(const cEvent *Event, int Index, bool Current, bool Selectable, bool NowNext???, bool Schedule???);
  virtual void SetItem(const cTimer *Timer, int Index, bool Current, bool Selectable);
  virtual void SetItem(const cChannel *Channel, int Index, bool Current, bool Selectable);
  virtual void SetItem(const cRecording *Recording, int Index, bool Current, bool Selectable);
  --> false: call SetItem(text)
  */
  virtual void SetEvent(const cEvent *Event) = 0;
       ///< Sets the Event that shall be displayed, using the entire central area
       ///< of the menu. The Event's 'description' shall be displayed using a
       ///< cTextScroller, and the Scroll() function will be called to drive scrolling
       ///< that text if necessary.
  virtual void SetRecording(const cRecording *Recording) = 0;
       ///< Sets the Recording that shall be displayed, using the entire central area
       ///< of the menu. The Recording's 'description' shall be displayed using a
       ///< cTextScroller, and the Scroll() function will be called to drive scrolling
       ///< that text if necessary.
  virtual void SetText(const char *Text, bool FixedFont) = 0;
       ///< Sets the Text that shall be displayed, using the entire central area
       ///< of the menu. The Text shall be displayed using a cTextScroller, and
       ///< the Scroll() function will be called to drive scrolling that text if
       ///< necessary.
  //XXX ??? virtual void SetHelp(const char *Help) = 0;
  virtual int GetTextAreaWidth(void) const;
       ///< Returns the width in pixel of the area which is used to display text
       ///< with SetText(). The width of the area is the width of the central area
       ///< minus the width of any possibly displayed scroll-bar or other decoration.
       ///< The default implementation returns 0. Therefore a caller of this method
       ///< must be prepared to receive 0 if the plugin doesn't implement this method.
  virtual const cFont *GetTextAreaFont(bool FixedFont) const;
       ///< Returns a pointer to the font which is used to display text with SetText().
       ///< The parameter FixedFont has the same meaning as in SetText(). The default
       ///< implementation returns NULL. Therefore a caller of this method must be
       ///< prepared to receive NULL if the plugin doesn't implement this method.
       ///< The returned pointer is valid a long as the instance of cSkinDisplayMenu
       ///< exists.
  };

class cSkinDisplayReplay : public cSkinDisplay {
       ///< This class implements the progress display used during replay of
       ///< a recording.
protected:
  const cMarks *marks;
  class cProgressBar : public cBitmap {
  protected:
    int total;
    int Pos(int p) { return p * Width() / total; }
    void Mark(int x, bool Start, bool Current, tColor ColorMark, tColor ColorCurrent);
  public:
    cProgressBar(int Width, int Height, int Current, int Total, const cMarks *Marks, tColor ColorSeen, tColor ColorRest, tColor ColorSelected, tColor ColorMark, tColor ColorCurrent);
    };
public:
  cSkinDisplayReplay(void);
  virtual void SetMarks(const cMarks *Marks);
       ///< Sets the editing marks to Marks, which shall be used to display the
       ///< progress bar through a cProgressBar object.
  virtual void SetTitle(const char *Title) = 0;
       ///< Sets the title of the recording.
  virtual void SetMode(bool Play, bool Forward, int Speed) = 0;
       ///< Sets the current replay mode, which can be used to display some
       ///< indicator, showing the user whether we are currently in normal
       ///< play mode, fast forward etc.
  virtual void SetProgress(int Current, int Total) = 0;
       ///< This function will be called whenever the position in or the total
       ///< length of the recording has changed. A cProgressBar shall then be
       ///< used to display a progress indicator.
  virtual void SetCurrent(const char *Current) = 0;
       ///< Sets the current position within the recording, as a user readable
       ///< string if the form "h:mm:ss.ff". The ".ff" part, indicating the
       ///< frame number, is optional and the actual implementation needs to
       ///< take care that it is erased from the display when a Current string
       ///< _with_ ".ff" is followed by one without it.
  virtual void SetTotal(const char *Total) = 0;
       ///< Sets the total length of the recording, as a user readable
       ///< string if the form "h:mm:ss".
  virtual void SetJump(const char *Jump) = 0;
       ///< Sets the prompt that allows the user to enter a jump point.
       ///< Jump is a string of the form "Jump: mm:ss". The actual implementation
       ///< needs to be able to handle variations in the length of this
       ///< string, which will occur when the user enters an actual value.
       ///< If Jump is NULL, the jump prompt shall be removed from the display.
  virtual void SetMessage(eMessageType Type, const char *Text) = 0;
       ///< Sets a one line message Text, with the given Type. Type can be used
       ///< to determine, e.g., the colors for displaying the Text.
       ///< If Text is NULL, any previously displayed message must be removed, and
       ///< any previous contents overwritten by the message must be restored.
  };

class cSkinDisplayVolume : public cSkinDisplay {
       ///< This class implements the volume/mute display.
public:
  virtual void SetVolume(int Current, int Total, bool Mute) = 0;
       ///< Sets the volume to the given Current value, which is in the range
       ///< 0...Total. If Mute is true, audio is currently muted and a "mute"
       ///< indicator shall be displayed.
  };

class cSkinDisplayTracks : public cSkinDisplay {
       ///< This class implements the track display.
public:
  virtual void SetTrack(int Index, const char * const *Tracks) = 0;
       ///< Sets the current track to the one given by Index, which
       ///< points into the Tracks array of strings.
  virtual void SetAudioChannel(int AudioChannel) = 0;
       ///< Sets the audio channel indicator.
       ///< 0=stereo, 1=left, 2=right, -1=don't display the audio channel indicator.
  };

class cSkinDisplayMessage : public cSkinDisplay {
       ///< This class implements a simple message display.
public:
  virtual void SetMessage(eMessageType Type, const char *Text) = 0;
       ///< Sets the message to Text. Type can be used to decide how to display
       ///< the message, for instance in which colors.
  };

class cSkin : public cListObject {
private:
  char *name;
  cTheme *theme;
public:
  cSkin(const char *Name, cTheme *Theme = NULL);
       ///< Creates a new skin class, with the given Name and Theme.
       ///< Name will be used to identify this skin in the 'setup.conf'
       ///< file, and is normally not seen by the user. It should
       ///< consist of only lowercase letters and digits.
       ///< Theme must be a static object that survives the entire lifetime
       ///< of this skin.
       ///< The constructor of a derived class shall not set up any data
       ///< structures yet, because whether or not this skin will actually
       ///< be used is not yet known at this point. All actual work shall
       ///< be done in the pure functions below.
       ///< A cSkin object must be created on the heap and shall not be
       ///< explicitly deleted.
  virtual ~cSkin();
  const char *Name(void) { return name; }
  cTheme *Theme(void) { return theme; }
  virtual const char *Description(void) = 0;
       ///< Returns a user visible, single line description of this skin,
       ///< which may consist of arbitrary text and can, if the skin
       ///< implementation wishes to do so, be internationalized.
       ///< The actual text shouldn't be too long, so that it can be
       ///< fully displayed in the Setup/OSD menu.
  virtual cSkinDisplayChannel *DisplayChannel(bool WithInfo) = 0;
       ///< Creates and returns a new object for displaying the current
       ///< channel. WithInfo indicates whether it shall display only
       ///< the basic channel data, or also information about the present
       ///< and following EPG event.
       ///< The caller must delete the object after use.
  virtual cSkinDisplayMenu *DisplayMenu(void) = 0;
       ///< Creates and returns a new object for displaying a menu.
       ///< The caller must delete the object after use.
  virtual cSkinDisplayReplay *DisplayReplay(bool ModeOnly) = 0;
       ///< Creates and returns a new object for displaying replay progress.
       ///< ModeOnly indicates whether this should be a full featured replay
       ///< display, or just a replay mode indicator.
       ///< The caller must delete the object after use.
  virtual cSkinDisplayVolume *DisplayVolume(void) = 0;
       ///< Creates and returns a new object for displaying the current volume.
       ///< The caller must delete the object after use.
  virtual cSkinDisplayTracks *DisplayTracks(const char *Title, int NumTracks, const char * const *Tracks) = 0;
       ///< Creates and returns a new object for displaying the available tracks.
       ///< NumTracks indicates how many entries in Tracks are available.
       ///< Tracks will be valid throughout the entire lifetime of the returned
       ///< cSkinDisplayTrack object.
       ///< The caller must delete the object after use.
  virtual cSkinDisplayMessage *DisplayMessage(void) = 0;
       ///< Creates and returns a new object for displaying a message.
       ///< The caller must delete the object after use.
  };

class cSkins : public cList<cSkin> {
private:
  cSkin *current;
  cSkinDisplayMessage *displayMessage;
  cMutex queueMessageMutex;
public:
  cSkins(void);
  ~cSkins();
  bool SetCurrent(const char *Name = NULL);
       ///< Sets the current skin to the one indicated by name.
       ///< If no such skin can be found, the first one will be used.
  cSkin *Current(void) { return current; }
       ///< Returns a pointer to the current skin.
  bool IsOpen(void) { return cSkinDisplay::Current(); }
       ///< Returns true if there is currently a skin display object active.
  eKeys Message(eMessageType Type, const char *s, int Seconds = 0);
       ///< Displays the given message, either through a currently visible
       ///< display object that is capable of doing so, or by creating a
       ///< temporary cSkinDisplayMessage object.
       ///< The return value is the key pressed by the user. If no user input
       ///< has been received within Seconds (the default value of 0 results
       ///< in the value defined for "Message time" in the setup), kNone
       ///< will be returned.
  int QueueMessage(eMessageType Type, const char *s, int Seconds = 0, int Timeout = 0);
       ///< Like Message(), but this function may be called from a background
       ///< thread. The given message is put into a queue and the main program
       ///< loop will display it as soon as this is suitable. If Timeout is 0,
       ///< QueueMessage() returns immediately and the return value will be kNone.
       ///< If a positive Timeout is given, the thread will wait at most the given
       ///< number of seconds for the message to be actually displayed (note that
       ///< the user may currently be doing something that doesn't allow for
       ///< queued messages to be displayed immediately). If the timeout expires
       ///< and the message hasn't been displayed yet, the return value is -1
       ///< and the message will be removed from the queue without being displayed.
       ///< Positive values of Timeout are only allowed for background threads.
       ///< If QueueMessage() is called from the foreground thread with a Timeout
       ///< greater than 0, the call is ignored and nothing is displayed.
       ///< Queued messages will be displayed in the sequence they have been
       ///< put into the queue, so messages from different threads may appear
       ///< mingled. If a particular thread queues a message with a Timeout of
       ///< -1, and the previous message from the same thread also had a Timeout
       ///< of -1, only the last message will be displayed. This can be used for
       ///< progress displays, where only the most recent message is actually
       ///< important.
       ///< Type may only be mtInfo, mtWarning or mtError. A call with mtStatus
       ///< will be ignored. A call with an empty message from a background thread
       ///< removes all queued messages from the calling thread. A call with
       ///< an empty message from the main thread will be ignored.
  void ProcessQueuedMessages(void);
       ///< Processes the first queued message, if any.
  void Flush(void);
       ///< Flushes the currently active cSkinDisplay, if any.
  virtual void Clear(void);
       ///< Free up all registered skins
  };

extern cSkins Skins;

#endif //__SKINS_H
