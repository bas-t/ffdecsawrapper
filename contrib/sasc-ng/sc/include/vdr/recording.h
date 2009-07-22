/*
 * recording.h: Recording file handling
 *
 * See the main source file 'vdr.c' for copyright information and
 * how to reach the author.
 *
 * $Id: recording.h 1.57 2007/06/17 12:53:05 kls Exp $
 */

#ifndef __RECORDING_H
#define __RECORDING_H

#include <time.h>
#include "channels.h"
#include "config.h"
#include "epg.h"
#include "thread.h"
#include "timers.h"
#include "tools.h"

extern bool VfatFileSystem;

void RemoveDeletedRecordings(void);
void AssertFreeDiskSpace(int Priority = 0, bool Force = false);
     ///< The special Priority value -1 means that we shall get rid of any
     ///< deleted recordings faster than normal (because we're cutting).
     ///< If Force is true, the check will be done even if the timeout
     ///< hasn't expired yet.

class cResumeFile {
private:
  char *fileName;
public:
  cResumeFile(const char *FileName);
  ~cResumeFile();
  int Read(void);
  bool Save(int Index);
  void Delete(void);
  };

class cRecordingInfo {
  friend class cRecording;
private:
  tChannelID channelID;
  char *channelName;
  const cEvent *event;
  cEvent *ownEvent;
  char *aux;
  cRecordingInfo(const cChannel *Channel = NULL, const cEvent *Event = NULL);
  void SetData(const char *Title, const char *ShortText, const char *Description);
  void SetAux(const char *Aux);
public:
  ~cRecordingInfo();
  tChannelID ChannelID(void) const { return channelID; }
  const char *ChannelName(void) const { return channelName; }
  const char *Title(void) const { return event->Title(); }
  const char *ShortText(void) const { return event->ShortText(); }
  const char *Description(void) const { return event->Description(); }
  const cComponents *Components(void) const { return event->Components(); }
  const char *Aux(void) const { return aux; }
  bool Read(FILE *f);
  bool Write(FILE *f, const char *Prefix = "") const;
  };

class cRecording : public cListObject {
  friend class cRecordings;
private:
  mutable int resume;
  mutable char *titleBuffer;
  mutable char *sortBuffer;
  mutable char *fileName;
  mutable char *name;
  mutable int fileSizeMB;
  cRecordingInfo *info;
  static char *StripEpisodeName(char *s);
  char *SortName(void) const;
  int GetResume(void) const;
public:
  time_t start;
  int priority;
  int lifetime;
  time_t deleted;
  cRecording(cTimer *Timer, const cEvent *Event);
  cRecording(const char *FileName);
  virtual ~cRecording();
  virtual int Compare(const cListObject &ListObject) const;
  const char *Name(void) const { return name; }
  const char *FileName(void) const;
  const char *Title(char Delimiter = ' ', bool NewIndicator = false, int Level = -1) const;
  const cRecordingInfo *Info(void) const { return info; }
  const char *PrefixFileName(char Prefix);
  int HierarchyLevels(void) const;
  void ResetResume(void) const;
  bool IsNew(void) const { return GetResume() <= 0; }
  bool IsEdited(void) const;
  bool WriteInfo(void);
  bool Delete(void);
       // Changes the file name so that it will no longer be visible in the "Recordings" menu
       // Returns false in case of error
  bool Remove(void);
       // Actually removes the file from the disk
       // Returns false in case of error
  };

class cRecordings : public cList<cRecording>, public cThread {
private:
  static char *updateFileName;
  bool deleted;
  time_t lastUpdate;
  int state;
  const char *UpdateFileName(void);
  void Refresh(bool Foreground = false);
  void ScanVideoDir(const char *DirName, bool Foreground = false, int LinkLevel = 0);
protected:
  void Action(void);
public:
  cRecordings(bool Deleted = false);
  virtual ~cRecordings();
  bool Load(void) { return Update(true); }
       ///< Loads the current list of recordings and returns true if there
       ///< is anything in it (for compatibility with older plugins - use
       ///< Update(true) instead).
  bool Update(bool Wait = false);
       ///< Triggers an update of the list of recordings, which will run
       ///< as a separate thread if Wait is false. If Wait is true, the
       ///< function returns only after the update has completed.
       ///< Returns true if Wait is true and there is anyting in the list
       ///< of recordings, false otherwise.
  void TouchUpdate(void);
       ///< Touches the '.update' file in the video directory, so that other
       ///< instances of VDR that access the same video directory can be triggered
       ///< to update their recordings list.
  bool NeedsUpdate(void);
  void ChangeState(void) { state++; }
  bool StateChanged(int &State);
  void ResetResume(const char *ResumeFileName = NULL);
  cRecording *GetByName(const char *FileName);
  void AddByName(const char *FileName, bool TriggerUpdate = true);
  void DelByName(const char *FileName);
  int TotalFileSizeMB(void); ///< Only for deleted recordings!
  };

