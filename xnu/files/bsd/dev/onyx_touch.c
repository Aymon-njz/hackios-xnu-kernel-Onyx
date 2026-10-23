/*
 * First-stage touchscreen discovery for Xiaomi Onyx (Redmi Turbo 4 Pro / POCO F7).
 *
 * The phone uses an NT38771 on QUPv3 SE4 SPI. Android recovery identifies it as
 * PID 0x5998 and uses an event buffer at controller address 0x031400. This probe
 * is deliberately read-only: it does not enable clocks, change pinmux, toggle
 * reset, or send a command to the controller.
 *
 * Enable only with `onyx_touch_probe=1`. A later report reader must remain
 * firmware-free and separate from this discovery path.
 */
#include <kern/clock.h>
#include <kern/thread.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <sys/redwood_input.h>
#include <sys/systm.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);

#define ONYX_TLMM_PHYS           0x0f000000UL
#define ONYX_TLMM_SIZE           0x100000UL
#define ONYX_SE4_PHYS            0x00a90000UL
#define ONYX_SE_SIZE             0x4000UL

#define GPIO_IO(base, n)         ((base) + 0x1000UL * (n) + 4)
#define GENI_STATUS(base)        ((base) + 0x040)
#define GENI_SER_M_CLK_CFG(base) ((base) + 0x048)
#define GENI_IF_DISABLE_RO(base) ((base) + 0x064)
#define GENI_FW_REVISION_RO(base) ((base) + 0x068)
#define SE_HW_PARAM_0(base)      ((base) + 0xe24)

static inline uint32_t
onyx_touch_rd(vm_offset_t address)
{
	return *(volatile uint32_t *)address;
}

static void
onyx_touch_probe_thread(__unused void *parameter, __unused wait_result_t result)
{
	int mmio = 0;
	vm_offset_t tlmm, se;

	/* Avoid early platform bring-up: this runs after bsd_init has released its thread. */
	delay_for_interval(5, NSEC_PER_SEC);
	/*
	 * The normal probe remains passive.  A second-stage argument is required
	 * before this thread maps even the read-only controller registers.
	 */
	printf("onyx-touch: deferred stage 1; NT38771 PID 0x5998, SPI4/SE4 %#x, event buffer %#x\n",
	    ONYX_SE4_PHYS, 0x031400);
	if (!PE_parse_boot_argn("onyx_touch_mmio", &mmio, sizeof(mmio)) || mmio != 1) {
		return;
	}
	tlmm = ml_io_map(ONYX_TLMM_PHYS, ONYX_TLMM_SIZE);
	se = ml_io_map(ONYX_SE4_PHYS, ONYX_SE_SIZE);
	if (!tlmm || !se) {
		printf("onyx-touch: cannot map TLMM or QUPv3 SE4\n");
		return;
	}
	printf("onyx-touch: stage 2 MMIO: SE fw %#x fifo-if %#x status %#x clk %#x hw %#x; gpio irq78=%u cs19=%u\n",
	    onyx_touch_rd(GENI_FW_REVISION_RO(se)), onyx_touch_rd(GENI_IF_DISABLE_RO(se)),
	    onyx_touch_rd(GENI_STATUS(se)), onyx_touch_rd(GENI_SER_M_CLK_CFG(se)), onyx_touch_rd(SE_HW_PARAM_0(se)),
	    onyx_touch_rd(GPIO_IO(tlmm, 78)) & 1, onyx_touch_rd(GPIO_IO(tlmm, 19)) & 1);
}

void
onyx_touch_init(void)
{
	int enabled = 0;
	thread_t thread;

	if (!PE_parse_boot_argn("onyx_touch_probe", &enabled, sizeof(enabled)) || enabled != 1) {
		return;
	}
	if (kernel_thread_start(onyx_touch_probe_thread, NULL, &thread) == KERN_SUCCESS) {
		thread_deallocate(thread);
	}
}
