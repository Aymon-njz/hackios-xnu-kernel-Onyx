#include "hakios.onyx_qtb.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct {uint64_t reg[0x460/8];unsigned mode,writes,waits;} f;
static uint64_t read_reg(void *p,uint32_t off) {
 (void)p;assert(!(off&7) && off<0x460);
 if(off==0x418)return f.mode==1?0:1;
 if(off==0x450)return f.mode==2?0:1;
 if(off==0x458)return f.mode==3?1:0x12345000;
 return f.reg[off/8];
}
static void write_reg(void *p,uint32_t off,uint64_t value) {
 (void)p;assert(!(off&7) && off<0x460);f.reg[off/8]=value;f.writes++;
}
static void delay(void *p,unsigned us) {(void)p;assert(us==1);f.waits++;}
int main(void) {
 for(unsigned mode=0;mode<4;mode++) {
  memset(&f,0,sizeof(f));f.mode=mode;
  struct onyx_qtb_io io={.read=read_reg,.write=write_reg,.delay_us=delay,.ready=1};
  struct onyx_qtb_result out;
  int rc=onyx_qtb_translate(&io,0x60002000,0,0,&out);
  assert(f.reg[0x410/8]==0 && f.reg[0x448/8]==0);
  if(mode==0)assert(rc==0 && out.completed && out.raw_output==0x12345000 && out.address_field==0x12345 && !io.quarantined);
  if(mode==1)assert(rc==ONYX_QTB_HALT_TIMEOUT && f.writes==2 && f.waits==100);
  if(mode==2)assert(rc==ONYX_QTB_TIMEOUT && !out.completed && f.waits==100);
  if(mode==3)assert(rc==ONYX_QTB_FAULT && !out.completed);
  if(mode)assert(io.quarantined);
 }
 memset(&f,0,sizeof(f));struct onyx_qtb_io io={.read=read_reg,.write=write_reg,.delay_us=delay};
 struct onyx_qtb_result out;
 assert(onyx_qtb_translate(&io,0,0,0,&out)==ONYX_QTB_ARGUMENT && !f.writes);
 io.ready=1;f.reg[0x410/8]=1;
 assert(onyx_qtb_translate(&io,0,0,0,&out)==ONYX_QTB_BUSY && !f.writes);
 puts("QTB500 translation transport fake-MMIO tests passed");
}
