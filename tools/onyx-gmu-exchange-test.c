#include "hakios.onyx_gmu_exchange.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
_Alignas(16) static uint32_t memory[(ONYX_HFI_SHARED_BYTES+3)/4];
static unsigned mode,writes,delays;
static void delay(void *p,unsigned us) {(void)p;assert(us==10);delays++;}
static void event(void *p,const uint32_t *m,size_t n) {(void)p;(void)m;(void)n;assert(0);}
static void doorbell(void *p,uint32_t off,uint32_t v) {
 (void)p;assert(off==0x650 && v==1);writes++;
 if(mode==3)return;
 struct onyx_hfi_table *t=(void *)memory;
 uint32_t *cmd=(void *)((char *)memory+ONYX_HFI_DATA_OFFSET);
 uint32_t *msg=cmd+1024;
 assert((cmd[0]&0xffff)==0x20e && cmd[1]==0);
 msg[0]=14|(3<<8)|(1<<16);
 msg[1]=cmd[0]^(mode==1 ? 1u<<20:0);
 msg[2]=mode==2 ? 7:0;
 t->queue[1].write_index=4;
}
int main(void) {
 for(mode=0;mode<4;mode++) {
  struct onyx_hfi h;
  assert(onyx_hfi_init(&h,memory,sizeof(memory),0x60002000)==0);
  struct onyx_rsc_io io={.write=doorbell,.delay_us=delay,.owned=1};
  struct onyx_gmu_exchange x={.hfi=&h,.io=&io,.event=event,.ready=1,.sequence=12};
  uint32_t result=0; writes=delays=0;
  int rc=onyx_gmu_core_fw_start(&x,&result);
  assert(writes==1 && x.sequence==13);
  if(mode==0)assert(rc==0 && !x.quarantined);
  if(mode==1)assert(rc==ONYX_GMU_EXCHANGE_PROTOCOL && x.quarantined);
  if(mode==2)assert(rc==ONYX_GMU_EXCHANGE_REJECTED && result==7 && x.quarantined);
  if(mode==3)assert(rc==ONYX_GMU_EXCHANGE_TIMEOUT && delays==10000 && x.quarantined);
  x.ready=0;writes=0;
  assert(onyx_gmu_core_fw_start(&x,&result)==ONYX_GMU_EXCHANGE_ARGUMENT && writes==0);
 }
 puts("HFI exchange fake-firmware tests passed");
}
