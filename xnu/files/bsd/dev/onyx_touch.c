/*
 * First-stage touchscreen discovery for Xiaomi Onyx (Redmi Turbo 4 Pro / POCO F7).
 *
 * The phone uses an NT38771 on QUPv3 SE4 SPI. Android recovery identifies it as
 * PID 0x5998 and uses an event buffer at controller address 0x031400.
 *
 * `onyx_touch_probe=1 onyx_touch_geni=1` enables the firmware-free, polled
 * report reader after iOS is already up.  It uses QUPv3/GENI FIFO mode, as
 * OrangeFox does; touching an unclocked serial engine resets this phone.
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
#define ONYX_GCC_PHYS            0x00100000UL
#define ONYX_GCC_SIZE            0x60000UL
#define ONYX_SE4_PHYS            0x00a90000UL
#define ONYX_SE_SIZE             0x4000UL
#define ONYX_GPI1_PHYS           0x00a00000UL
#define ONYX_GPI_SIZE            0x60000UL
#define ONYX_APPS_SMMU_PHYS      0x15000000UL
#define ONYX_GPI_STREAM          0x00b6U

#define GPIO_CFG(base, n)        ((base) + 0x1000UL * (n))
#define GPIO_IO(base, n)         ((base) + 0x1000UL * (n) + 4)
#define GENI_FORCE_DEFAULT(base) ((base) + 0x020)
#define GENI_OUTPUT_CTRL(base)   ((base) + 0x024)
#define GENI_CGC_CTRL(base)      ((base) + 0x028)
#define GENI_STATUS(base)        ((base) + 0x040)
#define GENI_SER_M_CLK_CFG(base) ((base) + 0x048)
#define GENI_IF_DISABLE_RO(base) ((base) + 0x064)
#define GENI_FW_REVISION_RO(base) ((base) + 0x068)
#define GENI_CLK_SEL(base)       ((base) + 0x07c)
#define SPI_CPHA(base)           ((base) + 0x224)
#define SPI_LOOPBACK(base)       ((base) + 0x22c)
#define SPI_CPOL(base)           ((base) + 0x230)
#define SPI_DEMUX_OUTPUT_INV(base) ((base) + 0x24c)
#define SPI_DEMUX_SEL(base)      ((base) + 0x250)
#define GENI_BYTE_GRAN(base)     ((base) + 0x254)
#define GENI_DMA_MODE_EN(base)   ((base) + 0x258)
#define SPI_TRANS_CFG(base)      ((base) + 0x25c)
#define GENI_TX_PACKING_CFG0(base) ((base) + 0x260)
#define GENI_TX_PACKING_CFG1(base) ((base) + 0x264)
#define SPI_WORD_LEN(base)       ((base) + 0x268)
#define SPI_TX_TRANS_LEN(base)   ((base) + 0x26c)
#define SPI_RX_TRANS_LEN(base)   ((base) + 0x270)
#define SPI_DELAY_COUNTERS(base) ((base) + 0x278)
#define GENI_RX_PACKING_CFG0(base) ((base) + 0x284)
#define GENI_RX_PACKING_CFG1(base) ((base) + 0x288)
#define GENI_M_CMD0(base)        ((base) + 0x600)
#define GENI_M_CMD_CTRL(base)    ((base) + 0x604)
#define GENI_M_IRQ_STATUS(base)  ((base) + 0x610)
#define GENI_M_IRQ_EN(base)      ((base) + 0x614)
#define GENI_M_IRQ_CLEAR(base)   ((base) + 0x618)
#define GENI_S_IRQ_CLEAR(base)   ((base) + 0x648)
#define GENI_TX_FIFO(base)       ((base) + 0x700)
#define GENI_RX_FIFO(base)       ((base) + 0x780)
#define GENI_TX_FIFO_STATUS(base) ((base) + 0x800)
#define GENI_RX_FIFO_STATUS(base) ((base) + 0x804)
#define GENI_TX_WATERMARK(base)  ((base) + 0x80c)
#define GENI_RX_WATERMARK(base)  ((base) + 0x810)
#define GENI_RX_RFR_WATERMARK(base) ((base) + 0x814)
#define SE_DMA_TX_IRQ_CLR(base)  ((base) + 0xc44)
#define SE_DMA_RX_IRQ_CLR(base)  ((base) + 0xd44)
#define SE_GSI_EVENT_EN(base)    ((base) + 0xe18)
#define SE_IRQ_EN(base)          ((base) + 0xe1c)
#define SE_HW_PARAM_0(base)      ((base) + 0xe24)
#define SE_DMA_GENERAL_CFG(base) ((base) + 0xe30)

#define ONYX_GCC_VOTE(base)      ((base) + 0x52008)
#define ONYX_GCC_M_AHB_CBCR(base) ((base) + 0x23000)
#define ONYX_GCC_S_AHB_CBCR(base) ((base) + 0x23004)
#define ONYX_GCC_SE4_CBCR(base)  ((base) + 0x183cc)
#define ONYX_GCC_SE4_CMD(base)   ((base) + 0x183dc)
#define ONYX_GCC_SE4_CFG(base)   ((base) + 0x183e0)
#define ONYX_GCC_SE4_DFSR(base)  ((base) + 0x183f0)
#define ONYX_GCC_SE4_PERF_CFG(base, n) ((base) + 0x183f8 + 4 * (n))
#define ONYX_GCC_SE4_PERF_M(base, n) ((base) + 0x18438 + 4 * (n))
#define ONYX_GCC_SE4_PERF_N(base, n) ((base) + 0x18478 + 4 * (n))

#define ONYX_GCC_QUP_VOTES       ((1u << 18) | (1u << 19) | (1u << 20) | (1u << 21))
#define ONYX_GCC_SE4_VOTE        (1u << 26)
#define ONYX_CLOCK_OFF           (1u << 31)
#define ONYX_XO_HZ               19200000U
#define ONYX_PIN_FUNCTION_SPI    1

/* Tuna uses a 0x10000 EE adjustment for QUP1 GPI.  These offsets are from
 * the DT's 0xa00000 resource, after applying that adjustment to Qualcomm's
 * public GPI register layout. */
