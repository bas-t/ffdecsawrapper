/*
 * csa.h:
 *
 */

#ifndef __CSA_H
#define __CSA_H

#include <linux/dvb/ca.h>

//#define HAVE_SOFTCSA // make this patch detectable
//#define SOFTCSA_VERS 100 // ff stands for FFdecsa

class cCSA {
private:
  unsigned char even_ck[8], odd_ck[8];
  unsigned char queued_even_ck[8], queued_odd_ck[8];
  bool even_is_queued, odd_is_queued;
  bool force_dequeueing;
public:
  cCSA(void);
  void SetDescr(ca_descr_t *ca_descr);
  void SetCaPid(ca_pid_t *ca_pid);
  };

#endif //__CSA_H
