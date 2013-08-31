#include <stdio.h>
#include "log.h"
extern int tmprintf(const char *plugin, const char *fmt, ...);
static const char *plugin_name;
static unsigned  *log_level, print_level, id;

static void LogPrintSasc(const struct LogHeader *lh, const char *txt) {
  char tag[80];
  if(print_level <= ((*log_level >> id) & 3)) {
    sprintf(tag, "%s(%s)", plugin_name, lh->tag);
    tmprintf(tag, "%s\n", txt);
  }
}

void SetCAMPrint(const char *_plugin_name, unsigned int plugin_id, unsigned int _print_level, unsigned int *_log_level) {
  plugin_name = _plugin_name;
  id = plugin_id;
  log_level = _log_level;
  print_level = _print_level;
  cLogging::SetLogPrint(&LogPrintSasc);
}
