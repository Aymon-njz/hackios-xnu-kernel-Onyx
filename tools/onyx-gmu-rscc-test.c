#include "hakios.onyx_gmu_rscc.h"
#include <assert.h>
#include <stdio.h>
struct state {unsigned ack,busy,writes,delays,last;};
static uint32_t read_reg(void *p,uint32_t offset) {
 struct state *s=p;
 assert(offset==0x8ec20 || offset==0x404);
 return offset==0x8ec20 ? s->ack:s->busy;
}
static void write_reg(void *p,uint32_t offset,uint32_t value) {
 struct state *s=p;assert(offset==0x8ec1c);s->writes++;s->last=value;
}
static void delay(void *p,unsigned us) {struct state *s=p;assert(us==10);s->delays++;}
int main(void) {
 for(unsigned mode=0;mode<4;mode++) {
  struct state s={.ack=mode==2?0:2,.busy=mode==3?1:0};
  struct onyx_rsc_io io={.context=&s,.read=read_reg,.write=write_reg,.delay_us=delay,.owned=1};
  unsigned asleep=mode!=0;
  int rc=onyx_gmu_rscc_wake(&io,&io,&asleep);
  if(mode==0)assert(rc==0 && !s.writes && !asleep);
  if(mode==1)assert(rc==0 && s.writes==2 && !s.last && !asleep);
  if(mode>=2)assert(rc==(mode==2?ONYX_RSCC_ACK_TIMEOUT:ONYX_RSCC_BUSY_TIMEOUT) && asleep && io.quarantined && s.writes==1 && s.delays==10000);
 }
 puts("RSCC fake-MMIO tests passed");
}
