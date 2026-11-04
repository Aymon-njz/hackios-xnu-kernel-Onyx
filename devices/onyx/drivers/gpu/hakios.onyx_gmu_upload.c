#include "hakios.onyx_gmu_upload.h"
#define CM3_RESET 0x7e000u
static uint32_t word(const uint8_t *p)
{
 return (uint32_t)p[0] | (uint32_t)p[1]<<8 |
     (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
int onyx_gmu_upload_tcm(struct onyx_rsc_io *io, const void *image, size_t bytes,
 struct onyx_gen8_fw_block *scratch, size_t capacity,
 struct onyx_gmu_upload_report *report)
{
 size_t count=0;
 if (!report) return ONYX_GMU_UPLOAD_ARGUMENT;
 *report=(struct onyx_gmu_upload_report){0};
 if (!io || !io->read || !io->write || !io->owned || io->quarantined)
  return ONYX_GMU_UPLOAD_ARGUMENT;
 /* Parse every record before any hardware access. */
 if (onyx_gen8_fw_plan(image,bytes,scratch,capacity,&count)!=ONYX_GEN8_FW_OK)
  return ONYX_GMU_UPLOAD_IMAGE;
 if (io->read(io->context,CM3_RESET)!=1) return ONYX_GMU_UPLOAD_RESET;
 for (size_t n=0;n<count;n++) {
  const struct onyx_gen8_fw_block *b=&scratch[n];
  if (!b->payload || (b->region!=ONYX_GEN8_ITCM && b->region!=ONYX_GEN8_DTCM))
   continue;
  /* Stock GEN8_GMU_CM3_{I,D}TCM_START are word offsets 0x1b400/0x1c400. */
  uint32_t offset=b->region==ONYX_GEN8_ITCM ? 0x6d000+b->address :
      0x71000+(b->address-0x10004000);
  for (uint32_t p=0;p<b->length;p+=4) {
   if (io->read(io->context,CM3_RESET)!=1) {
    io->quarantined=1;
    return ONYX_GMU_UPLOAD_RESET;
   }
   uint32_t expected=word(b->payload+p);
   io->write(io->context,offset+p,expected);
   uint32_t observed=io->read(io->context,offset+p);
   if (observed!=expected) {
    report->failed_offset=offset+p;
    report->expected=expected;
    report->observed=observed;
    io->quarantined=1;
    return ONYX_GMU_UPLOAD_READBACK;
   }
   report->words_verified++;
  }
 }
 if (io->read(io->context,CM3_RESET)!=1) {
  io->quarantined=1;
  return ONYX_GMU_UPLOAD_RESET;
 }
 return ONYX_GMU_UPLOAD_OK;
}
