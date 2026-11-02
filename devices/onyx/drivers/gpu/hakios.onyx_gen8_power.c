#include "hakios.onyx_gen8_power.h"
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
