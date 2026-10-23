/*
 * First-stage touchscreen discovery for Xiaomi Onyx (Redmi Turbo 4 Pro / POCO F7).
 *
 * The phone uses an NT38771 on QUPv3 SE4 SPI. Android recovery identifies it as
 * PID 0x5998 and uses an event buffer at controller address 0x031400.
 *
 * `onyx_touch_probe=1` enables discovery only.  `onyx_touch=1` enables the
 * firmware-free, bit-banged SPI report reader after iOS is already up.
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

#define GPIO_CFG(base, n)        ((base) + 0x1000UL * (n))
#define GPIO_IO(base, n)         ((base) + 0x1000UL * (n) + 4)
#define GENI_STATUS(base)        ((base) + 0x040)
#define GENI_SER_M_CLK_CFG(base) ((base) + 0x048)
#define GENI_IF_DISABLE_RO(base) ((base) + 0x064)
#define GENI_FW_REVISION_RO(base) ((base) + 0x068)
#define SE_HW_PARAM_0(base)      ((base) + 0xe24)

#define ONYX_PIN_MISO            16
#define ONYX_PIN_MOSI            17
#define ONYX_PIN_CLK             18
#define ONYX_PIN_CS              19
#define ONYX_PIN_IRQ             78
#define ONYX_NVT_EVENT           0x031400U
/* Novatek's 65-byte point packet plus its command byte at index zero. */
#define ONYX_NVT_REPORT          66
#define ONYX_NVT_MAX_TOUCHES     10
#define ONYX_TOUCH_BRIDGE_MAGIC  0x48544354U /* "TCTH" */

/*
 * The XNU core cannot safely access Onyx's Trusted-Touch-owned QUP engine.
 * The resident loader runs on the boot CPU while XNU runs on bootcpu=7 and
 * can use Qualcomm's own UEFI SPI driver.  This cache-line-separated mailbox
 * is the only data shared between them; XNU never calls into UEFI and never
 * maps the protected serial engine in bridge mode.
 */
struct onyx_touch_bridge {
	uint32_t magic;
	uint32_t version;
	volatile uint32_t sequence;
	volatile int32_t status;
	uint32_t report_length;
	uint32_t reserved[11];
	uint8_t report[ONYX_NVT_REPORT];
} __attribute__((aligned(128)));

static vm_offset_t onyx_tlmm;
static struct {
	bool down, seen;
	uint32_t x, y;
} onyx_contact[ONYX_NVT_MAX_TOUCHES];

static void onyx_touch_report(const uint8_t *report);

static void
onyx_touch_bridge_run(uint64_t bridge_pa)
{
	volatile struct onyx_touch_bridge *bridge;
	uint32_t sequence = 0;
	uint8_t report[ONYX_NVT_REPORT];

	bridge = (volatile struct onyx_touch_bridge *)ml_io_map((vm_offset_t)bridge_pa,
	    sizeof(*bridge));
	if (bridge == NULL || bridge->magic != ONYX_TOUCH_BRIDGE_MAGIC ||
	    bridge->version != 1 || bridge->report_length != ONYX_NVT_REPORT) {
		printf("onyx-touch: loader bridge unavailable at %#llx\n", bridge_pa);
		return;
	}
	printf("onyx-touch: loader SPI bridge ready at %#llx\n", bridge_pa);
	for (;;) {
		uint32_t next = bridge->sequence;

		if (next != sequence) {
			/* The loader publishes sequence last after cleaning the report. */
			__asm__ volatile ("dmb sy" ::: "memory");
			memcpy(report, (const void *)(uintptr_t)bridge->report, sizeof(report));
			sequence = next;
			onyx_touch_report(report);
		}
		delay_for_interval(4000, NSEC_PER_USEC);
	}
}

static inline uint32_t
onyx_touch_rd(vm_offset_t address)
{
	return *(volatile uint32_t *)address;
}

