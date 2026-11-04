#include "hakios.onyx_gmu_start.h"
static int wait_status(struct onyx_rsc_io *io,uint32_t at,uint32_t mask,uint32_t value)
{
 for(unsigned n=0;n<10000;n++) {
  if((io->read(io->context,at)&mask)==value) return 1;
  io->delay_us(io->context,10);
 }
 return 0;
}
int onyx_gmu_start(struct onyx_rsc_io *io,const struct onyx_gmu_start_ready *r)
{
 if(!io || !io->read || !io->write || !io->delay_us || !io->owned || io->quarantined ||
    !r || !r->domain || !r->domain->attached || r->domain->quarantined ||
    !r->translation_verified || !r->firmware_uploaded || !r->secure_cx_initialized ||
    !r->registers_configured || !r->operating_clocks_verified || !r->rscc_initialized ||
    !r->bandwidth_ready) return ONYX_GMU_START_PREREQUISITE;
 if(io->read(io->context,0)!=1) return ONYX_GMU_START_RESET;
 /* adreno_gen8_gmu.c: clean result, deassert reset, FW_INIT[8:0]=BIT8. */
 io->write(io->context,0x70,0);
 io->write(io->context,0,0);
 int result=ONYX_GMU_START_FW_TIMEOUT;
 if(!wait_status(io,0x70,0x1ff,0x100)) goto fail;
 /* gen8_gmu_hfi_start: CTRL_INIT -> CTRL_STATUS[0]. This is hardware
  * HFI initialization, not receipt of a CORE_FW_START command ACK. */
 io->write(io->context,0x618,1);
 result=ONYX_GMU_START_HFI_TIMEOUT;
 if(!wait_status(io,0x600,1,1)) goto fail;
 return ONYX_GMU_START_OK;
fail:
 io->write(io->context,0,1);
 io->quarantined=1;
 return result;
}