#define GPI_CH_CONTEXT(k)        (0x10000UL + 0x80UL * (k))
#define GPI_EV_CONTEXT           0x11000UL
#define GPI_CH_DOORBELL(k)       (0x12000UL + 0x08UL * (k))
#define GPI_EV_DOORBELL          0x12100UL
#define GPI_CH_CMD               0x13008UL
#define GPI_EV_CMD               0x13010UL
#define GPI_IRQ_TYPE             0x13080UL
#define GPI_IRQ_MASK             0x13088UL
#define GPI_CH_IRQ               0x13090UL
#define GPI_EV_IRQ               0x13094UL
#define GPI_IEOB_IRQ             0x130b0UL
#define GPI_GLOB_IRQ             0x13100UL
#define GPI_GPII_IRQ             0x13118UL
#define GPI_ERROR_LOG            0x13200UL
#define GPI_CH_SCRATCH(k, n)     (0x10060UL + 0x80UL * (k) + 4UL * (n))

#define SMMU_SCR0                0x000UL
#define SMMU_IDR0                0x020UL
#define SMMU_IDR1                0x024UL
#define SMMU_SGFSR               0x048UL
#define SMMU_SGFSYNR1            0x054UL
#define SMMU_SMR(n)              (0x800UL + 4UL * (n))
#define SMMU_S2CR(n)             (0xc00UL + 4UL * (n))
#define SMMU_CB_SCTLR            0x000UL
#define SMMU_CB_TTBR0            0x020UL
#define SMMU_CB_FSR              0x058UL
#define SMMU_CB_FAR              0x060UL
#define SMMU_CB_FSYNR0           0x068UL

#define M_CMD_DONE               (1u << 0)
#define M_CMD_CANCEL             (1u << 4)
#define M_CMD_ABORT              (1u << 5)
#define M_ERRORS                 ((1u << 1) | (1u << 2) | (1u << 3) | (1u << 24) | (1u << 25) | (1u << 28) | (1u << 29))
#define SPI_FULL_DUPLEX          (3u << 27)

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

static vm_offset_t onyx_tlmm, onyx_gcc, onyx_se, onyx_gpi;
static uint32_t onyx_fifo_depth, onyx_se_source_hz, onyx_se_clock_select;
static struct {
	bool down, seen;
	uint32_t x, y;
} onyx_contact[ONYX_NVT_MAX_TOUCHES];

static void onyx_touch_report(const uint8_t *report);
static bool onyx_clocks_on(void);

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

