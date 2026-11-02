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
#endif
