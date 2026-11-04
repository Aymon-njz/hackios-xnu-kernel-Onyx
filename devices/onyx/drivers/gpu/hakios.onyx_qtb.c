#include "hakios.onyx_qtb.h"
int onyx_qtb_translate(struct onyx_qtb_io *io,uint32_t va,unsigned instruction,
 unsigned write,struct onyx_qtb_result *out)
{
 if(!io || !io->read || !io->write || !io->delay_us || !io->ready ||
     io->quarantined || !out || instruction>1 || write>1 || (instruction && write))
  return ONYX_QTB_ARGUMENT;
 *out=(struct onyx_qtb_result){0};
 uint64_t fence=io->read(io->context,0x410);
 if((fence&1) || (io->read(io->context,0x448)&1)) return ONYX_QTB_BUSY;
 io->write(io->context,0x410,fence|1);
 int result=ONYX_QTB_HALT_TIMEOUT;
 unsigned halted=0;
 for(unsigned n=0;n<100;n++) {
  if(io->read(io->context,0x418)&1) { halted=1;break; }
  io->delay_us(io->context,1);
 }
 if(!halted) goto restore;
 io->write(io->context,0x430,5);
 /* PNU=privileged, TR_TYPE=shared, NON_SEC; no TLB allocation. */
 io->write(io->context,0x438,(1u<<13)|(instruction<<12)|(4u<<8)|(1u<<3)|write);
 io->write(io->context,0x440,va);
 io->write(io->context,0x448,1);
 result=ONYX_QTB_TIMEOUT;
 for(unsigned n=0;n<100;n++) {
  unsigned done=(unsigned)(io->read(io->context,0x450)&1);
  uint64_t raw=io->read(io->context,0x458);
  out->raw_output=raw;
  if(raw&1) { result=ONYX_QTB_FAULT;break; }
  if(done) {
   out->completed=1;
   out->address_field=raw>>12;
   result=ONYX_QTB_OK;
   break;
  }
  io->delay_us(io->context,1);
 }
 /* Vendor sequence resets ECATS trigger before resuming the QTB. */
 io->write(io->context,0x448,0);
restore:
 io->write(io->context,0x410,fence);
 (void)io->read(io->context,0);
 if(result!=ONYX_QTB_OK) io->quarantined=1;
 return result;
}
