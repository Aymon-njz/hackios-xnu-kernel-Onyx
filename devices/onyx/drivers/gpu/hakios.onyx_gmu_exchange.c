#include "hakios.onyx_gmu_exchange.h"
int onyx_gmu_core_fw_start(struct onyx_gmu_exchange *x, uint32_t *result)
{
 if (!x || !result || !x->ready || x->quarantined || !x->hfi || !x->event ||
     !x->io || !x->io->write || !x->io->delay_us || !x->io->owned ||
     x->io->quarantined || x->sequence>4095) return ONYX_GMU_EXCHANGE_ARGUMENT;
 *result=0;
 uint32_t command[2]={14u | (2u<<8) | (x->sequence<<20),0};
 int status=onyx_hfi_send(x->hfi,command,2);
 if (status!=ONYX_HFI_OK) return ONYX_GMU_EXCHANGE_QUEUE;
 x->sequence=(x->sequence+1)&4095;
 /* Queue publication barrier is provided by onyx_hfi_send. */
 x->io->write(x->io->context,0x650,1);
 for(unsigned n=0;n<10000;n++) {
  uint32_t msg[255]; size_t words=0;
  status=onyx_hfi_receive(x->hfi,1,msg,255,&words);
  if(status==ONYX_HFI_OK) {
   /* Bit19 is the optional recording flag, excluded from ACK identity. */
   if(((msg[0]>>16)&3)==1) {
    if(words<3 || (msg[1]&~(1u<<19))!=command[0]) {
     x->quarantined=1; return ONYX_GMU_EXCHANGE_PROTOCOL;
    }
    *result=msg[2];
    if(*result) { x->quarantined=1; return ONYX_GMU_EXCHANGE_REJECTED; }
    return ONYX_GMU_EXCHANGE_OK;
   }
   x->event(x->context,msg,words);
  } else if(status!=ONYX_HFI_EMPTY && status!=ONYX_HFI_INCOMPLETE) {
   x->quarantined=1; return ONYX_GMU_EXCHANGE_QUEUE;
  }
  x->io->delay_us(x->io->context,10);
 }
 /* Never retry an uncertain command against a still-running firmware. */
 x->quarantined=1;
 return ONYX_GMU_EXCHANGE_TIMEOUT;
}