/*
 * GPI's DMA addresses are translated by the application-processor SMMU as
 * stream 0xb6 on Tuna.  Before taking a context, record whether firmware left
 * that stream translated, bypassed, faulting, or unmatched.  This is strictly
 * read-only: choosing a free match/context safely is a later diagnostic.
 */
static void
onyx_smmu_probe(void)
{
	vm_offset_t smmu = ml_io_map(ONYX_APPS_SMMU_PHYS, 0x2000);
	bool used[256] = { false };
	uint32_t scr0, idr0, idr1, matches, contexts, page, pages;
	uint32_t covering = UINT32_MAX, free_match = UINT32_MAX, free_context;

	if (!smmu) {
		printf("onyx-touch: cannot map apps SMMU\n");
		return;
	}
	scr0 = onyx_touch_rd(smmu + SMMU_SCR0);
	idr0 = onyx_touch_rd(smmu + SMMU_IDR0);
	idr1 = onyx_touch_rd(smmu + SMMU_IDR1);
	matches = idr0 & 0xff;
	contexts = idr1 & 0xff;
	page = idr1 >> 31 ? 0x10000 : 0x1000;
	pages = 2u << (idr1 >> 28 & 7);
	printf("onyx-touch: apps SMMU scr0 %#x idr0 %#x idr1 %#x; %u matches %u contexts, page %#x pages %u, global fault %#x stream %#x\n",
	    scr0, idr0, idr1, matches, contexts, page, pages,
	    onyx_touch_rd(smmu + SMMU_SGFSR), onyx_touch_rd(smmu + SMMU_SGFSYNR1));
	for (uint32_t n = 0; n < matches && SMMU_SMR(n) < 0xc00; n++) {
		uint32_t smr = onyx_touch_rd(smmu + SMMU_SMR(n));
		uint32_t s2cr = onyx_touch_rd(smmu + SMMU_S2CR(n));

		if (!(smr >> 31)) {
			if (free_match == UINT32_MAX) {
				free_match = n;
			}
			continue;
		}
		used[s2cr & 0xff] = true;
		uint32_t stream = smr & 0x7fff, mask = smr >> 16 & 0x7fff;
		if (((stream ^ ONYX_GPI_STREAM) & ~mask) == 0) {
			covering = n;
			printf("onyx-touch: apps SMMU match %u covers GPI stream %#x: smr %#x stream %#x mask %#x s2cr %#x type %u context %u\n",
			    n, ONYX_GPI_STREAM, smr, stream, mask, s2cr, s2cr >> 16 & 3, s2cr & 0xff);
		}
	}
	for (free_context = 0; free_context < contexts && used[free_context]; free_context++) {
	}
	printf("onyx-touch: apps SMMU GPI stream %s; first free match %u context %u\n",
	    covering == UINT32_MAX ? "is unmatched" : "has a covering match",
	    free_match, free_context);
	if (covering != UINT32_MAX) {
		uint32_t s2cr = onyx_touch_rd(smmu + SMMU_S2CR(covering));
		uint32_t context = s2cr & 0xff;
		if ((s2cr >> 16 & 3) == 0 && context < contexts) {
			vm_offset_t cb = ml_io_map(ONYX_APPS_SMMU_PHYS +
			    (vm_offset_t)(pages + context) * page, 0x1000);

			if (cb) {
				printf("onyx-touch: apps SMMU context %u sctlr %#x ttbr0 %08x:%08x fsr %#x far %08x:%08x fsynr0 %#x\n",
				    context, onyx_touch_rd(cb + SMMU_CB_SCTLR),
				    onyx_touch_rd(cb + SMMU_CB_TTBR0 + 4), onyx_touch_rd(cb + SMMU_CB_TTBR0),
				    onyx_touch_rd(cb + SMMU_CB_FSR), onyx_touch_rd(cb + SMMU_CB_FAR + 4),
				    onyx_touch_rd(cb + SMMU_CB_FAR), onyx_touch_rd(cb + SMMU_CB_FSYNR0));
			}
		}
	}
}

