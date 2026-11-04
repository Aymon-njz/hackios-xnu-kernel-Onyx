#include "hakios.onyx_gen8_power.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct {uint32_t reg[0x9800/4];unsigned mode,writes;} f;
static uint32_t rd(void *p,uint32_t off) {(void)p;assert(off<0x9800);return f.reg[off/4];}
static void wr(void *p,uint32_t off,uint32_t v) {
 (void)p;assert(off<0x9800);f.writes++;
 if(f.mode==2 && off==0x931c && (v&0x71f)==0x201)return;
 f.reg[off/4]=v;
 if((off==0x9318 || off==0x93ec) && f.mode!=3)f.reg[off/4]&=~1u;
}
static void delay(void *p,unsigned us) {(void)p;assert(us==10);}
int main(void) {
 for(unsigned mode=0;mode<4;mode++) {
  memset(&f,0,sizeof(f));f.mode=mode;
  f.reg[0x9080/4]=0x80000000;f.reg[0]=0x80000005;f.reg[1]=1;
  f.reg[0x18/4]=0x407;f.reg[0x10/4]=0x34;f.reg[0x14/4]=0x1555;
  f.reg[0x931c/4]=f.reg[0x93f0/4]=1;
  if(mode==1)f.reg[0]=0;
  struct onyx_rsc_io io={.read=rd,.write=wr,.delay_us=delay,.owned=1};
  int rc=onyx_gen8_clocks_operating(&io,1,1);
  if(mode==0)assert(rc==0 && f.reg[0x931c/4]==0x201 && f.reg[0x93f0/4]==0x505);
  if(mode==1)assert(rc==ONYX_CLOCK_ARGUMENT && !f.writes);
  if(mode>=2)assert(rc==(mode==2?ONYX_CLOCK_READBACK:ONYX_CLOCK_TIMEOUT) && io.quarantined && f.reg[0x931c/4]==1 && f.reg[0x93f0/4]==1);
 }
 puts("Operating root switch fake-MMIO tests passed");
}
