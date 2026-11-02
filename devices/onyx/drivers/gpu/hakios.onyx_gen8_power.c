#include "hakios.onyx_gen8_power.h"

static int clock_wait(struct onyx_rsc_io *io, uint32_t at, uint32_t mask, uint32_t wanted)
{
	for (unsigned us = 0; us < 2000; us += 10) {
		if ((io->read(io->context, at) & mask) == wanted) return 1;
		io->delay_us(io->context, 10);
	}
	return 0;
}

static int branch_running(uint32_t value)
{
	/* Stock clk_branch2_check_halt: CLK_OFF clear OR NOC FSM status ON. */
	return !(value & 0x80000000u) || ((value >> 28) & 7) == 2;
}

static int branch_wait(struct onyx_rsc_io *io, uint32_t at)
{
	for (unsigned us = 0; us < 2000; us += 10) {
		if (branch_running(io->read(io->context, at))) return 1;
		io->delay_us(io->context, 10);
	}
	return 0;
}

enum onyx_clock_result
onyx_gen8_clocks_tcxo(struct onyx_rsc_io *io, struct onyx_clock_trace *trace)
{
	/* Tuna gpucc-tuna.c and clk-rcg2.c: source 0 is TCXO, HID divider
	 * encodes 2*div-1. CMD root-enable=bit1, update=bit0, root-off=bit31. */
	static const uint32_t roots[] = { 0x93ec, 0x9318 }; /* HUB, GMU */
	/* gpucc-tuna.c probe order. These controls have no clk_branch halt
	 * contract: never interpret bit31 as status here. */
	static const uint32_t aon[] = { 0x93a4, 0x9008, 0x93a8, 0x9004, 0x90cc };
	static const uint32_t branches[] = { 0x90e4, 0x93e8, 0x90e8, 0x90bc, 0x90d4 };
	uint32_t old_cmd[2], old_cfg[2], old_branch[5], old_aon[5];
	unsigned roots_touched = 0, branches_touched = 0, aon_touched = 0;
	enum onyx_clock_result result = ONYX_CLOCK_READBACK;
	struct onyx_clock_trace local = {0};
	uint32_t unlatched_branches = 0, unlatched_aon = 0;
	if (!trace) trace = &local;
	*trace = local;
	if (!io || !io->read || !io->write || !io->delay_us || !io->owned || io->quarantined)
		return ONYX_CLOCK_ARGUMENT;
	if ((io->read(io->context, 0x9080) & 0x80000001u) != 0x80000000u)
		return ONYX_CLOCK_CX_OFF;
	for (unsigned n = 0; n < 2; n++) {
		old_cmd[n] = io->read(io->context, roots[n]);
		old_cfg[n] = io->read(io->context, roots[n] + 4);
		if (old_cmd[n] & 1) return ONYX_CLOCK_ARGUMENT; /* update already in progress */
	}
	for (unsigned n = 0; n < 5; n++) old_branch[n] = io->read(io->context, branches[n]);
	for (unsigned n = 0; n < 5; n++) old_aon[n] = io->read(io->context, aon[n]);
	/* Follow the stock always-on prerequisite order before roots/branches.
	 * Preserve all unrelated fields. Record bit0 readback after the
	 * complete sequence; stock does not require these request bits to echo.
	 * There is no defined halt polling contract for these controls. */
	for (unsigned n = 0; n < 5; n++) {
		aon_touched = n + 1;
		io->write(io->context, aon[n], old_aon[n] | 1u);
	}
	io->delay_us(io->context, 10);
	for (unsigned n = 0; n < 2; n++) {
		uint32_t config = (old_cfg[n] & ~0x71fu) | 1u;
		roots_touched = n + 1;
		io->write(io->context, roots[n], (old_cmd[n] & ~1u) | 2u);
		io->write(io->context, roots[n] + 4, config);
		*trace = (struct onyx_clock_trace){.phase=1, .offset=roots[n] + 4, .mask=0x71f, .expected=1, .observed=
		    io->read(io->context, roots[n] + 4)};
		if ((trace->observed & trace->mask) != trace->expected) goto rollback;
		io->write(io->context, roots[n], io->read(io->context, roots[n]) | 3u);
		if (!clock_wait(io, roots[n], 1u | (1u << 31), 0)) {
			*trace = (struct onyx_clock_trace){.phase=2, .offset=roots[n], .mask=0x80000001u, .expected=0, .observed=
			    io->read(io->context, roots[n])};
			result = ONYX_CLOCK_TIMEOUT; goto rollback;
		}
	}
	for (unsigned n = 0; n < 5; n++) {
		branches_touched = n + 1;
		io->write(io->context, branches[n], old_branch[n] | 1u);
		*trace = (struct onyx_clock_trace){.phase=3, .offset=branches[n], .mask=1, .expected=1, .observed=
		    io->read(io->context, branches[n])};
		if (!(trace->observed & 1)) unlatched_branches |= 1u << n;
		/* AHB has no status bit contract: retain strict request checking. */
		if (branches[n] == 0x90bc && !(trace->observed & 1)) goto rollback;
		/* Tuna AHB uses BRANCH_HALT_DELAY. Other branches have bit31
		 * status; do not treat vote=running. */
		if (branches[n] == 0x90bc) io->delay_us(io->context, 10);
		else if (!branch_wait(io, branches[n])) {
			*trace = (struct onyx_clock_trace){.phase=4, .offset=branches[n], .mask=0x80000000u, .expected=0, .observed=
			    io->read(io->context, branches[n])};
			result = ONYX_CLOCK_TIMEOUT; goto rollback;
		}
	}
	for (unsigned n = 0; n < 5; n++) {
		*trace = (struct onyx_clock_trace){.phase=5, .offset=aon[n], .mask=1, .expected=1, .observed=
		    io->read(io->context, aon[n])};
		if (!(trace->observed & 1)) unlatched_aon |= 1u << n;
	}
	trace->unlatched_branches = unlatched_branches;
	trace->unlatched_aon = unlatched_aon;
	return ONYX_CLOCK_OK;
rollback:
	trace->unlatched_branches = unlatched_branches;
	trace->unlatched_aon = unlatched_aon;
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
	while (aon_touched) {
		unsigned n = --aon_touched;
		uint32_t current = io->read(io->context, aon[n]);
		io->write(io->context, aon[n], (current & ~1u) | (old_aon[n] & 1));
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
