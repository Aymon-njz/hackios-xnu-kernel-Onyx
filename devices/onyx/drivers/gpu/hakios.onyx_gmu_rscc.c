#include "hakios.onyx_gmu_rscc.h"
static int valid(struct onyx_rsc_io *io)
{
 return io && io->owned && !io->quarantined && io->read && io->write && io->delay_us;
}
static int poll(struct onyx_rsc_io *io,uint32_t offset,uint32_t mask,uint32_t expected)
{
 for(unsigned n=0;n<10000;n++) {
  if((io->read(io->context,offset)&mask)==expected) return 1;
  io->delay_us(io->context,10);
 }
 return 0;
}
int onyx_gmu_rscc_wake(struct onyx_rsc_io *gpu,struct onyx_rsc_io *rscc,unsigned *sleep_done)
{
 if(!valid(gpu) || !valid(rscc) || !sleep_done || *sleep_done>1) return ONYX_RSCC_ARGUMENT;
 if(!*sleep_done) return ONYX_RSCC_OK;
 /* Stock register constants are word offsets; transport takes bytes. */
 gpu->write(gpu->context,0x23b07*4,2);
 int result=ONYX_RSCC_ACK_TIMEOUT;
 if(!poll(gpu,0x23b08*4,2,2)) goto fail;
 result=ONYX_RSCC_BUSY_TIMEOUT;
 if(!poll(rscc,0x101*4,0xffffffffu,0)) goto fail;
 gpu->write(gpu->context,0x23b07*4,0);
 *sleep_done=0;
 return ONYX_RSCC_OK;
fail:
 /* Retain the failed request/state as stock does. Do not issue more writes
  * to a sequencer whose transition has not completed. */
 gpu->quarantined=rscc->quarantined=1;
 return result;
}
