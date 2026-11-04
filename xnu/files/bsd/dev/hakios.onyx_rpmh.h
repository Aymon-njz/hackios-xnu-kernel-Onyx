/* Command DB wire definitions: Qualcomm cmd-db.c. No regulator/MMIO writes. */
#ifndef HAKIOS_ONYX_RPMH_H
#define HAKIOS_ONYX_RPMH_H
#include <stdint.h>
#include <stddef.h>
struct onyx_rpmh_resource {
	uint32_t address;
	uint16_t levels[16];
	unsigned count;
};
/* 0 success, 1 absent, 2 malformed. Output unchanged on failure. */
int onyx_rpmh_lookup(const void *database, size_t bytes, const char *name,
    struct onyx_rpmh_resource *resource);
/* Select the lowest supported ARC level >= the request. Never lowers a vote
 * below the supplied existing level. This constructs a vote, not a write. */
int onyx_rpmh_arc_vote(const struct onyx_rpmh_resource *resource,
    unsigned requested, unsigned existing, uint32_t *address, uint32_t *index);

struct onyx_rsc_layout {
	uint32_t control, status, enable, wait, message, address, data, command_status, stride;
};
/* Reject unknown versions rather than interpreting their registers as v3. */
int onyx_rsc_layout(uint32_t version, struct onyx_rsc_layout *layout);

/* Hardware transport adapter. Caller must hold exclusive ownership of APPS
 * DRV2, serialize calls, supply device barriers in read/write, and keep an
 * existing vote floor. An idle slot alone is NOT an ownership grant.
 * Offsets passed to callbacks are relative to the 64 KiB DRV2 window.
 * This interface is not wired to MMIO by the inventory-only kernel entry. */
struct onyx_rsc_io {
	void *context;
	uint32_t (*read)(void *, uint32_t);
	void (*write)(void *, uint32_t, uint32_t);
	void (*delay_us)(void *, unsigned);
	unsigned owned; /* set only after firmware/PM ownership is established */
	unsigned quarantined; /* timeout or uncertain writes: no further requests */
};
enum onyx_rsc_result {
	ONYX_RSC_OK, ONYX_RSC_INVALID, ONYX_RSC_NOT_OWNED, ONYX_RSC_BUSY,
	ONYX_RSC_READBACK, ONYX_RSC_TIMEOUT
};
enum onyx_rsc_result onyx_rsc_vote(struct onyx_rsc_io *io, uint32_t version,
    unsigned slot, const struct onyx_rpmh_resource *resource,
    unsigned requested, unsigned existing);
/* Active BCM write using command-DB validated address and encoded units.
 * existing_x/y must be known vote floors, not guessed from TCS remnants.
 * Caller aggregates all shared path users and retains the votes until reset.
 * This is the transport, not the Hz/bandwidth aggregation or route discovery.
 */
enum onyx_rsc_result onyx_rsc_bcm_vote(struct onyx_rsc_io *, uint32_t version,
 unsigned slot, uint32_t address, unsigned x, unsigned y,
 unsigned existing_x, unsigned existing_y);
#endif
