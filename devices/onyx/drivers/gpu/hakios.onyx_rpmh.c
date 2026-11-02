#include "hakios.onyx_rpmh.h"
static uint16_t u16(const uint8_t *p) { return p[0] | (uint16_t)p[1] << 8; }
static uint32_t u32(const uint8_t *p) { return u16(p) | (uint32_t)u16(p + 2) << 16; }

static int write_checked(struct onyx_rsc_io *io, uint32_t offset, uint32_t value)
{
	io->write(io->context, offset, value);
	return io->read(io->context, offset) == value;
}

enum onyx_rsc_result
onyx_rsc_vote(struct onyx_rsc_io *io, uint32_t version, unsigned slot,
    const struct onyx_rpmh_resource *resource, unsigned requested, unsigned existing)
{
	struct onyx_rsc_layout l;
	uint32_t address, index;
	if (!io || !io->read || !io->write || !io->delay_us || slot < 1 || slot > 2 ||
	    onyx_rsc_layout(version, &l) ||
	    onyx_rpmh_arc_vote(resource, requested, existing, &address, &index)) return ONYX_RSC_INVALID;
	if (!io->owned || io->quarantined) return ONYX_RSC_NOT_OWNED;
	uint32_t base = 0xd00 + slot * 0x2a0, bit = 1u << slot;
	/* Refuse solver mode, pending IRQ, enabled commands or an active trigger.
	 * Never repurpose slot 0, or clear anyone else's pending completion. */
	uint32_t solver = io->read(io->context, ((version >> 16) & 255) == 3 ? 4 : 0);
	if (solver & (1u << 24)) return ONYX_RSC_NOT_OWNED;
	uint32_t control = io->read(io->context, base + l.control);
	if (io->read(io->context, 0xd04) & bit ||
	    !(io->read(io->context, base + l.status) & 1) ||
	    io->read(io->context, base + l.enable) || io->read(io->context, base + l.wait) ||
	    (control & ~(1u << 16))) return ONYX_RSC_BUSY;
	/* Firmware may leave completed commands enabled. Preserve them, but
	 * require completion/idle state and reject any request on our resource.
	 * This is not an ownership grant: the caller's exclusive claim is still
	 * required. Never modify another TCS to make this test pass. */
	for (unsigned s = 0; s < 3; s++) if (s != slot) {
		uint32_t other = 0xd00 + s * 0x2a0;
		uint32_t enabled = io->read(io->context, other + l.enable);
		if (!enabled) continue;
		if (!(io->read(io->context, other + l.status) & 1) || (enabled & ~0xffffu))
			return ONYX_RSC_BUSY;
		for (unsigned n = 0; n < 16; n++) if (enabled & (1u << n)) {
			uint32_t at = other + n * l.stride;
			uint32_t pending_address = io->read(io->context, at + l.address);
			uint32_t command_status = io->read(io->context, at + l.command_status);
			if (pending_address == address || (command_status & 0x10100) != 0x10100)
				return ONYX_RSC_BUSY;
		}
		if (io->read(io->context, other + l.enable) != enabled) return ONYX_RSC_BUSY;
	}
	uint32_t old_message = io->read(io->context, base + l.message);
	uint32_t old_address = io->read(io->context, base + l.address);
	uint32_t old_data = io->read(io->context, base + l.data);
	uint32_t old_irq = io->read(io->context, 0xd00);
	/* MSGID: eight-byte request, response required, write. */
	if (!write_checked(io, base + l.control, 0) ||
	    !write_checked(io, base + l.message, 0x10108) ||
	    !write_checked(io, base + l.address, address) ||
	    !write_checked(io, base + l.data, index) ||
	    !write_checked(io, base + l.wait, 1) ||
	    !write_checked(io, base + l.enable, 1) ||
	    !write_checked(io, 0xd00, old_irq | bit) ||
	    !write_checked(io, base + l.control, 1u << 16) ||
	    !write_checked(io, base + l.control, (1u << 16) | (1u << 24))) {
		io->quarantined = 1;
		return ONYX_RSC_READBACK;
	}
	unsigned elapsed;
	for (elapsed = 0; elapsed < 10000; elapsed += 10) {
		if (io->read(io->context, 0xd04) & bit) break;
		io->delay_us(io->context, 10);
	}
	if (elapsed == 10000) {
		/* Do not restore or retry while the device may still be using this
		 * request. Recovery must establish hardware ownership afresh. */
		io->quarantined = 1;
		return ONYX_RSC_TIMEOUT;
	}
	/* Completion observed: release only our slot. IRQ_CLEAR is W1C and is
	 * deliberately not checked as an ordinary read/write register. */
	if (!write_checked(io, base + l.enable, 0) || !write_checked(io, base + l.wait, 0)) {
		io->quarantined = 1; return ONYX_RSC_READBACK;
	}
	io->write(io->context, 0xd08, bit);
	/* Restore only our completion-enable bit. Never clear another slot's
	 * interrupt enable if firmware changed it while our request completed. */
	uint32_t irq = io->read(io->context, 0xd00);
	if (!write_checked(io, 0xd00, (irq & ~bit) | (old_irq & bit))) {
		io->quarantined = 1; return ONYX_RSC_READBACK;
	}
	if (!write_checked(io, base + l.control, 1u << 16) ||
	    !write_checked(io, base + l.control, 0) ||
	    !write_checked(io, base + l.message, old_message) ||
	    !write_checked(io, base + l.address, old_address) ||
	    !write_checked(io, base + l.data, old_data) ||
	    !write_checked(io, base + l.control, control)) {
		io->quarantined = 1; return ONYX_RSC_READBACK;
	}
	return ONYX_RSC_OK;
}

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
