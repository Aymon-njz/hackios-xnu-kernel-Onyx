/* The return stub uses a private 4 KiB-granule, 39-bit TTBR0 address space.
 * Its geometry must not depend on the user address space active at return. */
#ifndef LOADER_RETURN_GEOMETRY_H
#define LOADER_RETURN_GEOMETRY_H
#include <stdint.h>
#include <stdbool.h>

static inline bool
loader_return_geometry(uint64_t saved_tcr, uint64_t stub, uint64_t *return_tcr)
{
	if (!return_tcr || !stub || stub >= (1ULL << 39)) return false;
	/* T0SZ=25 starts the 4 KiB walk at L1. TG0=00 selects 4 KiB, EPD0=0
	 * permits the walk. TTBR1 and its kernel mappings are untouched. */
	*return_tcr = (saved_tcr & ~(0x3fULL | (3ULL << 14) | (1ULL << 7))) | 25;
	return true;
}
#endif
