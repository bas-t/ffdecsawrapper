/*
 * i18n.h: Internationalization
 *
 * See the main source file 'vdr.c' for copyright information and
 * how to reach the author.
 *
 * $Id: i18n.h 1.20 2007/05/28 11:43:14 kls Exp $
 */

#ifndef __I18N_H
#define __I18N_H

#include <stdio.h>

const int I18nNumLanguages = 22;

typedef const char *tI18nPhrase[I18nNumLanguages];

void I18nRegister(const tI18nPhrase * const Phrases, const char *Plugin);

const char *I18nTranslate(const char *s, const char *Plugin = NULL) __attribute_format_arg__(1);

const char * const * I18nLanguages(void);
const char *I18nLanguageCode(int Index);
int I18nLanguageIndex(const char *Code);
const char *I18nNormalizeLanguageCode(const char *Code);
   ///< Returns a 3 letter language code that may not be zero terminated.
   ///< If no normalized language code can be found, the given Code is returned.
   ///< Make sure at most 3 characters are copied when using it!
bool I18nIsPreferredLanguage(int *PreferredLanguages, const char *LanguageCode, int &OldPreference, int *Position = NULL);
   ///< Checks the given LanguageCode (which may be something like "eng" or "eng+deu")
   ///< against the PreferredLanguages and returns true if one is found that has an index
   ///< smaller than OldPreference (which should be initialized to -1 before the first
   ///< call to this function in a sequence of checks). If LanguageCode is not any of
   ///< the PreferredLanguages, and OldPreference is less than zero, OldPreference will
   ///< be set to a value higher than the highest language index.  If Position is given,
   ///< it will return 0 if this was a single language code (like "eng"), 1 if it was
   ///< the first of two language codes (like "eng" out of "eng+deu") and 2 if it was
   ///< the second one (like "deu" out of ""eng+deu").

#ifdef PLUGIN_NAME_I18N
#define tr(s)  I18nTranslate(s, PLUGIN_NAME_I18N)
#else
#define tr(s)  I18nTranslate(s)
#endif

#define trNOOP(s) (s)

#endif //__I18N_H