static void
onyx_gpi_probe(void)
{
	if (!onyx_gcc) {
		onyx_gcc = ml_io_map(ONYX_GCC_PHYS, ONYX_GCC_SIZE);
	}
	if (!onyx_gcc || !onyx_clocks_on()) {
		printf("onyx-touch: QUP clocks unavailable before GPI probe\n");
		return;
	}
	onyx_gpi = ml_io_map(ONYX_GPI1_PHYS, ONYX_GPI_SIZE);
	if (!onyx_gpi) {
		printf("onyx-touch: cannot map QUP1 GPI\n");
		return;
	}
	onyx_smmu_probe();
	printf("onyx-touch: GPII0 read-only state at %#lx\n", (unsigned long)onyx_gpi);
	for (unsigned channel = 0; channel < 2; channel++) {
		vm_offset_t context = onyx_gpi + GPI_CH_CONTEXT(channel);

		printf("onyx-touch: GPI ch%u ctx %#x len %#x base %08x:%08x rp %08x:%08x wp %08x:%08x db %08x:%08x\n",
		    channel, onyx_touch_rd(context), onyx_touch_rd(context + 4),
		    onyx_touch_rd(context + 12), onyx_touch_rd(context + 8),
		    onyx_touch_rd(context + 20), onyx_touch_rd(context + 16),
		    onyx_touch_rd(context + 28), onyx_touch_rd(context + 24),
		    onyx_touch_rd(onyx_gpi + GPI_CH_DOORBELL(channel) + 4),
		    onyx_touch_rd(onyx_gpi + GPI_CH_DOORBELL(channel)));
		printf("onyx-touch: GPI ch%u scratch %08x %08x %08x %08x\n", channel,
		    onyx_touch_rd(onyx_gpi + GPI_CH_SCRATCH(channel, 0)),
		    onyx_touch_rd(onyx_gpi + GPI_CH_SCRATCH(channel, 1)),
		    onyx_touch_rd(onyx_gpi + GPI_CH_SCRATCH(channel, 2)),
		    onyx_touch_rd(onyx_gpi + GPI_CH_SCRATCH(channel, 3)));
	}
	vm_offset_t event = onyx_gpi + GPI_EV_CONTEXT;
	printf("onyx-touch: GPI event ctx %#x len %#x base %08x:%08x rp %08x:%08x wp %08x:%08x db %08x:%08x\n",
	    onyx_touch_rd(event), onyx_touch_rd(event + 4),
	    onyx_touch_rd(event + 12), onyx_touch_rd(event + 8),
	    onyx_touch_rd(event + 20), onyx_touch_rd(event + 16),
	    onyx_touch_rd(event + 28), onyx_touch_rd(event + 24),
	    onyx_touch_rd(onyx_gpi + GPI_EV_DOORBELL + 4), onyx_touch_rd(onyx_gpi + GPI_EV_DOORBELL));
	printf("onyx-touch: GPI commands ch %#x ev %#x; irq type %#x mask %#x ch %#x ev %#x ieob %#x global %#x gpii %#x error %#x\n",
	    onyx_touch_rd(onyx_gpi + GPI_CH_CMD), onyx_touch_rd(onyx_gpi + GPI_EV_CMD),
	    onyx_touch_rd(onyx_gpi + GPI_IRQ_TYPE), onyx_touch_rd(onyx_gpi + GPI_IRQ_MASK),
	    onyx_touch_rd(onyx_gpi + GPI_CH_IRQ), onyx_touch_rd(onyx_gpi + GPI_EV_IRQ),
	    onyx_touch_rd(onyx_gpi + GPI_IEOB_IRQ), onyx_touch_rd(onyx_gpi + GPI_GLOB_IRQ),
	    onyx_touch_rd(onyx_gpi + GPI_GPII_IRQ), onyx_touch_rd(onyx_gpi + GPI_ERROR_LOG));
}

static void
onyx_gpio_configure(unsigned pin, unsigned function, bool output)
{
	/* Onyx TLMM: function starts at bit 2, 6 mA drive, no pull, output bit 9. */
	onyx_touch_wr(GPIO_CFG(onyx_tlmm, pin), function << 2 | 2u << 6 | (output ? 1u << 9 : 0));
}

static inline bool
onyx_gpio_get(unsigned pin)
{
	return onyx_touch_rd(GPIO_IO(onyx_tlmm, pin)) & 1;
}

