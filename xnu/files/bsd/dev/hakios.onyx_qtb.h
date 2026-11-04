#ifndef ONYX_QTB_H
#define ONYX_QTB_H
#include <stdint.h>
/* QTB500 64-bit register access relative to physical 0x03de8000.
 * Caller serializes all ECATS use, powers both QTB and SMMU, establishes
 * domain ownership, holds CM3 in reset, disables context fault IRQ/stall and
 * clears pending context faults. These are not inferred from idle registers.
 */
struct onyx_qtb_io {
 void *context;
 uint64_t (*read)(void *,uint32_t);
 void (*write)(void *,uint32_t,uint64_t);
 void (*delay_us)(void *,unsigned);
 unsigned ready, quarantined;
};
struct onyx_qtb_result { uint64_t raw_output, address_field; unsigned completed; };
enum onyx_qtb_status { ONYX_QTB_OK, ONYX_QTB_ARGUMENT, ONYX_QTB_BUSY,
 ONYX_QTB_HALT_TIMEOUT, ONYX_QTB_TIMEOUT, ONYX_QTB_FAULT };
/* Privileged nonsecure SID5 read/instruction/write translation probe.
 * Returns raw OUTFLD0 and stock FIELD_GET(PA), not a software translation.
 * Address-field interpretation must be checked on hardware before claiming
 * it matches a CPU physical address. No memory payload is read or written.
 */
int onyx_qtb_translate(struct onyx_qtb_io *,uint32_t va,unsigned instruction,
 unsigned write,struct onyx_qtb_result *);
#endif
