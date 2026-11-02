#include <assert.h>
#include <stdio.h>
#include "loader_return_geometry.h"

int main(void)
{
	for (unsigned size = 0; size < 64; size++) for (unsigned granule = 0; granule < 4; granule++) {
		uint64_t input = 0x123456789abcdef0ULL;
		input = (input & ~(0x3fULL | (3ULL << 14))) | size | (uint64_t)granule << 14;
		uint64_t output = 0;
		assert(loader_return_geometry(input, 0xa1000000, &output));
		assert((output & 0x3f) == 25);
		assert(!(output & ((3ULL << 14) | (1ULL << 7))));
		uint64_t changed = 0x3fULL | (3ULL << 14) | (1ULL << 7);
		assert((output & ~changed) == (input & ~changed));
	}
	uint64_t output = 0xdead;
	assert(!loader_return_geometry(0, 0, &output) && output == 0xdead);
	assert(!loader_return_geometry(0, 1ULL << 39, &output) && output == 0xdead);
	assert(!loader_return_geometry(0, 0xa1000000, 0));
	assert(loader_return_geometry(0, (1ULL << 39) - 64, &output));
	assert((((1ULL << 39) - 64) >> 30) == 511);
	puts("return geometry: 256 TCR combinations, bounds and TTBR1 preservation passed");
}