static uint32_t
onyx_rcg_rate(uint32_t cfg, uint32_t m, uint32_t not_n_minus_m)
{
	static const uint32_t parent[8] = { ONYX_XO_HZ, 600000000, 0, 0, 0, 0, 300000000, 0 };
	uint64_t hz = parent[cfg >> 8 & 7];
	uint32_t divider = cfg & 0x1f, mode = cfg >> 12 & 3;

	if (divider) {
		hz = hz * 2 / (divider + 1);
	}
	if (mode) {
		uint32_t n = (~not_n_minus_m & 0xffff) + (m & 0xffff);

		if (n) {
			hz = hz * (m & 0xffff) / n;
		}
	}
	return (uint32_t)hz;
}

static bool
onyx_clocks_on(void)
{
	uint32_t votes = onyx_touch_rd(ONYX_GCC_VOTE(onyx_gcc));

	onyx_touch_wr(ONYX_GCC_VOTE(onyx_gcc), votes | ONYX_GCC_QUP_VOTES);
	if (onyx_touch_rd(ONYX_GCC_SE4_DFSR(onyx_gcc)) & 1) {
		for (uint32_t level = 0; level < 8; level++) {
			uint32_t hz = onyx_rcg_rate(onyx_touch_rd(ONYX_GCC_SE4_PERF_CFG(onyx_gcc, level)),
			    onyx_touch_rd(ONYX_GCC_SE4_PERF_M(onyx_gcc, level)),
			    onyx_touch_rd(ONYX_GCC_SE4_PERF_N(onyx_gcc, level)));

			if (hz >= 1000000 && (onyx_se_source_hz == 0 || hz < onyx_se_source_hz)) {
				onyx_se_source_hz = hz;
				onyx_se_clock_select = level;
			}
		}
	} else {
		/* Tuna's RCG parent 0 is the 19.2 MHz crystal; HID divider 1 means divide by one. */
		onyx_touch_wr(ONYX_GCC_SE4_CFG(onyx_gcc),
		    (onyx_touch_rd(ONYX_GCC_SE4_CFG(onyx_gcc)) & ~0x371fu) | 1);
		onyx_touch_wr(ONYX_GCC_SE4_CMD(onyx_gcc), onyx_touch_rd(ONYX_GCC_SE4_CMD(onyx_gcc)) | 1);
		for (unsigned i = 0; i < 500 && (onyx_touch_rd(ONYX_GCC_SE4_CMD(onyx_gcc)) & 1); i++) {
			delay_for_interval(10, NSEC_PER_USEC);
		}
		if (onyx_touch_rd(ONYX_GCC_SE4_CMD(onyx_gcc)) & 1) {
			printf("onyx-touch: SE4 clock configuration timed out\n");
			return false;
		}
		onyx_se_source_hz = ONYX_XO_HZ;
		onyx_se_clock_select = 0;
	}
	if (onyx_se_source_hz == 0) {
		printf("onyx-touch: no usable SE4 source clock\n");
		return false;
	}
	onyx_touch_wr(ONYX_GCC_VOTE(onyx_gcc),
	    onyx_touch_rd(ONYX_GCC_VOTE(onyx_gcc)) | ONYX_GCC_QUP_VOTES | ONYX_GCC_SE4_VOTE);
	for (unsigned i = 0; i < 1000; i++) {
		if (!((onyx_touch_rd(ONYX_GCC_M_AHB_CBCR(onyx_gcc)) |
		    onyx_touch_rd(ONYX_GCC_S_AHB_CBCR(onyx_gcc)) |
		    onyx_touch_rd(ONYX_GCC_SE4_CBCR(onyx_gcc))) & ONYX_CLOCK_OFF)) {
			return true;
		}
		delay_for_interval(10, NSEC_PER_USEC);
	}
	printf("onyx-touch: clocks stay off: %#x %#x %#x\n",
	    onyx_touch_rd(ONYX_GCC_M_AHB_CBCR(onyx_gcc)), onyx_touch_rd(ONYX_GCC_S_AHB_CBCR(onyx_gcc)),
	    onyx_touch_rd(ONYX_GCC_SE4_CBCR(onyx_gcc)));
	return false;
}

