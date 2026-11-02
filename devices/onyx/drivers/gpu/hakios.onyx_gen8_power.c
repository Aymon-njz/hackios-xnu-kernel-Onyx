#include "hakios.onyx_gen8_power.h"

static int clock_wait(struct onyx_rsc_io *io, uint32_t at, uint32_t mask, uint32_t wanted)
{
	for (unsigned us = 0; us < 2000; us += 10) {
		if ((io->read(io->context, at) & mask) == wanted) return 1;
		io->delay_us(io->context, 10);
	}
	return 0;
}

enum onyx_clock_result
onyx_gen8_clocks_tcxo(struct onyx_rsc_io *io)
{
	/* Tuna gpucc-tuna.c and clk-rcg2.c: source 0 is TCXO, HID divider
	 * encodes 2*div-1. CMD root-enable=bit1, update=bit0, root-off=bit31. */
	static const uint32_t roots[] = { 0x93ec, 0x9318 }; /* HUB, GMU */
	static const uint32_t branches[] = { 0x9008, 0x90e4, 0x93e8, 0x90e8, 0x90bc, 0x90d4 };
	uint32_t old_cmd[2], old_cfg[2], old_branch[6];
	unsigned roots_touched = 0, branches_touched = 0;
	enum onyx_clock_result result = ONYX_CLOCK_READBACK;
	if (!io || !io->read || !io->write || !io->delay_us || !io->owned || io->quarantined)
		return ONYX_CLOCK_ARGUMENT;
	if ((io->read(io->context, 0x9080) & 0x80000001u) != 0x80000000u)
		return ONYX_CLOCK_CX_OFF;
	for (unsigned n = 0; n < 2; n++) {
		old_cmd[n] = io->read(io->context, roots[n]);
		old_cfg[n] = io->read(io->context, roots[n] + 4);
		if (old_cmd[n] & 1) return ONYX_CLOCK_ARGUMENT; /* update already in progress */
	}
	for (unsigned n = 0; n < 6; n++) old_branch[n] = io->read(io->context, branches[n]);
	for (unsigned n = 0; n < 2; n++) {
		uint32_t config = (old_cfg[n] & ~0x71fu) | 1u;
		roots_touched = n + 1;
		io->write(io->context, roots[n], (old_cmd[n] & ~1u) | 2u);
		io->write(io->context, roots[n] + 4, config);
		if ((io->read(io->context, roots[n] + 4) & 0x71f) != 1) goto rollback;
		io->write(io->context, roots[n], io->read(io->context, roots[n]) | 3u);
		if (!clock_wait(io, roots[n], 1u | (1u << 31), 0)) {
			result = ONYX_CLOCK_TIMEOUT; goto rollback;
		}
	}
	for (unsigned n = 0; n < 6; n++) {
		branches_touched = n + 1;
		io->write(io->context, branches[n], old_branch[n] | 1u);
		if (!(io->read(io->context, branches[n]) & 1)) goto rollback;
		/* Tuna AHB uses BRANCH_HALT_DELAY. Other branches have bit31
		 * status, including always-on controls; do not treat vote=running. */
		if (branches[n] == 0x90bc) io->delay_us(io->context, 10);
		else if (!clock_wait(io, branches[n], 1u << 31, 0)) {
			result = ONYX_CLOCK_TIMEOUT; goto rollback;
		}
	}
	return ONYX_CLOCK_OK;
rollback:
	while (branches_touched) {
		unsigned n = --branches_touched;
		uint32_t current = io->read(io->context, branches[n]);
		io->write(io->context, branches[n], (current & ~1u) | (old_branch[n] & 1));
	}
	while (roots_touched) {
		unsigned n = --roots_touched;
		io->write(io->context, roots[n] + 4, old_cfg[n]);
		io->write(io->context, roots[n], io->read(io->context, roots[n]) | 3u);
		(void)clock_wait(io, roots[n], 1, 0);
		uint32_t current = io->read(io->context, roots[n]);
		io->write(io->context, roots[n], (current & ~3u) | (old_cmd[n] & 2));
	}
	io->quarantined = 1;
	return result;
}

enum onyx_cx_result
onyx_gen8_cx_enable(struct onyx_rsc_io *io, unsigned supplies_ready, uint32_t *saved)
{
	if (!io || !io->read || !io->write || !io->delay_us || !saved ||
	    !io->owned || io->quarantined) return ONYX_CX_ARGUMENT;
	if (!supplies_ready) return ONYX_CX_SUPPLIES;
	uint32_t before = io->read(io->context, 0x9080);
	*saved = before;
	/* Stock gdsc.c defers to hardware when HW_CONTROL bit 1 is set. */
	if (before & 2) return ONYX_CX_HW_CONTROL;
	if ((before & 0x80000001u) == 0x80000000u) return ONYX_CX_OK;
	io->write(io->context, 0x9080, before & ~1u);
	if (io->read(io->context, 0x9080) & 1) {
		io->quarantined = 1;
		return ONYX_CX_WRITE_FAILED;
	}
	/* The downstream driver waits after a collapse write before status
	 * polling; use bounded waits here, never read unpowered GMU registers. */
	io->delay_us(io->context, 1);
	for (unsigned us = 0; us < 2000; us += 10) {
		if (io->read(io->context, 0x9080) & (1u << 31)) return ONYX_CX_OK;
		io->delay_us(io->context, 10);
	}
	/* Restore our request bit only, preserving status and unrelated fields.
	 * This requests rollback; it does not claim the physical domain is off. */
	uint32_t current = io->read(io->context, 0x9080);
	io->write(io->context, 0x9080, (current & ~1u) | (before & 1));
	io->quarantined = 1;
	return ONYX_CX_TIMEOUT;
}
