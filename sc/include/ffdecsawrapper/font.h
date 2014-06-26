/*
 * font.h: Font handling for the DVB On Screen Display
 *
 * See the main source file 'vdr.c' for copyright information and
 * how to reach the author.
 *
 * $Id: font.h 1.20 2007/06/23 10:09:14 kls Exp $
 */

#ifndef __FONT_H
#define __FONT_H

#include <stdint.h>
#include <stdlib.h>
#include "tools.h"

#define MAXFONTNAME 64
#define MAXFONTSIZE 64

enum eDvbFont {
  fontOsd,
  fontFix,
  fontSml
#define eDvbFontSize (fontSml + 1)
  };

class cBitmap;
typedef uint32_t tColor; // see also osd.h
typedef uint8_t tIndex;

extern const char *DefaultFontOsd;
extern const char *DefaultFontSml;
extern const char *DefaultFontFix;

class cFont {
private:
  static cFont *fonts[];
public:
  virtual ~cFont() {}
  virtual int Width(uint c) const = 0;
          ///< Returns the width of the given character in pixel.
  virtual int Width(const char *s) const = 0;
          ///< Returns the width of the given string in pixel.
  virtual int Height(void) const = 0;
          ///< Returns the height of this font in pixel (all characters have the same height).
  int Height(const char *s) const { return Height(); }
          ///< Returns the height of this font in pixel (obsolete, just for backwards compatibilty).
  virtual void DrawText(cBitmap *Bitmap, int x, int y, const char *s, tColor ColorFg, tColor ColorBg, int Width) const = 0;
          ///< Draws the given text into the Bitmap at position (x, y) with the given colors.
          ///< The text will not exceed the given Width (if > 0), and will end with a complete character.
  static void SetFont(eDvbFont Font, const char *Name, int CharHeight);
          ///< Sets the given Font to use the font data according to Name (see CreateFont())
          ///< and make its characters CharHeight pixels high.
  static const cFont *GetFont(eDvbFont Font);
          ///< Gets the given Font, which was previously set by a call to SetFont().
          ///< If no SetFont() call has been made, the font as defined in the setup is returned.
          ///< The caller must not use the returned font outside the scope in which
          ///< it was retrieved by the call to GetFont(), because a call to SetFont()
          ///< may delete an existing font.
  static cFont *CreateFont(const char *Name, int CharHeight, int CharWidth = 0);
          ///< Creates a new font object with the given Name and makes its characters
          ///< CharHeight pixels high. If CharWidth is given, it overwrites the font's
          ///< default width. Name is of the form "Family:Style", for instance
          ///< "Verdana:Bold Italic" or "Times New Roman". See GetAvailableFontNames()
          ///< for how to get a list of all available font names.
          ///< If the requested font can't be created, NULL is returned.
          ///< The caller must delete the font when it is no longer needed.
  static bool GetAvailableFontNames(cStringList *FontNames, bool Monospaced = false);
          ///< Queries the font configuration for a list of available font names,
          ///< which is returned in FontNames. If Monospaced is true, only
          ///< monospaced fonts will be returned. The resulting font names are
          ///< in a format that can be used with GetFontFileName() to get the name
          ///< of the actual font file.
          ///< Returns true if any font names were found.
  static cString GetFontFileName(const char *FontName);
          ///< Retruns the actual font file name for the given FontName.
  };

class cTextWrapper {
private:
  char *text;
  char *eol;
  int lines;
  int lastLine;
public:
  cTextWrapper(void);
  cTextWrapper(const char *Text, const cFont *Font, int Width);
  ~cTextWrapper();
  void Set(const char *Text, const cFont *Font, int Width);
      ///< Wraps the Text to make it fit into the area defined by the given Width
      ///< when displayed with the given Font.
      ///< Wrapping is done by inserting the necessary number of newline
      ///< characters into the string.
  const char *Text(void);
      ///< Returns the full wrapped text.
  int Lines(void) { return lines; }
      ///< Returns the actual number of lines needed to display the full wrapped text.
  const char *GetLine(int Line);
      ///< Returns the given Line. The first line is numbered 0.
  };

#endif //__FONT_H