static const char *
onyx_geni_init(uint32_t hz)
{
	uint32_t divider;

	if ((onyx_touch_rd(GENI_FW_REVISION_RO(onyx_se)) >> 8 & 0xff) != 1) {
		return "SE4 has no SPI firmware";
	}
	/*
	 * GENI_IF_DISABLE_RO advertises the firmware's preferred interface.  The
	 * Qualcomm SPI driver still falls back to FIFO mode when GPI DMA is not
	 * available, even when this bit is set.  XNU has no GPI stack yet, so do
	 * the same instead of abandoning an otherwise live SPI engine.
	 */
	if (onyx_touch_rd(GENI_IF_DISABLE_RO(onyx_se)) & 1) {
		printf("onyx-touch: SE4 advertises GPI-only; forcing the supported FIFO fallback\n");
	}
	onyx_fifo_depth = onyx_touch_rd(SE_HW_PARAM_0(onyx_se)) >> 16 & 0x3f;
	if (onyx_fifo_depth < 4) {
		return "SE4 reports no FIFO";
	}
	onyx_touch_wr(SE_GSI_EVENT_EN(onyx_se), 0);
	onyx_touch_wr(GENI_M_IRQ_CLEAR(onyx_se), 0xffffffff);
	onyx_touch_wr(GENI_S_IRQ_CLEAR(onyx_se), 0xffffffff);
	onyx_touch_wr(SE_DMA_TX_IRQ_CLR(onyx_se), 0xffffffff);
	onyx_touch_wr(SE_DMA_RX_IRQ_CLR(onyx_se), 0xffffffff);
	onyx_touch_wr(GENI_CGC_CTRL(onyx_se), onyx_touch_rd(GENI_CGC_CTRL(onyx_se)) | 0x7f);
	onyx_touch_wr(SE_DMA_GENERAL_CFG(onyx_se), onyx_touch_rd(SE_DMA_GENERAL_CFG(onyx_se)) | 0xf);
	onyx_touch_wr(GENI_OUTPUT_CTRL(onyx_se), 0x7f);
	onyx_touch_wr(GENI_FORCE_DEFAULT(onyx_se), 1);
	onyx_touch_wr(GENI_DMA_MODE_EN(onyx_se), onyx_touch_rd(GENI_DMA_MODE_EN(onyx_se)) & ~1u);
	onyx_touch_wr(GENI_RX_WATERMARK(onyx_se), onyx_fifo_depth - 3);
	onyx_touch_wr(GENI_RX_RFR_WATERMARK(onyx_se), onyx_fifo_depth - 2);
	onyx_touch_wr(GENI_TX_WATERMARK(onyx_se), 0);
	onyx_touch_wr(SE_IRQ_EN(onyx_se), onyx_touch_rd(SE_IRQ_EN(onyx_se)) | 0xf);
	onyx_touch_wr(GENI_M_IRQ_EN(onyx_se), onyx_touch_rd(GENI_M_IRQ_EN(onyx_se)) | 0x7fc0007f);
	onyx_touch_wr(SPI_LOOPBACK(onyx_se), 0);
	onyx_touch_wr(SPI_DEMUX_SEL(onyx_se), 0);
	onyx_touch_wr(SPI_CPHA(onyx_se), 0);
	onyx_touch_wr(SPI_CPOL(onyx_se), 0);
	onyx_touch_wr(SPI_DEMUX_OUTPUT_INV(onyx_se), 0);
	onyx_touch_wr(SPI_TRANS_CFG(onyx_se), onyx_touch_rd(SPI_TRANS_CFG(onyx_se)) & ~2u);
	onyx_touch_wr(SPI_DELAY_COUNTERS(onyx_se), 0);
	onyx_touch_wr(GENI_TX_PACKING_CFG0(onyx_se), 0x0007f8fe);
	onyx_touch_wr(GENI_TX_PACKING_CFG1(onyx_se), 0x000ffefe);
	onyx_touch_wr(GENI_RX_PACKING_CFG0(onyx_se), 0x0007f8fe);
	onyx_touch_wr(GENI_RX_PACKING_CFG1(onyx_se), 0x000ffefe);
	onyx_touch_wr(GENI_BYTE_GRAN(onyx_se), 0);
	onyx_touch_wr(SPI_WORD_LEN(onyx_se), 4);
	divider = (onyx_se_source_hz + hz - 1) / hz;
	onyx_touch_wr(GENI_CLK_SEL(onyx_se), onyx_se_clock_select & 7);
	onyx_touch_wr(GENI_SER_M_CLK_CFG(onyx_se), (divider ? divider : 1) << 4 | 1);
	return NULL;
}

