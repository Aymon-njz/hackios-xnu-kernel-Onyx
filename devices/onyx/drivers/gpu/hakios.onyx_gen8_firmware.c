#include "hakios.onyx_gen8_firmware.h"

static uint32_t
read_le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
	    (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int
region_for(uint32_t address, uint32_t length)
{
	static const struct { uint32_t base, size; } regions[] = {
		{ 0, 0x4000 }, { 0x4000, 0xffc000 },
		{ 0x10004000, 0x4000 }, { 0x60000000, 0x20000000 },
		{ 0xc0000000, 0x20000000 }
	};
	if (!length || (address & 3) || (length & 3)) return -1;
	for (unsigned n = 0; n < sizeof(regions) / sizeof(regions[0]); n++) {
		/* Subtraction avoids wrapping a malicious 32-bit end address. */
		if (address >= regions[n].base && length <= regions[n].size &&
		    address - regions[n].base <= regions[n].size - length) return (int)n;
	}
	return -1;
}

enum onyx_gen8_fw_status
onyx_gen8_fw_plan(const void *image, size_t bytes,
    struct onyx_gen8_fw_block *blocks, size_t capacity, size_t *count)
{
	const uint8_t *data = image;
	size_t offset = 0, needed = 0;
	int have_code = 0;
	if (!count) return ONYX_GEN8_FW_ARGUMENT;
	*count = 0;
	if (!data || !blocks) return ONYX_GEN8_FW_ARGUMENT;
	if (!bytes) return ONYX_GEN8_FW_EMPTY;
	/* Validate the entire stream before publishing anything to a caller
	 * that might later use these entries to program the hardware. */
	while (offset < bytes) {
		uint32_t address, size, type, value;
		if (bytes - offset < 16) return ONYX_GEN8_FW_TRUNCATED;
		address = read_le32(data + offset);
		size = read_le32(data + offset + 4);
		type = read_le32(data + offset + 8);
		value = read_le32(data + offset + 12);
		offset += 16;
		if (size) {
			if (size > bytes - offset) return ONYX_GEN8_FW_TRUNCATED;
			if (region_for(address, size) < 0) return ONYX_GEN8_FW_RANGE;
			have_code = 1;
		} else {
			if (have_code || type < 1 || type > 7) return ONYX_GEN8_FW_METADATA;
			if ((type == 1 || type == 7) && region_for(address, value) < 0)
				return ONYX_GEN8_FW_RANGE;
		}
		if (needed == capacity) return ONYX_GEN8_FW_CAPACITY;
		needed++;
		offset += size;
	}
	if (!have_code) return ONYX_GEN8_FW_EMPTY;
	/* TCM is fixed hardware memory. Every other code range must have
	 * reservation metadata that a later allocator can actually back. */
	for (offset = 0; offset < bytes;) {
		uint32_t address = read_le32(data + offset);
		uint32_t size = read_le32(data + offset + 4);
		int region = size ? region_for(address, size) : -1;
		if (size && region != ONYX_GEN8_ITCM && region != ONYX_GEN8_DTCM) {
			int backed = 0;
			for (size_t meta = 0; meta < bytes; meta += 16) {
				uint32_t base = read_le32(data + meta);
				uint32_t type = read_le32(data + meta + 8);
				uint32_t length = read_le32(data + meta + 12);
				if (read_le32(data + meta + 4)) break;
				if ((type == 1 || type == 7) && address >= base &&
				    size <= length && address - base <= length - size) {
					backed = 1;
					break;
				}
			}
			if (!backed) return ONYX_GEN8_FW_BACKING;
		}
		offset += 16 + size;
	}
	/* All bounds are now established. No cast of unaligned wire data. */
	offset = 0;
	for (size_t n = 0; n < needed; n++) {
		struct onyx_gen8_fw_block *b = &blocks[n];
		b->address = read_le32(data + offset);
		b->length = read_le32(data + offset + 4);
		b->type = read_le32(data + offset + 8);
		b->value = read_le32(data + offset + 12);
		offset += 16;
		b->payload = b->length ? data + offset : NULL;
		int region = region_for(b->address, b->length ? b->length : b->value);
		/* A version record has no address range; its region is unused. */
		b->region = region < 0 ? ONYX_GEN8_ITCM : (enum onyx_gen8_region)region;
		offset += b->length;
	}
	*count = needed;
	return ONYX_GEN8_FW_OK;
}