static inline void
onyx_touch_wr(vm_offset_t address, uint32_t value)
{
	*(volatile uint32_t *)address = value;
}

static void
onyx_gpio_configure(unsigned pin, unsigned function, bool output)
{
	/* Onyx TLMM: function starts at bit 2, 6 mA drive, no pull, output bit 9. */
	onyx_touch_wr(GPIO_CFG(onyx_tlmm, pin), function << 2 | 2u << 6 | (output ? 1u << 9 : 0));
}

static inline void
onyx_gpio_set(unsigned pin, bool high)
{
	onyx_touch_wr(GPIO_IO(onyx_tlmm, pin), high ? 2 : 0);
}

static inline bool
onyx_gpio_get(unsigned pin)
{
	return onyx_touch_rd(GPIO_IO(onyx_tlmm, pin)) & 1;
}

static bool
onyx_spi_transfer(const uint8_t *tx, uint8_t *rx, uint32_t length)
{
	onyx_gpio_set(ONYX_PIN_CS, false);
	for (uint32_t i = 0; i < length; i++) {
		uint8_t in = 0, out = tx ? tx[i] : 0;
		for (int bit = 7; bit >= 0; bit--) {
			onyx_gpio_set(ONYX_PIN_MOSI, out >> bit & 1);
			onyx_gpio_set(ONYX_PIN_CLK, true);
			in = (uint8_t)(in << 1 | onyx_gpio_get(ONYX_PIN_MISO));
			onyx_gpio_set(ONYX_PIN_CLK, false);
		}
		if (rx) {
			rx[i] = in;
		}
	}
	onyx_gpio_set(ONYX_PIN_CS, true);
	return true;
}

static bool
onyx_nvt_read(uint32_t address, uint8_t *data, uint32_t length)
{
	uint8_t page[3] = { 0xff, (uint8_t)(address >> 15), (uint8_t)(address >> 7) };
	uint8_t tx[ONYX_NVT_REPORT + 1] = { 0 }, rx[ONYX_NVT_REPORT + 1] = { 0 };

	if (length > ONYX_NVT_REPORT) {
		return false;
	}
	/* Novatek SPI selects a 15-bit page then reads from its low-seven-bit offset. */
	tx[0] = (uint8_t)(address & 0x7f);
	onyx_spi_transfer(page, NULL, sizeof(page));
	onyx_spi_transfer(tx, rx, length + 1);
	memcpy(data, rx + 1, length);
	return true;
}

static void
onyx_touch_report(const uint8_t *report)
{
	for (unsigned id = 0; id < ONYX_NVT_MAX_TOUCHES; id++) {
		onyx_contact[id].seen = false;
	}
	for (unsigned slot = 0; slot < ONYX_NVT_MAX_TOUCHES; slot++) {
		const uint8_t *p = report + 1 + slot * 6;
		unsigned event = p[0] & 7, id = (p[0] >> 3) - 1;
		uint32_t x, y;

		if (id >= ONYX_NVT_MAX_TOUCHES || (event != 1 && event != 2)) {
			continue;
		}
		x = ((uint32_t)p[1] << 4) | (p[2] >> 4);
		y = ((uint32_t)(p[2] & 0xf) << 8) | p[3];
		x = x >= 1280 ? 65535 : x * 65535 / 1280;
		y = y >= 2772 ? 65535 : y * 65535 / 2772;
		onyx_contact[id].seen = true;
		if (!onyx_contact[id].down) {
			redwood_touch_post(REDWOOD_TOUCH_DOWN, id, x, y);
		} else if (onyx_contact[id].x != x || onyx_contact[id].y != y) {
			redwood_touch_post(REDWOOD_TOUCH_MOVE, id, x, y);
		}
		onyx_contact[id].down = true;
		onyx_contact[id].x = x;
		onyx_contact[id].y = y;
	}
	for (unsigned id = 0; id < ONYX_NVT_MAX_TOUCHES; id++) {
		if (onyx_contact[id].down && !onyx_contact[id].seen) {
			onyx_contact[id].down = false;
			redwood_touch_post(REDWOOD_TOUCH_UP, id, onyx_contact[id].x, onyx_contact[id].y);
		}
	}
}

