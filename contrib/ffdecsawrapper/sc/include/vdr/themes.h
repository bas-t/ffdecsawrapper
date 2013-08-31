/*
 * themes.h: Color themes used by skins
 *
 * See the main source file 'vdr.c' for copyright information and
 * how to reach the author.
 *
 * $Id: themes.h 1.1 2004/05/15 14:22:16 kls Exp $
 */

#ifndef __THEMES_H
#define __THEMES_H

#include "i18n.h"
#include "osd.h"

class cTheme {
public:
  enum { MaxThemeColors = 128 };
private:
  char *name;
  char *descriptions[I18nNumLanguages];
  char *colorNames[MaxThemeColors];
  tColor colorValues[MaxThemeColors];
  bool FileNameOk(const char *FileName, bool SetName = false);
public:
  cTheme(void);
       ///< Creates a new theme class.
  ~cTheme();
  const char *Name(void) { return name; }
  const char *Description(void);
       ///< Returns a user visible, single line description of this theme.
       ///< The actual text shouldn't be too long, so that it can be
       ///< fully displayed in the Setup/OSD menu.
  bool Load(const char *FileName, bool OnlyDescriptions = false);
       ///< Loads the theme data from the given file.
  bool Save(const char *FileName);
       ///< Saves the theme data to the given file.
       ///< FileName must be in the form "<skin>-<theme>.theme", where <skin>
       ///< is the name of the skin this theme applies to, and <theme> is the
       ///< actual theme name, which will be used to identify this theme in the
       ///< 'setup.conf', and is normally not seen by the user. It should
       ///< consist of only lowercase letters and digits.
  int AddColor(const char *Name, tColor Color);
       ///< Adds a color with the given Name to this theme, initializes it
       ///< with Color and returns an index into the color array that can
       ///< be used in a call to Color() later. The index returned from the
       ///< first call to AddColor() is 0, and subsequent calls will return
       ///< values that are incremented by 1 with every call.
       ///< If a color entry with the given Name already exists, its value
       ///< will be overwritten with Color and the returned index will be
       ///< that of the existing entry.
  tColor Color(int Subject);
       ///< Returns the color for the given Subject. Subject must be one of
       ///< the values returned by a previous call to AddColor().
  };

// A helper macro that simplifies defining theme colors.
#define THEME_CLR(Theme, Subject, Color) static const int Subject = Theme.AddColor(#Subject, Color)

class cThemes {
private:
  int numThemes;
  char **names;
  char **fileNames;
  char **descriptions;
  static char *themesDirectory;
  void Clear(void);
public:
  cThemes(void);
  ~cThemes();
  bool Load(const char *SkinName);
  int NumThemes(void) { return numThemes; }
  const char *Name(int Index) { return Index < numThemes ? names[Index] : NULL; }
  const char *FileName(int Index) { return Index < numThemes ? fileNames[Index] : NULL; }
  const char * const *Descriptions(void) { return descriptions; }
  int GetThemeIndex(const char *Description);
  static void SetThemesDirectory(const char *ThemesDirectory);
  static void Load(const char *SkinName, const char *ThemeName, cTheme *Theme);
  static void Save(const char *SkinName, cTheme *Theme);
  };

#endif //__THEMES_H
