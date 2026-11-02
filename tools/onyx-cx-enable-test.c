#include "hakios.onyx_gen8_power.h"
#include <assert.h>
#include <stdio.h>
struct fake { uint32_t value; unsigned writes, waits, power_on, reject; };
static uint32_t rd(void *p, uint32_t at) { assert(at == 0x9080); return ((struct fake *)p)->value; }
static void wr(void *p, uint32_t at, uint32_t value)
{
	struct fake *f = p; assert(at == 0x9080); f->writes++;
	if (!f->reject) f->value = value;
}
static void wait_us(void *p, unsigned us)
{
	struct fake *f = p; assert(us == 1 || us == 10); f->waits++;
	if (f->power_on && !(f->value & 1)) f->value |= 1u << 31;
}
int main(void)
{
	struct fake f = { .value = 0x22f801, .power_on = 1 };
	struct onyx_rsc_io io = { &f, rd, wr, wait_us, 1, 0 };
	uint32_t old;
	assert(onyx_gen8_cx_enable(&io, 0, &old) == ONYX_CX_SUPPLIES && !f.writes);
	assert(onyx_gen8_cx_enable(&io, 1, &old) == ONYX_CX_OK);
	assert(old == 0x22f801 && f.value == 0x8022f800 && f.writes == 1);
	assert(onyx_gen8_cx_enable(&io, 1, &old) == ONYX_CX_OK && f.writes == 1);
	f = (struct fake){ .value = 0x22f803 };
	assert(onyx_gen8_cx_enable(&io, 1, &old) == ONYX_CX_HW_CONTROL && !f.writes);
	f = (struct fake){ .value = 0x22f801, .reject = 1 };
	assert(onyx_gen8_cx_enable(&io, 1, &old) == ONYX_CX_WRITE_FAILED && io.quarantined);
	io.quarantined = 0; f = (struct fake){ .value = 0x22f801 };
	assert(onyx_gen8_cx_enable(&io, 1, &old) == ONYX_CX_TIMEOUT && io.quarantined);
	assert(f.value == 0x22f801 && f.waits == 201 && f.writes == 2);
	assert(onyx_gen8_cx_enable(&io, 1, &old) == ONYX_CX_ARGUMENT && f.writes == 2);
	puts("CX enable: supply gate, success, already on, HW ownership, rejected write, timeout and rollback checked");
}