static void
onyx_touch_run(void)
{
	uint8_t report[ONYX_NVT_REPORT];

	onyx_tlmm = ml_io_map(ONYX_TLMM_PHYS, ONYX_TLMM_SIZE);
	if (!onyx_tlmm) {
		printf("onyx-touch: cannot map TLMM\n");
		return;
	}
	/* GPIO mode avoids taking ownership of QUPv3 clocks while the first driver is verified. */
	onyx_gpio_set(ONYX_PIN_CS, true);
	onyx_gpio_set(ONYX_PIN_CLK, false);
	onyx_gpio_set(ONYX_PIN_MOSI, false);
	onyx_gpio_configure(ONYX_PIN_MISO, 0, false);
	onyx_gpio_configure(ONYX_PIN_MOSI, 0, true);
	onyx_gpio_configure(ONYX_PIN_CLK, 0, true);
	onyx_gpio_configure(ONYX_PIN_CS, 0, true);
	onyx_gpio_configure(ONYX_PIN_IRQ, 0, false);
	printf("onyx-touch: NT38771 polling enabled; irq=%u\n", onyx_gpio_get(ONYX_PIN_IRQ));
	for (;;) {
		if (!onyx_gpio_get(ONYX_PIN_IRQ) && onyx_nvt_read(ONYX_NVT_EVENT, report, sizeof(report))) {
			onyx_touch_report(report);
		}
		delay_for_interval(8000, NSEC_PER_USEC);
	}
}

static void
onyx_touch_probe_thread(__unused void *parameter, __unused wait_result_t result)
{
	int mmio = 0, run = 0;
	uint64_t bridge_pa = 0;
	vm_offset_t tlmm, se;

	/* Avoid early platform bring-up: this runs after bsd_init has released its thread. */
	delay_for_interval(5, NSEC_PER_SEC);
	/*
	 * The normal probe remains passive.  A second-stage argument is required
	 * before this thread maps even the read-only controller registers.
	 */
	printf("onyx-touch: deferred stage 1; NT38771 PID 0x5998, SPI4/SE4 %#x, event buffer %#x\n",
	    ONYX_SE4_PHYS, 0x031400);
	if (PE_parse_boot_argn("onyx_touch_shared_pa", &bridge_pa, sizeof(bridge_pa)) && bridge_pa != 0) {
		onyx_touch_bridge_run(bridge_pa);
		return;
	}
	if (!PE_parse_boot_argn("onyx_touch_mmio", &mmio, sizeof(mmio)) || mmio != 1) {
		PE_parse_boot_argn("onyx_touch", &run, sizeof(run));
		if (run == 1) {
			onyx_touch_run();
		}
		return;
	}
	tlmm = ml_io_map(ONYX_TLMM_PHYS, ONYX_TLMM_SIZE);
	if (!tlmm) {
		printf("onyx-touch: cannot map TLMM\n");
		return;
	}
	printf("onyx-touch: TLMM mapped at %#lx\n", (unsigned long)tlmm);
	se = ml_io_map(ONYX_SE4_PHYS, ONYX_SE_SIZE);
	if (!se) {
		printf("onyx-touch: cannot map QUPv3 SE4\n");
		return;
	}
	printf("onyx-touch: QUPv3 SE4 mapped at %#lx\n", (unsigned long)se);
	/*
	 * Onyx assigns the touch bus to Qualcomm Trusted Touch.  The mappings are
	 * valid, but a load from the peripheral causes the secure monitor to reset
	 * the non-secure OS until its ownership handoff has run.  Do not turn a
	 * diagnostic boot into a reboot loop while that handoff is being ported.
	 */
	printf("onyx-touch: SE4 is Trusted-Touch owned; raw access remains disabled pending VM handoff\n");
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
