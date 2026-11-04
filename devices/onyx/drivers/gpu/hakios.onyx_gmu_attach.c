#include "hakios.onyx_gmu_attach.h"
#define ROOT 0x20000u
static int put(struct onyx_rsc_io *io,uint32_t at,uint32_t val)
{ io->write(io->context,at,val); return io->read(io->context,at)==val; }
static int sync(struct onyx_rsc_io *io,uint32_t cb)
{
 io->write(io->context,cb+0x7f0,1);
 for(unsigned n=0;n<200;n++) {
  if(!(io->read(io->context,cb+0x7f4)&1)) return 1;
  io->delay_us(io->context,10);
 }
 return 0;
}
int onyx_gmu_attach(struct onyx_rsc_io *io,const struct onyx_gmu_lease *l,
 const struct onyx_gmu_mmu *pt,struct onyx_gmu_attachment *out)
{
 if(!io || !io->read || !io->write || !io->delay_us || !io->owned || io->quarantined ||
    !l || !pt || !pt->tables || pt->used<3 || pt->used>pt->pages || !out || out->attached || out->quarantined ||
    !l->handoff_verified || !l->tables_published || !l->cm3_reset_verified || l->bank>=22 || l->slot>=24 ||
    !pt->physical || (pt->physical&4095) || pt->physical>=(1ULL<<36)) return ONYX_ATTACH_ARGUMENT;
 if(io->read(io->context,0x20)!=0x4c017e18 || io->read(io->context,0x24)!=0x40000016 ||
    io->read(io->context,0x28)!=0x5511) return ONYX_ATTACH_IDENTITY;
 /* Measured IDR2.OAS=1: this SMMU supports 36-bit output addresses.
  * The generic builder permits 40 bits, so enforce the hardware subset. */
 for(size_t n=0;n<pt->used*512;n++)
  if((pt->tables[n]&3) && (pt->tables[n]&0x000000fffffff000ULL)>=(1ULL<<36))
   return ONYX_ATTACH_ARGUMENT;
 uint32_t cb=ROOT+l->bank*4096,smr=0x800+l->slot*4,s2cr=0xc00+l->slot*4;
 if(io->read(io->context,smr)&0x80000000u) return ONYX_ATTACH_BUSY;
 for(unsigned n=0;n<24;n++) {
  uint32_t m=io->read(io->context,0x800+4*n),r=io->read(io->context,0xc00+4*n);
  if(!(m&0x80000000u)) continue;
  if((((5u^m)&~(m>>16)&0x7fff)==0) || (((r>>16)&3)==0 && (r&255)==l->bank)) return ONYX_ATTACH_BUSY;
 }
 /* Only an empty, disabled bank within the independently verified lease. */
 if(io->read(io->context,cb) || io->read(io->context,cb+0x20) ||
    io->read(io->context,cb+0x24) || io->read(io->context,cb+0x28) ||
    io->read(io->context,cb+0x2c)) return ONYX_ATTACH_BUSY;
 uint32_t at[]={0x1000+4*l->bank,0x1800+4*l->bank,cb+0x10,cb+0x30,
                cb+0x20,cb+0x24,cb+0x38,cb+0x3c,s2cr,smr};
 uint32_t old[10]; for(unsigned n=0;n<10;n++) old[n]=io->read(io->context,at[n]);
 uint64_t ttbr=pt->physical|((uint64_t)(l->bank+1)<<48);
 /* AArch64 S1, 32-bit IOVA / 36-bit PA, 4K pages, coherent table walks.
  * Normal NC/WB MAIR slots match the private page-table builder. */
 uint32_t val[]={0x1f300,1,1u|(1u<<4)|(7u<<15),32u|(1u<<8)|(1u<<10)|(3u<<12)|(1u<<23),
                (uint32_t)ttbr,(uint32_t)(ttbr>>32),0xff44,0,l->bank,0x80000005u};
 unsigned changed=0; int result=ONYX_ATTACH_READBACK;
 for(unsigned n=0;n<8;n++) { changed=n+1; if(!put(io,at[n],val[n])) goto fail; }
 io->write(io->context,cb+0x610,l->bank+1);
 if(!sync(io,cb)) { result=ONYX_ATTACH_TIMEOUT; goto fail; }
 /* No stall-on-fault and no fault IRQ until an interrupt handler exists. */
 if(!put(io,cb,0x1027)) goto fail;
 for(unsigned n=8;n<10;n++) { changed=n+1; if(!put(io,at[n],val[n])) goto fail; }
 *out=(struct onyx_gmu_attachment){1,0,ttbr};
 /* Register installation succeeds; IDR0.ATOSNS means generic hardware
  * translation verification is unavailable. Do not set a verified flag. */
 return ONYX_ATTACH_OK;
fail:
 io->write(io->context,smr,0);
 io->write(io->context,cb,0);
 while(changed) { unsigned n=--changed; io->write(io->context,at[n],old[n]); }
 io->write(io->context,cb+0x610,l->bank+1);
 (void)sync(io,cb);
 io->quarantined=1; out->quarantined=1;
 return result;
}
