#ifndef ONYX_GMU_RSCC_H
#define ONYX_GMU_RSCC_H
#include "hakios.onyx_rpmh.h"
/* GPU-relative I/O includes AO registers through 0x8ec20. RSCC I/O is
 * relative to the device-tree RSCC resource. No address is guessed here.
 * Both windows must be powered and exclusively owned. A cold boot does not
 * run the stock wake sequence; only a completed sleep sets sleep_done.
 */
enum onyx_rscc_result { ONYX_RSCC_OK, ONYX_RSCC_ARGUMENT,
 ONYX_RSCC_ACK_TIMEOUT, ONYX_RSCC_BUSY_TIMEOUT };
int onyx_gmu_rscc_wake(struct onyx_rsc_io *gpu, struct onyx_rsc_io *rscc,
 unsigned *sleep_done);
#endif
