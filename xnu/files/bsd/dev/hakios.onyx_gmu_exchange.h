#ifndef ONYX_GMU_EXCHANGE_H
#define ONYX_GMU_EXCHANGE_H
#include "hakios.onyx_gen8_hfi.h"
#include "hakios.onyx_rpmh.h"
/* Serialized HFI v2 exchange after firmware and HFI hardware initialization.
 * MMIO relative to GMUCX control page 0x03d7e000. Caller supplies coherent
 * queues and owns their lifetime. Not safe alongside another queue consumer.
 * Non-ACK messages are delivered synchronously; callback must not reenter.
 */
struct onyx_gmu_exchange {
 struct onyx_hfi *hfi;
 struct onyx_rsc_io *io;
 void *context;
 void (*event)(void *, const uint32_t *, size_t);
 unsigned sequence, ready, quarantined;
};
enum onyx_gmu_exchange_result {
 ONYX_GMU_EXCHANGE_OK, ONYX_GMU_EXCHANGE_ARGUMENT, ONYX_GMU_EXCHANGE_QUEUE,
 ONYX_GMU_EXCHANGE_PROTOCOL, ONYX_GMU_EXCHANGE_REJECTED, ONYX_GMU_EXCHANGE_TIMEOUT
};
int onyx_gmu_core_fw_start(struct onyx_gmu_exchange *, uint32_t *firmware_result);
#endif
