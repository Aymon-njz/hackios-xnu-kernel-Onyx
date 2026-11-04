#ifndef ONYX_GMU_START_H
#define ONYX_GMU_START_H
#include "hakios.onyx_gmu_attach.h"
/* Register I/O relative to physical 0x03d7e000, length >=0x1000.
 * All prerequisites must come from real successful platform operations,
 * never from boot arguments. This function does not provide those operations.
 */
struct onyx_gmu_start_ready {
 const struct onyx_gmu_attachment *domain;
 unsigned translation_verified, firmware_uploaded, secure_cx_initialized;
 unsigned registers_configured, operating_clocks_verified, rscc_initialized, bandwidth_ready;
};
enum onyx_gmu_start_result { ONYX_GMU_START_OK, ONYX_GMU_START_PREREQUISITE,
 ONYX_GMU_START_RESET, ONYX_GMU_START_FW_TIMEOUT, ONYX_GMU_START_HFI_TIMEOUT };
int onyx_gmu_start(struct onyx_rsc_io *, const struct onyx_gmu_start_ready *);
#endif
