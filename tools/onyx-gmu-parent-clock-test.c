#include "hakios.onyx_gen8_power.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct {uint32_t r[0x72000/4];unsigned mode,writes;} f;
static uint32_t rd(void *p,uint32_t off){(void)p;assert(off<0x72000);return f.r[off/4];}
static void wr(void *p,uint32_t off,uint32_t v){
 (void)p;assert(off==0x71150);f.writes++;
 if(f.mode==2)return;
 f.r[off/4]=v;
 if(f.mode!=3)f.r[off/4]&=~0x80000000u;
 if(f.mode==4)f.r[0]&=~0x80000000u;
}
static void delay(void *p,unsigned us){(void)p;assert(us==10);}
int main(void){
 for(unsigned mode=0;mode<5;mode++){
  memset(&f,0,sizeof(f));f.mode=mode;
  f.r[0]=0x80000005;f.r[1]=1;f.r[0x10/4]=31;f.r[0x14/4]=0x4000;
  f.r[0x71150/4]=0x80000000;
  if(mode==1)f.r[0x10/4]=32;
  struct onyx_rsc_io io={.read=rd,.write=wr,.delay_us=delay,.owned=1};uint64_t rate=0;
  int rc=onyx_gen8_gpll0_parent(&io,&rate);
  if(mode==0)assert(rc==0 && rate==600000000 && f.writes==1);
  if(mode==1)assert(rc==ONYX_CLOCK_ARGUMENT && !f.writes);
  if(mode>=2)assert(rc==(mode==3?ONYX_CLOCK_TIMEOUT:ONYX_CLOCK_READBACK) && io.quarantined && !(f.r[0x71150/4]&1));
 }
 puts("Live GPLL0 verification and GPU gate fake-MMIO tests passed");
}
