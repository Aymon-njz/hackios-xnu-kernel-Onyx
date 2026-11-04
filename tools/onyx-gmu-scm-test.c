#include "hakios.onyx_gmu_scm.h"
#include <assert.h>
#include <stdio.h>
static unsigned mode,calls;
static void invoke(void *p,uint64_t r[8]) {
 (void)p;calls++;
 assert(r[1]==1);
 if(calls==1) {
  assert(r[0]==0x42000601 && r[2]==0x02002801);
  r[0]=0;r[1]=mode==1?0:1;return;
 }
 if(calls==2)assert(r[0]==0x42002801 && r[2]==15);
 else assert(r[0]==1 && r[6]==0x1234 && r[2]==15);
 r[0]=mode==2?2:mode==3?(uint64_t)-12:mode==4?1:mode==5&&calls==2?1:0;
 r[6]=0x1234;
}
int main(void) {
 for(mode=0;mode<6;mode++) {
  calls=0;struct onyx_scm s={.invoke=invoke,.owned=1};
  int result=onyx_gmu_secure_cx(&s,6);
  if(mode==0 || mode==5)assert(result==0 && !s.quarantined && calls==(mode==5?3u:2u));
  if(mode==1)assert(result==ONYX_SCM_UNAVAILABLE && calls==1);
  if(mode==2)assert(result==ONYX_SCM_ASYNC && s.quarantined);
  if(mode==3)assert(result==ONYX_SCM_ERROR && s.firmware_status==-12);
  if(mode==4)assert(result==ONYX_SCM_INTERRUPTED_LIMIT && calls==65 && s.quarantined);
 }
 calls=0;struct onyx_scm s={.invoke=invoke};
 assert(onyx_gmu_secure_cx(&s,0)==ONYX_SCM_ARGUMENT && !calls);
 puts("SCM protocol simulation tests passed; no SMC executed");
}
