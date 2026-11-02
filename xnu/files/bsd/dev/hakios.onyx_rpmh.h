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
#endif
