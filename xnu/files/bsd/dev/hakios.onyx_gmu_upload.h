#ifndef ONYX_GMU_UPLOAD_H
#define ONYX_GMU_UPLOAD_H
#include "hakios.onyx_gen8_firmware.h"
#include "hakios.onyx_rpmh.h"
/* I/O relative to GPU base 0x03d00000, mapped through offset 0x7e000.
 * Caller owns CX, establishes clocks, and serializes reset/TCM access.
 * MMIO callbacks must provide device barriers. Image remains immutable.
 * Only uploads ITCM/DTCM; external firmware must separately be published.
 * Never releases reset. Success does not mean firmware has executed. */
enum onyx_gmu_upload_result {
 ONYX_GMU_UPLOAD_OK, ONYX_GMU_UPLOAD_ARGUMENT, ONYX_GMU_UPLOAD_IMAGE,
 ONYX_GMU_UPLOAD_RESET, ONYX_GMU_UPLOAD_READBACK
};
struct onyx_gmu_upload_report {
 uint32_t words_verified, failed_offset, expected, observed;
};
int onyx_gmu_upload_tcm(struct onyx_rsc_io *, const void *, size_t,
 struct onyx_gen8_fw_block *, size_t, struct onyx_gmu_upload_report *);
#endif
