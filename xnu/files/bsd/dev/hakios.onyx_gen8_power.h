/* Bounded power/clock helpers derived from Tuna GPUCC and GDSC. */
#ifndef HAKIOS_ONYX_GEN8_POWER_H
#define HAKIOS_ONYX_GEN8_POWER_H
#include <stdint.h>
#include "hakios.onyx_rpmh.h"

enum onyx_cx_result {
	ONYX_CX_OK, ONYX_CX_ARGUMENT, ONYX_CX_SUPPLIES, ONYX_CX_HW_CONTROL,
	ONYX_CX_WRITE_FAILED, ONYX_CX_TIMEOUT
};
/* I/O addresses are relative to Tuna GPUCC. Caller must establish supplies
 * first. Only the CX collapse bit is changed; no PLL, GMU or GX access. */
enum onyx_cx_result onyx_gen8_cx_enable(struct onyx_rsc_io *gpucc,
    unsigned supplies_ready, uint32_t *saved_gdscr);

enum onyx_clock_result {
	ONYX_CLOCK_OK, ONYX_CLOCK_ARGUMENT, ONYX_CLOCK_CX_OFF,
	ONYX_CLOCK_READBACK, ONYX_CLOCK_TIMEOUT
};
/* Configure GMU/HUB roots from TCXO at 19.2 MHz, then open CX branches.
 * No PLL or GX programming. This is an access-test frequency, not the final
 * GMU firmware operating point. On success clocks stay on until reboot.
 * On failure saved request fields are restored best-effort and quarantined. */
/* Last checked register, captured before rollback. phase: 1=config,
 * 2=root update, 3=branch vote, 4=branch halt, 5=always-on request. */
struct onyx_clock_trace {
	uint32_t phase, offset, mask, expected, observed;
	/* Request bits that did not echo. These are diagnostics, not proof of
	 * running clocks; stock uses branch halt/FSM status instead. */
	uint32_t unlatched_branches, unlatched_aon;
};
enum onyx_clock_result onyx_gen8_clocks_tcxo(struct onyx_rsc_io *gpucc,
    struct onyx_clock_trace *trace);

/* Read-only GMUCX AO counter test, after the caller establishes CX/clocks.
 * Offsets are bytes relative to the CM3 control page at physical 0x03d7e000.
 * Advancement proves an observable AO counter, not CM3 firmware execution.
 */
enum onyx_counter_result {
    ONYX_COUNTER_OK, ONYX_COUNTER_ARGUMENT, ONYX_COUNTER_UNSTABLE,
    ONYX_COUNTER_STALLED, ONYX_COUNTER_BACKWARDS
};
enum onyx_counter_result onyx_gen8_counter_sample(struct onyx_rsc_io *io,
    uint64_t *before, uint64_t *after);

enum onyx_clock_result onyx_gen8_smmu_clock(struct onyx_rsc_io *io);

enum onyx_gen8_power_missing {
	ONYX_CX_OFF = 1u << 0,
	ONYX_CX_COLLAPSED = 1u << 1,
	ONYX_GMU_CLOCK_OFF = 1u << 2,
	ONYX_CXO_CLOCK_OFF = 1u << 3,
	ONYX_HUB_CLOCK_OFF = 1u << 4,
	ONYX_GMU_RCG_OFF = 1u << 5,
};

static inline uint32_t
onyx_gen8_power_missing(uint32_t cx, uint32_t gmu, uint32_t cxo,
    uint32_t hub, uint32_t gmu_rcg)
{
	uint32_t missing = 0;
	if (!(cx & (1u << 31))) missing |= ONYX_CX_OFF;
	if (cx & 1) missing |= ONYX_CX_COLLAPSED;
	/* A bit-0 vote alone is not evidence of a running branch. */
	if (gmu & (1u << 31)) missing |= ONYX_GMU_CLOCK_OFF;
	if (cxo & (1u << 31)) missing |= ONYX_CXO_CLOCK_OFF;
	if (hub & (1u << 31)) missing |= ONYX_HUB_CLOCK_OFF;
	if (gmu_rcg & (1u << 31)) missing |= ONYX_GMU_RCG_OFF;
	return missing;
}
/* Switch already-running TCXO roots to 500MHz GMU / 200MHz HUB.
 * Requires independently established voltage votes and a verified 600MHz
 * GPLL0 parent. GPU PLL0 must already be locked at 1GHz with /2 output.
 * Does not initialize/calibrate PLL0 or establish bandwidth votes.
 */
enum onyx_clock_result onyx_gen8_clocks_operating(struct onyx_rsc_io *,
 unsigned supplies_ready, unsigned gpll0_600mhz_verified);
/* Cold, exclusively owned Lucid OLE PLL0 only. Caller has completed CX/MX
 * votes sufficient for the 1305.6MHz calibration point and keeps them held.
 * Both GMU/HUB roots must still use TCXO; no other PLL consumer is permitted.
 * Programs Tuna's stock 1GHz configuration, calibrates, then enables /2.
 */
enum onyx_clock_result onyx_gen8_pll0_cold(struct onyx_rsc_io *,
 unsigned calibration_supplies_ready);
/* GCC-relative I/O, minimum mapped length0x71154. Validate running GPLL0
 * from live Lucid OLE rate/lock registers, then enable only the GPU's CPH
 * branch. Never reprogram the shared GPLL0. Ownership covers that branch.
 */
enum onyx_clock_result onyx_gen8_gpll0_parent(struct onyx_rsc_io *, uint64_t *rate);
#endif
