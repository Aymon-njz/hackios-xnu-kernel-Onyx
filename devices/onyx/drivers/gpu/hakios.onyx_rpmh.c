#include "hakios.onyx_rpmh.h"
static uint16_t u16(const uint8_t *p) { return p[0] | (uint16_t)p[1] << 8; }
static uint32_t u32(const uint8_t *p) { return u16(p) | (uint32_t)u16(p + 2) << 16; }
int onyx_rpmh_arc_vote(const struct onyx_rpmh_resource *r, unsigned requested,
    unsigned existing, uint32_t *address, uint32_t *index)
{
	if (!r || !address || !index || !r->count || r->count > 16 ||
	    ((r->address >> 16) & 7) != 3 || (r->address & 3)) return 2;
	unsigned wanted = requested > existing ? requested : existing;
	for (unsigned n = 0; n < r->count; n++) {
		if (n && r->levels[n] <= r->levels[n - 1]) return 2;
	}
	for (unsigned n = 0; n < r->count; n++) if (r->levels[n] >= wanted) {
		*address = r->address; *index = n; return 0;
	}
	return 1;
}

int onyx_rsc_layout(uint32_t version, struct onyx_rsc_layout *layout)
{
	if (!layout) return 2;
	unsigned major = (version >> 16) & 255, minor = (version >> 8) & 255;
	if (major == 3) {
		*layout = (struct onyx_rsc_layout){ 0x24, 0x28, 0x2c, 0x20, 0x34, 0x38, 0x3c, 0x40, 24 };
		return 0;
	}
	if (major == 2 && minor == 7) {
		*layout = (struct onyx_rsc_layout){ 0x14, 0x18, 0x1c, 0x10, 0x30, 0x34, 0x38, 0x3c, 20 };
		return 0;
	}
	return 1;
}

int onyx_rpmh_lookup(const void *database, size_t bytes, const char *name,
    struct onyx_rpmh_resource *resource)
{
	const uint8_t *b = database;
	uint8_t key[8] = { 0 };
	unsigned length = 0, found = 0;
	struct onyx_rpmh_resource result = { 0 };
	if (!b || !name || !resource || bytes < 144) return 2;
	while (name[length] && length < 8) { key[length] = name[length]; length++; }
	if (!length || (length == 8 && name[length])) return 2;
	if (b[4] != 0xdb || b[5] != 0x30 || b[6] != 3 || b[7] != 0x0c) return 2;
	size_t available = bytes - 144;
	for (unsigned slave = 0; slave < 8; slave++) {
		const uint8_t *s = b + 8 + slave * 16;
		if (!u16(s)) continue;
		uint32_t headers = u16(s + 2), data = u16(s + 4), count = u16(s + 6);
		if (headers > available || count > (available - headers) / 24 || data > available) return 2;
		for (unsigned n = 0; n < count; n++) {
			const uint8_t *entry = b + 144 + headers + n * 24;
			uint32_t size = u16(entry + 20), offset = u16(entry + 22);
			if (offset > available - data || size > available - data - offset) return 2;
			unsigned match = 1;
			for (unsigned i = 0; i < 8; i++) if (entry[i] != key[i]) match = 0;
			if (!match) continue;
			if (found++ || size & 1 || size > 32 || !u32(entry + 16)) return 2;
			result.address = u32(entry + 16);
			const uint8_t *levels = b + 144 + data + offset;
			for (unsigned i = 0; i < size / 2; i++) {
				uint16_t level = u16(levels + i * 2);
				if (i && !level) break;
				if (i && level <= result.levels[i - 1]) return 2;
				result.levels[result.count++] = level;
			}
		}
	}
	if (!found) return 1;
	*resource = result;
	return 0;
}
