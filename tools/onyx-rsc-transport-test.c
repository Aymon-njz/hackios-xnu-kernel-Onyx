#include "hakios.onyx_rpmh.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
struct fake { uint32_t r[0x10000 / 4]; unsigned writes, delays, complete, reject; };
static uint32_t read_reg(void *p, uint32_t offset) { return ((struct fake *)p)->r[offset / 4]; }
static void write_reg(void *p, uint32_t offset, uint32_t value)
{
	struct fake *f = p; f->writes++;
	assert(offset >= 0xd00 && offset < 0x10000);
	/* Slot 0 occupies 0xd00..0xf9f, apart from shared IRQ clear. */
	assert(offset >= 0xfa0 || offset == 0xd08);
	if (f->reject == offset) return;
	if (offset == 0xd08) f->r[0xd04 / 4] &= ~value;
	else f->r[offset / 4] = value;
	if (offset == 0xfc4 && value == 0x1010000 && f->complete) f->r[0xd04 / 4] |= 2;
}
static void pause_us(void *p, unsigned us) { assert(us == 10); ((struct fake *)p)->delays++; }
static void reset(struct fake *f)
{
	memset(f, 0, sizeof(*f)); f->complete = 1;
	f->r[0xfc8 / 4] = 1; /* slot 1 status */
	f->r[0xfc4 / 4] = 0x10000;
	f->r[0xfd4 / 4] = 0x42; f->r[0xfd8 / 4] = 0x43; f->r[0xfdc / 4] = 0x44;
}
int main(void)
{
	static struct fake f;
	struct onyx_rpmh_resource rail = { .address = 0x30000, .levels = { 0, 0x10, 0x180 }, .count = 3 };
	struct onyx_rsc_io io = { &f, read_reg, write_reg, pause_us, 1, 0 };
	reset(&f);
	assert(onyx_rsc_vote(&io, 0x30800, 1, &rail, 0x10, 0x180) == ONYX_RSC_OK);
	assert(f.r[0xfd4 / 4] == 0x42 && f.r[0xfd8 / 4] == 0x43 && f.r[0xfdc / 4] == 0x44);
	assert(f.r[0xfc4 / 4] == 0x10000 && !f.r[0xfcc / 4] && !f.r[0xd04 / 4]);
	reset(&f); io.owned = 0;
	assert(onyx_rsc_vote(&io, 0x30800, 1, &rail, 0x10, 0) == ONYX_RSC_NOT_OWNED && !f.writes);
	io.owned = 1; f.r[0xd2c / 4] = 1; /* firmware slot 0 */
	assert(onyx_rsc_vote(&io, 0x30800, 1, &rail, 0x10, 0) == ONYX_RSC_BUSY && !f.writes);
	reset(&f); f.r[4 / 4] = 1u << 24;
	assert(onyx_rsc_vote(&io, 0x30800, 1, &rail, 0x10, 0) == ONYX_RSC_NOT_OWNED && !f.writes);
	reset(&f); f.complete = 0;
	assert(onyx_rsc_vote(&io, 0x30800, 1, &rail, 0x10, 0) == ONYX_RSC_TIMEOUT);
	assert(io.quarantined && f.delays == 1000);
	unsigned writes = f.writes;
	assert(onyx_rsc_vote(&io, 0x30800, 1, &rail, 0x10, 0) == ONYX_RSC_NOT_OWNED && writes == f.writes);
	reset(&f); io.quarantined = 0; f.reject = 0xfd4;
	assert(onyx_rsc_vote(&io, 0x30800, 1, &rail, 0x10, 0) == ONYX_RSC_READBACK && io.quarantined);
	reset(&f); io.quarantined = 0;
	assert(onyx_rsc_vote(&io, 0x30800, 0, &rail, 0x10, 0) == ONYX_RSC_INVALID && !f.writes);
	puts("RSC transport: completion/restoration, ownership, occupied slot, solver, timeout quarantine and rejected writes passed");
}