static void
onyx_geni_drain(uint8_t *rx, uint32_t length, uint32_t *received)
{
	uint32_t status = onyx_touch_rd(GENI_RX_FIFO_STATUS(onyx_se));
	uint32_t words = status & 0x01ffffff, bytes = words * 4;

	if (status & 0x80000000u) {
		uint32_t valid = status >> 28 & 7;
		if (valid && valid < 4) {
			bytes -= 4 - valid;
		}
	}
	for (uint32_t i = 0; i < words; i++) {
		uint32_t word = onyx_touch_rd(GENI_RX_FIFO(onyx_se));
		for (unsigned j = 0; j < 4 && bytes; j++, bytes--) {
			if (*received < length) {
				if (rx) {
					rx[*received] = (uint8_t)(word >> (8 * j));
				}
				(*received)++;
			}
		}
	}
}

static void
onyx_geni_recover(void)
{
	onyx_touch_wr(GENI_M_CMD_CTRL(onyx_se), 1u << 2);
	for (unsigned i = 0; i < 2000 && !(onyx_touch_rd(GENI_M_IRQ_STATUS(onyx_se)) & M_CMD_CANCEL); i++) {
		delay_for_interval(100, NSEC_PER_USEC);
	}
	if (!(onyx_touch_rd(GENI_M_IRQ_STATUS(onyx_se)) & M_CMD_CANCEL)) {
		onyx_touch_wr(GENI_M_CMD_CTRL(onyx_se), 1u << 1);
		for (unsigned i = 0; i < 2000 && !(onyx_touch_rd(GENI_M_IRQ_STATUS(onyx_se)) & M_CMD_ABORT); i++) {
			delay_for_interval(100, NSEC_PER_USEC);
		}
	}
	for (unsigned i = 0; i < 64 && (onyx_touch_rd(GENI_RX_FIFO_STATUS(onyx_se)) & 0x01ffffff); i++) {
		(void)onyx_touch_rd(GENI_RX_FIFO(onyx_se));
	}
	onyx_touch_wr(GENI_M_IRQ_CLEAR(onyx_se), 0xffffffff);
}

static bool
onyx_geni_transfer(const uint8_t *tx, uint8_t *rx, uint32_t length)
{
	static unsigned complaints;
	uint32_t sent = 0, received = 0, status = 0;
	uint64_t deadline;

	for (unsigned i = 0; onyx_touch_rd(GENI_STATUS(onyx_se)) & 1; i++) {
		if (i > 2000) {
			onyx_geni_recover();
			break;
		}
		delay_for_interval(100, NSEC_PER_USEC);
	}
	onyx_touch_wr(GENI_M_IRQ_CLEAR(onyx_se), 0xffffffff);
	onyx_touch_wr(SPI_TX_TRANS_LEN(onyx_se), length);
	onyx_touch_wr(SPI_RX_TRANS_LEN(onyx_se), length);
	onyx_touch_wr(GENI_M_CMD0(onyx_se), SPI_FULL_DUPLEX);
	clock_interval_to_deadline(200, NSEC_PER_MSEC, &deadline);
	while (mach_absolute_time() < deadline) {
		while (sent < length && (onyx_touch_rd(GENI_TX_FIFO_STATUS(onyx_se)) & 0x0fffffff) + 1 < onyx_fifo_depth) {
			uint32_t word = 0;
			for (unsigned j = 0; j < 4 && sent < length; j++, sent++) {
				word |= (uint32_t)(tx ? tx[sent] : 0) << (8 * j);
			}
			onyx_touch_wr(GENI_TX_FIFO(onyx_se), word);
		}
		status = onyx_touch_rd(GENI_M_IRQ_STATUS(onyx_se));
		onyx_geni_drain(rx, length, &received);
		if (status & M_ERRORS) {
			break;
		}
		if (status & M_CMD_DONE) {
			onyx_geni_drain(rx, length, &received);
			onyx_touch_wr(GENI_M_IRQ_CLEAR(onyx_se), status);
			if (received == length) {
				return true;
			}
			break;
		}
	}
	if (complaints++ < 8) {
		printf("onyx-touch: SPI transfer %u failed: status %#x sent %u received %u\n",
		    length, status, sent, received);
	}
	onyx_geni_recover();
	return false;
}