extern cRecordings Recordings;
extern cRecordings DeletedRecordings;

class cMark : public cListObject {
public:
  int position;
  char *comment;
  cMark(int Position = 0, const char *Comment = NULL);
  virtual ~cMark();
  cString ToText(void);
  bool Parse(const char *s);
  bool Save(FILE *f);
  };

class cMarks : public cConfig<cMark> {
public:
  bool Load(const char *RecordingFileName);
  void Sort(void);
  cMark *Add(int Position);
  cMark *Get(int Position);
  cMark *GetPrev(int Position);
  cMark *GetNext(int Position);
  };

#define RUC_BEFORERECORDING "before"
#define RUC_AFTERRECORDING  "after"
#define RUC_EDITEDRECORDING "edited"

class cRecordingUserCommand {
private:
  static const char *command;
public:
  static void SetCommand(const char *Command) { command = Command; }
  static void InvokeCommand(const char *State, const char *RecordingFileName);
  };

//XXX+
#define FRAMESPERSEC 25

// The maximum size of a single frame (up to HDTV 1920x1080):
#define MAXFRAMESIZE  KILOBYTE(512)

// The maximum file size is limited by the range that can be covered
// with 'int'. 4GB might be possible (if the range is considered
// 'unsigned'), 2GB should be possible (even if the range is considered
// 'signed'), so let's use 2000MB for absolute safety (the actual file size
// may be slightly higher because we stop recording only before the next
// 'I' frame, to have a complete Group Of Pictures):
#define MAXVIDEOFILESIZE 2000 // MB
#define MINVIDEOFILESIZE  100 // MB

class cIndexFile {
private:
  struct tIndex { int offset; uchar type; uchar number; short reserved; };
  int f;
  char *fileName;
  int size, last;
  tIndex *index;
  cResumeFile resumeFile;
  cMutex mutex;
  bool CatchUp(int Index = -1);
public:
  cIndexFile(const char *FileName, bool Record);
  ~cIndexFile();
  bool Ok(void) { return index != NULL; }
  bool Write(uchar PictureType, uchar FileNumber, int FileOffset);
  bool Get(int Index, uchar *FileNumber, int *FileOffset, uchar *PictureType = NULL, int *Length = NULL);
  int GetNextIFrame(int Index, bool Forward, uchar *FileNumber = NULL, int *FileOffset = NULL, int *Length = NULL, bool StayOffEnd = false);
  int Get(uchar FileNumber, int FileOffset);
  int Last(void) { CatchUp(); return last; }
  int GetResume(void) { return resumeFile.Read(); }
  bool StoreResume(int Index) { return resumeFile.Save(Index); }
  bool IsStillRecording(void);
  };

class cFileName {
private:
  cUnbufferedFile *file;
  int fileNumber;
  char *fileName, *pFileNumber;
  bool record;
  bool blocking;
public:
  cFileName(const char *FileName, bool Record, bool Blocking = false);
  ~cFileName();
  const char *Name(void) { return fileName; }
  int Number(void) { return fileNumber; }
  cUnbufferedFile *Open(void);
  void Close(void);
  cUnbufferedFile *SetOffset(int Number, int Offset = 0);
  cUnbufferedFile *NextFile(void);
  };

cString IndexToHMSF(int Index, bool WithFrame = false);
      // Converts the given index to a string, optionally containing the frame number.
int HMSFToIndex(const char *HMSF);
      // Converts the given string (format: "hh:mm:ss.ff") to an index.
int SecondsToFrames(int Seconds); //XXX+ ->player???
      // Returns the number of frames corresponding to the given number of seconds.

int ReadFrame(cUnbufferedFile *f, uchar *b, int Length, int Max);

char *ExchangeChars(char *s, bool ToFileSystem);
      // Exchanges the characters in the given string to or from a file system
      // specific representation (depending on ToFileSystem). The given string will
      // be modified and may be reallocated if more space is needed. The return
      // value points to the resulting string, which may be different from s.

#endif //__RECORDING_H
