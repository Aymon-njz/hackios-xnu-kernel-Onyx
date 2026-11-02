#ifndef HAKIOS_ONYX_JOURNAL_H
#define HAKIOS_ONYX_JOURNAL_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#define ONYX_JOURNAL_BYTES (2u << 20)
#define ONYX_JOURNAL_HEADER 4096u
#define ONYX_USERDATA_BYTES 249517027328ULL
#define ONYX_USERDATA_START_LBA 3124736ULL
#define ONYX_JOURNAL_FIRST_LBA 64041467ULL

static inline bool
onyx_journal_geometry(uint32_t block_size, uint64_t blocks, uint64_t start,
    uint16_t unit, uint8_t firmware_lun, uint8_t native_lun, uint64_t native_offset)
{
	/* Old UEFI device paths can leave LUN unknown (0xff). The native
	 * mapping must still be unit/LUN 0, with the pinned address and size;
	 * a matching journal signature is also read from LUN 0 before writes. */
	return block_size == 4096 && blocks == ONYX_USERDATA_BYTES / 4096 &&
	    start == ONYX_USERDATA_START_LBA && unit == 0 && native_lun == 0 &&
	    (firmware_lun == 0 || firmware_lun == 0xff) && native_offset == start * 4096;
}

static inline bool
onyx_journal_span(uint64_t first, uint64_t lba, uint64_t blocks)
{
	return first == ONYX_JOURNAL_FIRST_LBA && lba >= first && lba - first < 512u &&
	    blocks > 0 && blocks <= 512u - (lba - first);
}

/* A slot may carry exactly its caller's validated journal WRITE(10), with
 * Force Unit Access and no other control flags. Normal slots remain read-only. */
static inline bool
onyx_journal_command(const uint8_t *u, uint32_t dw0, uint32_t lba, uint32_t blocks, unsigned tag)
{
	if (!u || !blocks || blocks > 512 || dw0 != 0x12000000u ||
	    u[0] != 1 || u[1] != 0x20 || u[2] != 0 || u[3] != tag ||
	    u[16] != 0x2a || u[17] != 8) return false;
	for (unsigned n = 4; n < 12; n++) if (u[n]) return false;
	for (unsigned n = 25; n < 32; n++) if (u[n]) return false;
	if (u[22]) return false;
	uint32_t length = (uint32_t)u[12] << 24 | (uint32_t)u[13] << 16 | (uint32_t)u[14] << 8 | u[15];
	uint32_t at = (uint32_t)u[18] << 24 | (uint32_t)u[19] << 16 | (uint32_t)u[20] << 8 | u[21];
	return length == blocks * 4096u && at == lba && (((uint32_t)u[23] << 8) | u[24]) == blocks;
}

static inline uint32_t
onyx_journal_crc(const uint8_t *bytes, size_t length)
{
	uint32_t crc = UINT32_MAX;
	for (size_t n = 0; n < length; n++) {
		crc ^= bytes[n];
		for (unsigned bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ ((0u - (crc & 1)) & 0xedb88320u);
	}
	return ~crc;
}
#endif