static bool
onyx_nvt_read(uint32_t address, uint8_t *data, uint32_t length)
{
	uint8_t page[3] = { 0xff, (uint8_t)(address >> 15), (uint8_t)(address >> 7) };
	uint8_t tx[ONYX_NVT_REPORT + 1] = { 0 }, rx[ONYX_NVT_REPORT + 1] = { 0 };

	if (length > ONYX_NVT_REPORT) {
		return false;
	}
	/* Writes have bit 7 set. Reads return one command byte and one dummy byte. */
	tx[0] = (uint8_t)(address & 0x7f);
	if (!onyx_geni_transfer(page, NULL, sizeof(page)) ||
	    !onyx_geni_transfer(tx, rx, length + 1)) {
		return false;
	}
	data[0] = tx[0];
	memcpy(data + 1, rx + 2, length - 1);
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
		/* NT36xxx SPI point data: X high, Y high, then both low nibbles. */
		x = ((uint32_t)p[1] << 4) | (p[3] >> 4);
		y = ((uint32_t)p[2] << 4) | (p[3] & 0xf);
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
	const char *problem;
	uint8_t report[ONYX_NVT_REPORT];
	unsigned idle = 0, reads = 0;

	onyx_tlmm = ml_io_map(ONYX_TLMM_PHYS, ONYX_TLMM_SIZE);
	onyx_gcc = ml_io_map(ONYX_GCC_PHYS, ONYX_GCC_SIZE);
	if (!onyx_tlmm || !onyx_gcc) {
		printf("onyx-touch: cannot map TLMM/GCC\n");
		return;
	}
	if (!onyx_clocks_on()) {
		return;
	}
	onyx_se = ml_io_map(ONYX_SE4_PHYS, ONYX_SE_SIZE);
	if (!onyx_se) {
		printf("onyx-touch: cannot map QUPv3 SE4\n");
		return;
	}
	printf("onyx-touch: SE4 firmware %#x interface %#x parameters %#x source %u Hz select %u\n",
	    onyx_touch_rd(GENI_FW_REVISION_RO(onyx_se)), onyx_touch_rd(GENI_IF_DISABLE_RO(onyx_se)),
	    onyx_touch_rd(SE_HW_PARAM_0(onyx_se)), onyx_se_source_hz, onyx_se_clock_select);
	problem = onyx_geni_init(1000000);
	if (problem != NULL) {
		printf("onyx-touch: %s\n", problem);
		return;
	}
	/* Tuna pinctrl assigns QUP1 SE4 to function 1 on GPIO16..19. */
	onyx_gpio_configure(ONYX_PIN_MISO, ONYX_PIN_FUNCTION_SPI, false);
	onyx_gpio_configure(ONYX_PIN_MOSI, ONYX_PIN_FUNCTION_SPI, false);
	onyx_gpio_configure(ONYX_PIN_CLK, ONYX_PIN_FUNCTION_SPI, false);
	onyx_gpio_configure(ONYX_PIN_CS, ONYX_PIN_FUNCTION_SPI, false);
	onyx_gpio_configure(ONYX_PIN_IRQ, 0, false);
	printf("onyx-touch: NT38771 GENI polling enabled at 1 MHz; irq=%u\n", onyx_gpio_get(ONYX_PIN_IRQ));
	for (;;) {
		bool asserted = !onyx_gpio_get(ONYX_PIN_IRQ);

		/* Read immediately, on IRQ, and every 80 ms in case the edge/level is missed. */
		if (asserted || reads == 0 || ++idle >= 10) {
			idle = 0;
			if (onyx_nvt_read(ONYX_NVT_EVENT, report, sizeof(report))) {
				if (reads++ == 0) {
					printf("onyx-touch: first report irq=%u data %02x %02x %02x %02x %02x %02x %02x %02x\n",
					    asserted, report[1], report[2], report[3], report[4], report[5], report[6], report[7], report[8]);
				}
				onyx_touch_report(report);
			}
		}
		delay_for_interval(8000, NSEC_PER_USEC);
	}
}

static void
onyx_touch_probe_thread(__unused void *parameter, __unused wait_result_t result)
{
	int mmio = 0, run = 0, geni = 0, gpi_probe = 0;
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
	if (PE_parse_boot_argn("onyx_touch_gpi_probe", &gpi_probe, sizeof(gpi_probe)) && gpi_probe == 1) {
		onyx_gpi_probe();
		return;
	}
	if (PE_parse_boot_argn("onyx_touch_geni", &geni, sizeof(geni)) && geni == 1) {
		onyx_touch_run();
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
