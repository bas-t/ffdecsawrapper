
bool DllsLoad(const char *libdir);
void InitAll(const char *cfgdir);
void LogAll(void);
void LogNone(void);
void SDump(const unsigned char *buffer, int n);
int ReadRaw(const char *name, unsigned char *buff, int maxlen);
