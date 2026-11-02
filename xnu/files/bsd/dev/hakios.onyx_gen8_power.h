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
struct onyx_clock_trace { uint32_t phase, offset, mask, expected, observed; };
enum onyx_clock_result onyx_gen8_clocks_tcxo(struct onyx_rsc_io *gpucc,
    struct onyx_clock_trace *trace);

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
#endif
