#include "hakios.onyx_gmu_start.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t regs[0x40000/4];
struct fake { unsigned writes,reject,stall,boot,fw_fail,hfi_fail; };
static uint32_t rd(void *p,uint32_t at) { struct fake *f=p;assert(at<0x40000 && !(at&3)); if(f->stall && (at&4095)==0x7f4)return 1;return regs[at/4]; }
static void wr(void *p,uint32_t at,uint32_t value) {
 struct fake *f=p;f->writes++;assert(at<0x40000 && !(at&3));if(f->reject==at)return;regs[at/4]=value;
 if(f->boot && at==0 && !value && !f->fw_fail)regs[0x70/4]=0x100;
 if(f->boot && at==0x618 && value && !f->hfi_fail)regs[0x600/4]=1;
}
static void delay(void *p,unsigned us) {(void)p;assert(us==10);}
static _Alignas(4096) uint64_t tables[4*512];
static void reset(struct fake *f) {memset(regs,0,sizeof regs);*f=(struct fake){.reject=~0u};regs[8]=0x4c017e18;regs[9]=0x40000016;regs[10]=0x5511;}
int main(void) {
 struct fake f;reset(&f);struct onyx_rsc_io io={&f,rd,wr,delay,1,0};
 struct onyx_gmu_mmu pt;assert(!onyx_gmu_mmu_init(&pt,tables,0x90000000,4));
 assert(!onyx_gmu_mmu_map(&pt,0x4000,0x91000000,4096,ONYX_GMU_READ));
 struct onyx_gmu_attachment a={0};struct onyx_gmu_lease l={3,7,1,1,1};
 l.handoff_verified=0;assert(onyx_gmu_attach(&io,&l,&pt,&a)==ONYX_ATTACH_ARGUMENT && !f.writes);l.handoff_verified=1;
 regs[(0x800+7*4)/4]=0x80000005;assert(onyx_gmu_attach(&io,&l,&pt,&a)==ONYX_ATTACH_BUSY && !f.writes);reset(&f);
 assert(onyx_gmu_attach(&io,&l,&pt,&a)==0 && a.attached && regs[(0x800+7*4)/4]==0x80000005);
 reset(&f);a=(struct onyx_gmu_attachment){0};f.reject=0x23020;
 assert(onyx_gmu_attach(&io,&l,&pt,&a)==ONYX_ATTACH_READBACK && a.quarantined && !regs[0x23000/4] && !regs[(0x800+7*4)/4]);
 reset(&f);io.quarantined=0;a=(struct onyx_gmu_attachment){0};f.stall=1;
 assert(onyx_gmu_attach(&io,&l,&pt,&a)==ONYX_ATTACH_TIMEOUT && a.quarantined);
 reset(&f);io.quarantined=0;f.boot=1;regs[0]=1;a=(struct onyx_gmu_attachment){.attached=1};
 struct onyx_gmu_start_ready r={&a,1,1,1,1,1,1,1};r.translation_verified=0;
 assert(onyx_gmu_start(&io,&r)==ONYX_GMU_START_PREREQUISITE && !f.writes);r.translation_verified=1;
 assert(onyx_gmu_start(&io,&r)==0);
 reset(&f);f.boot=1;f.fw_fail=1;regs[0]=1;
 assert(onyx_gmu_start(&io,&r)==ONYX_GMU_START_FW_TIMEOUT && regs[0]==1 && io.quarantined);
 reset(&f);io.quarantined=0;f.boot=1;f.hfi_fail=1;regs[0]=1;
 assert(onyx_gmu_start(&io,&r)==ONYX_GMU_START_HFI_TIMEOUT && regs[0]==1 && io.quarantined);
 puts("GMU attach/start transport: ownership/no-write gates, installation, readback rollback, TLB/FW/HFI timeouts passed; fake MMIO only");
}
