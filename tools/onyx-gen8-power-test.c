#include <assert.h>
#include <stdio.h>
#include "hakios.onyx_gen8_power.h"
int main(void)
{
	/* Measured live on Onyx, verified journal native-41443545834. */
	assert(onyx_gen8_power_missing(0x22f801, 0x80002001, 0x80000001,
	    0x80000001, 0x80000000) == 63);
	/* Isolate each condition; votes don't override the hardware-off bit. */
	assert(onyx_gen8_power_missing(0x80000000, 1, 1, 1, 0) == 0);
	assert(onyx_gen8_power_missing(0, 1, 1, 1, 0) == ONYX_CX_OFF);
	assert(onyx_gen8_power_missing(0x80000001, 1, 1, 1, 0) == ONYX_CX_COLLAPSED);
	assert(onyx_gen8_power_missing(0x80000000, 0x80000001, 1, 1, 0) == ONYX_GMU_CLOCK_OFF);
	assert(onyx_gen8_power_missing(0x80000000, 1, 0x80000001, 1, 0) == ONYX_CXO_CLOCK_OFF);
	assert(onyx_gen8_power_missing(0x80000000, 1, 1, 0x80000001, 0) == ONYX_HUB_CLOCK_OFF);
	assert(onyx_gen8_power_missing(0x80000000, 1, 1, 1, 0x80000000) == ONYX_GMU_RCG_OFF);
	puts("Gen8 power status: measured data and six independent gates passed");
}
