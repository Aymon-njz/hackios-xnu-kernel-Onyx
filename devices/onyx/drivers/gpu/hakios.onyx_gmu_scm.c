#include "hakios.onyx_gmu_scm.h"
static int call(struct onyx_scm *s,uint32_t function,uint32_t argument,uint64_t *result)
{
 uint64_t continuation=0;
 for(unsigned n=0;n<64;n++) {
  uint64_t r[8]={function,1,argument,0,0,0,continuation,0};
  s->invoke(s->context,r);
  s->firmware_status=(int64_t)r[0];
  if(r[0]==0) { *result=r[1]; return ONYX_SCM_OK; }
  if(r[0]==1) { function=1; continuation=r[6]; continue; }
  s->quarantined=1;
  if(r[0]==2 || r[0]==3) return ONYX_SCM_ASYNC;
  return ONYX_SCM_ERROR;
 }
 s->quarantined=1;
 return ONYX_SCM_INTERRUPTED_LIMIT;
}
int onyx_gmu_secure_cx(struct onyx_scm *s,unsigned features)
{
 /* Vendor GPU requests: mandatory ALWAYS bit0 + TSENSE bit3;
  * optional BCL bit1, CLX bit2 must come from the target core profile. */
 if(!s || !s->invoke || !s->owned || s->quarantined || (features&~6u))
  return ONYX_SCM_ARGUMENT;
 uint64_t result=0;
 /* Standard SMC64, SIP owner2, INFO service6 cmd1; queried ID omits
  * calling-convention/type bits, matching qcom_scm_is_call_available. */
 int rc=call(s,0x42000601u,0x02002801u,&result);
 if(rc) return rc;
 if(!result) return ONYX_SCM_UNAVAILABLE;
 return call(s,0x42002801u,9u|features,&result);
}
#if defined(__aarch64__)
void onyx_scm_arm64_invoke(void *unused,uint64_t r[8])
{
 (void)unused;
 register uint64_t x0 __asm__("x0")=r[0], x1 __asm__("x1")=r[1];
 register uint64_t x2 __asm__("x2")=r[2], x3 __asm__("x3")=r[3];
 register uint64_t x4 __asm__("x4")=r[4], x5 __asm__("x5")=r[5];
 register uint64_t x6 __asm__("x6")=r[6], x7 __asm__("x7")=r[7];
 __asm__ volatile("dsb sy; smc #0; dsb sy" : "+r"(x0),"+r"(x1),"+r"(x2),"+r"(x3),
     "+r"(x4),"+r"(x5),"+r"(x6),"+r"(x7) : : "memory","cc",
     "x8","x9","x10","x11","x12","x13","x14","x15","x16","x17");
 r[0]=x0;r[1]=x1;r[2]=x2;r[3]=x3;r[4]=x4;r[5]=x5;r[6]=x6;r[7]=x7;
}
#endif
