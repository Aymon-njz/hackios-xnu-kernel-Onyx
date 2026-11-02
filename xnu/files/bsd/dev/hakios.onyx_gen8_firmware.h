/* Gen8 GMU firmware staging. No MMIO, allocations, or firmware writes here.
 * Wire format and virtual ranges: SM8735 KGSL kgsl_gmu_core.h and
 * adreno_gen8_gmu.c, revision 8500748dae32f10507c02f1c94bc971553cb525b.
 * Firmware data remains owned by the caller for the lifetime of the plan. */
#ifndef ONYX_GEN8_FIRMWARE_H
#define ONYX_GEN8_FIRMWARE_H
#include <stddef.h>
#include <stdint.h>

enum onyx_gen8_region {
	ONYX_GEN8_ITCM, ONYX_GEN8_CACHE, ONYX_GEN8_DTCM,
	ONYX_GEN8_UNCACHED, ONYX_GEN8_UNCACHED_EXTENDED
};
enum onyx_gen8_fw_status {
	ONYX_GEN8_FW_OK, ONYX_GEN8_FW_ARGUMENT, ONYX_GEN8_FW_TRUNCATED,
	ONYX_GEN8_FW_RANGE, ONYX_GEN8_FW_METADATA, ONYX_GEN8_FW_CAPACITY,
	ONYX_GEN8_FW_EMPTY, ONYX_GEN8_FW_BACKING
};
struct onyx_gen8_fw_block {
	uint32_t address, length, type, value;
	enum onyx_gen8_region region;
	/* NULL for reservation/version metadata; otherwise points into the image. */
	const uint8_t *payload;
};

/* On failure *count is zero and no entries are published. Metadata must
 * precede code. Reservation records use value as their length, not size.
 * Successful parsing does not establish firmware authenticity or ownership. */
enum onyx_gen8_fw_status onyx_gen8_fw_plan(const void *image, size_t bytes,
    struct onyx_gen8_fw_block *blocks, size_t capacity, size_t *count);
#endif
