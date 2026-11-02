#include "hakios.onyx_gen8_power.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
struct fake { uint32_t r[0x9800 / 4]; unsigned writes, waits, stuck, reject; uint32_t history[128]; };
static int root(uint32_t at) { return at == 0x9318 || at == 0x93ec; }
static int branch(uint32_t at)
{
	return at == 0x90e4 || at == 0x93e8 || at == 0x90e8 || at == 0x90bc || at == 0x90d4;
}
static int aon(uint32_t at) { return at == 0x93a4 || at == 0x9008 || at == 0x93a8 || at == 0x9004 || at == 0x90cc; }
static uint32_t rd(void *p, uint32_t at) { assert(at < 0x9800 && !(at & 3)); return ((struct fake *)p)->r[at / 4]; }
static void wr(void *p, uint32_t at, uint32_t value)
{
	struct fake *f = p; assert(f->writes < 128); f->history[f->writes++] = at;
	assert(root(at) || root(at - 4) || branch(at) || aon(at));
	if (f->reject == at) return;
	f->r[at / 4] = value;
	if (root(at) && f->stuck != at) f->r[at / 4] = (value & ~0x80000001u) | ((value & 2) ? 0 : 0x80000000u);
	if (branch(at) && f->stuck != at) f->r[at / 4] = (value & ~0x80000000u) | ((value & 1) ? 0 : 0x80000000u);
}
static void pause_us(void *p, unsigned us) { assert(us == 10); ((struct fake *)p)->waits++; }
static void reset(struct fake *f)
{
	memset(f, 0, sizeof(*f));
	f->r[0x9080 / 4] = 0xf822f800;
	f->r[0x9318 / 4] = f->r[0x93ec / 4] = 0x80000000;
	f->r[0x931c / 4] = f->r[0x93f0 / 4] = 0x100000;
	const uint32_t branches[] = { 0x90e4, 0x93e8, 0x90e8, 0x90bc, 0x90d4 };
	for (unsigned n = 0; n < 5; n++) f->r[branches[n] / 4] = 0x80000000;
}
int main(void)
{
	static struct fake f;
	struct onyx_rsc_io io = { &f, rd, wr, pause_us, 1, 0 };
	struct onyx_clock_trace trace;
	reset(&f);
	assert(onyx_gen8_clocks_tcxo(&io, &trace) == ONYX_CLOCK_OK);
	const uint32_t expected_aon[] = { 0x93a4, 0x9008, 0x93a8, 0x9004, 0x90cc };
	for (unsigned n = 0; n < 5; n++) {
		assert(f.history[n] == expected_aon[n]);
		assert(f.r[expected_aon[n] / 4] & 1);
	}
	assert(f.r[0x931c / 4] == 0x100001 && f.r[0x93f0 / 4] == 0x100001);
	assert((f.r[0x9318 / 4] & 0x80000003u) == 2);
	assert(f.r[0x90d4 / 4] == 1 && f.r[0x90e8 / 4] == 1);
	reset(&f); f.r[0x9080 / 4] = 0x22f801;
	assert(onyx_gen8_clocks_tcxo(&io, &trace) == ONYX_CLOCK_CX_OFF && !f.writes);
	reset(&f); io.owned = 0;
	assert(onyx_gen8_clocks_tcxo(&io, &trace) == ONYX_CLOCK_ARGUMENT && !f.writes);
	io.owned = 1; f.r[0x9318 / 4] |= 1;
	assert(onyx_gen8_clocks_tcxo(&io, &trace) == ONYX_CLOCK_ARGUMENT && !f.writes);
	reset(&f); f.reject = 0x93f0;
	assert(onyx_gen8_clocks_tcxo(&io, &trace) == ONYX_CLOCK_READBACK && io.quarantined);
	assert(f.r[0x93f0 / 4] == 0x100000);
	assert(trace.phase == 1 && trace.offset == 0x93f0 && trace.mask == 0x71f &&
	    trace.expected == 1 && trace.observed == 0x100000);
	reset(&f); io.quarantined = 0; f.stuck = 0x9318;
	assert(onyx_gen8_clocks_tcxo(&io, &trace) == ONYX_CLOCK_TIMEOUT && io.quarantined);
	assert(f.r[0x931c / 4] == 0x100000 && f.r[0x93f0 / 4] == 0x100000);
	assert(trace.phase == 2 && trace.offset == 0x9318 && trace.observed & 1);
	reset(&f); io.quarantined = 0; f.stuck = 0x90d4;
	assert(onyx_gen8_clocks_tcxo(&io, &trace) == ONYX_CLOCK_TIMEOUT && io.quarantined);
	assert(!(f.r[0x90d4 / 4] & 1) && !(f.r[0x90e8 / 4] & 1));
	assert(f.r[0x931c / 4] == 0x100000 && f.r[0x93f0 / 4] == 0x100000);
	assert(trace.phase == 4 && trace.offset == 0x90d4 && trace.observed & 0x80000000u);
	for (unsigned n = 0; n < 5; n++) assert(!(f.r[expected_aon[n] / 4] & 1));
	reset(&f); io.quarantined = 0; f.reject = 0x9008;
	assert(onyx_gen8_clocks_tcxo(&io, &trace) == ONYX_CLOCK_OK && !io.quarantined);
	assert(trace.unlatched_aon == 2);
	reset(&f); f.reject = 0x93e8; f.r[0x93e8 / 4] = 0;
	assert(onyx_gen8_clocks_tcxo(&io, &trace) == ONYX_CLOCK_OK);
	assert(trace.unlatched_branches == 2);
	reset(&f); f.reject = 0x93e8;
	assert(onyx_gen8_clocks_tcxo(&io, &trace) == ONYX_CLOCK_TIMEOUT && io.quarantined);
	assert(trace.offset == 0x93e8 && trace.phase == 4);
	reset(&f); io.quarantined = 0; f.reject = 0x93e8; f.r[0x93e8 / 4] = 0xa0000000;
	assert(onyx_gen8_clocks_tcxo(&io, &trace) == ONYX_CLOCK_OK); /* FSM ON */
	reset(&f); f.reject = 0x90bc;
	assert(onyx_gen8_clocks_tcxo(&io, &trace) == ONYX_CLOCK_OK && !io.quarantined);
	assert(trace.unlatched_branches == 8 && f.waits > 0);
	io.quarantined = 1;
	unsigned writes = f.writes;
	assert(onyx_gen8_clocks_tcxo(&io, &trace) == ONYX_CLOCK_ARGUMENT && f.writes == writes);
	puts("GMU TCXO clocks: success, CX/ownership gates, update busy, rejected config, root/branch timeouts and rollback passed");
}
