class cCam;
class cSascDvbDevice;

class sascCam {
private:
 cCam *cam;
 cSascDvbDevice *dev;
public:
  sascCam(int devNum);
  void Stop();
  void Tune(cChannel *ch);
  void AddPrg(int sid, int *epid, const unsigned char *pmt, int pmtlen);
};
