/* Onyx Gen8 kernel integration. Stages 1..4 validate/discover without writes.
 * Stages 5/6 test CX power and clocks; stage 7 optionally reads the GMUCX AO counter.
 * No stage currently starts the GMU, 3D engine or GPU SMMU.
 * The existing no-GPU stubs stay in force until a real backend is implemented. */
#include <sys/systm.h>
#include <kern/startup.h>
#include <kern/thread.h>
#include <kern/clock.h>
#include <pexpert/pexpert.h>
#include <sys/hakios.input.h>
#include <mach/mach_types.h>
#include "hakios.onyx_gen8_firmware.h"
#include "hakios.onyx_gen8_hfi.h"
#include "hakios.onyx_gen8_power.h"
#include "hakios.onyx_rpmh.h"

static uint8_t hfi_check_memory[ONYX_HFI_SHARED_BYTES] __attribute__((aligned(16)));

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);

static uint32_t onyx_gpu_mmio_read(void *context, uint32_t offset)
{
	__asm__ volatile ("dsb sy" ::: "memory");
	uint32_t value = *(volatile uint32_t *)((vm_offset_t)context + offset);
	__asm__ volatile ("dsb sy" ::: "memory");
	return value;
}
static void onyx_gpu_mmio_write(void *context, uint32_t offset, uint32_t value)
{
	*(volatile uint32_t *)((vm_offset_t)context + offset) = value;
	__asm__ volatile ("dsb sy" ::: "memory");
}
static void onyx_gpu_mmio_wait(__unused void *context, unsigned microseconds)
{
	delay_for_interval(microseconds, NSEC_PER_USEC);
}

static void onyx_gen8_power_test(unsigned stage)
{
	int enabled = 0, cpus = 0, native = 0;
	uint32_t bootcpu;
	if (!PE_parse_boot_argn("onyx_gpu_power", &enabled, sizeof(enabled)) || enabled != 1 ||
	    !PE_parse_boot_argn("cpus", &cpus, sizeof(cpus)) || cpus != 1 ||
	    !PE_parse_boot_argn("ufs_native", &native, sizeof(native)) || native != 4 ||
	    PE_parse_boot_argn("bootcpu", &bootcpu, sizeof(bootcpu))) {
		printf("onyx-gen8: power test refused: requires explicit power=1 and single-CPU loaderless mode\n");
		return;
	}
	/* The test takes over the APPS-owned DRV2 after the primary-core
	 * loaderless handoff. This is experimental: stable readback cannot prove
	 * hypervisor ownership. No live UEFI mailbox/firmware CPU is supported. */
	vm_offset_t rsc = ml_io_map(0x17a20000, 0x10000);
	vm_offset_t database = ml_io_map(0x81c60000, 0x20000);
	vm_offset_t gpucc = ml_io_map(0x03d90000, 0x9800);
	if (!rsc || !database || !gpucc) { printf("onyx-gen8: power mapping failed\n"); return; }
	struct onyx_rsc_io rio = { (void *)rsc, onyx_gpu_mmio_read, onyx_gpu_mmio_write, onyx_gpu_mmio_wait, 0, 0 };
	struct onyx_rsc_io gio = { (void *)gpucc, onyx_gpu_mmio_read, onyx_gpu_mmio_write, onyx_gpu_mmio_wait, 0, 0 };
	uint32_t version = rio.read(rio.context, 0), config = rio.read(rio.context, 0xc);
	if (version != 0x30800 || config != 0x800c8104 || (rio.read(rio.context, 4) & (1u << 24))) {
		printf("onyx-gen8: power refused: RSC identity/configuration changed\n"); return;
	}
	/* Require the observed empty slot and firmware command to stay unchanged
	 * for 10 ms. The transport revalidates state immediately before writing. */
	static const uint32_t offsets[] = {
		0xd24, 0xd28, 0xd2c, 0xd20, 0xd34, 0xd38, 0xd3c, 0xd40,
		0xfc4, 0xfc8, 0xfcc, 0xfc0, 0x1264, 0x1268, 0x126c, 0x1260
	};
	uint32_t snapshot[sizeof(offsets) / sizeof(offsets[0])];
	for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++) snapshot[i] = rio.read(rio.context, offsets[i]);
	for (unsigned sample = 0; sample < 100; sample++) {
		onyx_gpu_mmio_wait(NULL, 100);
		for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++)
			if (snapshot[i] != rio.read(rio.context, offsets[i])) {
				printf("onyx-gen8: RSC changed during takeover check; no power writes\n"); return;
			}
	}
	struct onyx_rpmh_resource rails[2];
	const char *names[] = { "cx.lvl", "mx.lvl" };
	for (unsigned n = 0; n < 2; n++) {
		if (onyx_rpmh_lookup((const void *)database, 0x20000, names[n], &rails[n]) ||
		    rails[n].address != (n ? 0x30010u : 0x30000u) ||
		    rails[n].count != (n ? 13u : 10u) ||
		    rails[n].levels[rails[n].count - 1] != (n ? 0x1c0u : 0x1b0u)) {
			printf("onyx-gen8: power refused: rail inventory changed\n"); return;
		}
	}
	rio.owned = 1;
	for (unsigned n = 0; n < 2; n++) {
		/* Diagnostic only: use the highest advertised level to avoid lowering
		 * an unknown existing vote. Leave the vote in place until reboot;
		 * lowering it without a full vote ledger would be incorrect. */
		unsigned level = rails[n].levels[rails[n].count - 1];
		printf("onyx-gen8: before experimental RPMh %s vote level %#x\n", names[n], level);
		enum onyx_rsc_result result = onyx_rsc_vote(&rio, version, 1, &rails[n], level, level);
		printf("onyx-gen8: RPMh %s vote status %u\n", names[n], (unsigned)result);
		if (result != ONYX_RSC_OK) return;
	}
	gio.owned = 1;
	uint32_t saved = 0;
	printf("onyx-gen8: before CX power-on; GMU and GX remain untouched\n");
	enum onyx_cx_result result = onyx_gen8_cx_enable(&gio, 1, &saved);
	printf("onyx-gen8: CX power status %u saved GDSCR %#x readback %#x\n",
	    (unsigned)result, saved, gio.read(gio.context, 0x9080));
	if (stage >= 6 && result == ONYX_CX_OK) {
		printf("onyx-gen8: before GMU/HUB TCXO clock setup; no firmware start\n");
		struct onyx_clock_trace trace;
		enum onyx_clock_result clock_result = onyx_gen8_clocks_tcxo(&gio, &trace);
		printf("onyx-gen8: clock check phase %u offset %#x mask %#x expected %#x observed %#x (before rollback)\n",
		    trace.phase, trace.offset, trace.mask, trace.expected, trace.observed);
		printf("onyx-gen8: non-echoing branch requests %#x always-on requests %#x; status checks do not prove GMU execution\n",
		    trace.unlatched_branches, trace.unlatched_aon);
		printf("onyx-gen8: GMU/HUB clock result %u quarantined %u\n", (unsigned)clock_result, gio.quarantined);
		static const uint32_t offsets[] = { 0x9318, 0x931c, 0x93ec, 0x93f0, 0x90d4, 0x90e4, 0x90e8, 0x90bc, 0x93a4, 0x9008, 0x93a8, 0x9004, 0x90cc, 0x9000 };
		for (unsigned n = 0; n < sizeof(offsets) / sizeof(offsets[0]); n++)
			printf("onyx-gen8: clock offset %#x readback %#x\n", offsets[n], gio.read(gio.context, offsets[n]));
        int access = 0;
        if (stage == 7 && clock_result == ONYX_CLOCK_OK && !gio.quarantined &&
            PE_parse_boot_argn("onyx_gmu_access", &access, sizeof(access)) && access == 1) {
            /* Recovery FDT: GMU 0x03d37000/0x68000. Stock GPU-relative
             * word 0x1f840 maps to 0x03d7e100. No reset/firmware writes. */
            printf("onyx-gen8: next GMUCX AO counter read at 0x03d7e100; saving pre-access log first\n");
            delay_for_interval(35, NSEC_PER_SEC);
            uint32_t missing = onyx_gen8_power_missing(gio.read(gio.context, 0x9080),
                gio.read(gio.context, 0x90d4), gio.read(gio.context, 0x90e4),
                gio.read(gio.context, 0x90e8), gio.read(gio.context, 0x9318));
            if (missing) {
                printf("onyx-gen8: GMUCX read skipped: power/clock state changed (%#x)\n", missing);
                return;
            }
            vm_offset_t control = ml_io_map(0x03d7e000, 0x1000);
            if (control) {
                struct onyx_rsc_io counter_io = { (void *)control,
                    onyx_gpu_mmio_read, NULL, onyx_gpu_mmio_wait, 1, 0 };
                uint64_t before = 0, after = 0;
                enum onyx_counter_result sample = onyx_gen8_counter_sample(&counter_io, &before, &after);
                printf("onyx-gen8: GMUCX counter result %u before %#llx after %#llx delta %llu; no CM3 firmware execution claimed\n",
                    (unsigned)sample, before, after, after >= before ? after - before : 0);
            } else printf("onyx-gen8: GMUCX control page mapping failed\n");
        }

	}
	printf("onyx-gen8: power test stopped; no GMU firmware boot, GPU work or Metal acceleration\n");
}

static void onyx_gen8_probe_supplies(void)
{
	/* Recovery FDT's reserved aop_cmd_db_region, read as memory. */
	vm_offset_t database = ml_io_map(0x81c60000, 0x20000);
	static const char *names[] = { "cx.lvl", "mx.lvl", "gfx.lvl" };
	if (!database) { printf("onyx-gen8: cannot map command DB\n"); return; }
	for (unsigned n = 0; n < 3; n++) {
		struct onyx_rpmh_resource resource = { 0 };
		int error = onyx_rpmh_lookup((const void *)database, 0x20000, names[n], &resource);
		printf("onyx-gen8: RPMh %s lookup status %d address %#x levels %u\n",
		    names[n], error, resource.address, resource.count);
		if (!error) for (unsigned i = 0; i < resource.count; i++)
			printf("onyx-gen8: RPMh %s index %u voltage-level %#x\n", names[n], i, resource.levels[i]);
	}
	printf("onyx-gen8: supply inventory complete; no voltage votes issued\n");
}

static void onyx_gen8_probe_rsc(void)
{
	/* FDT selects APPS RSC DRV2. Only read its identity and active TCSs.
	 * qcom,tcs-config lists ACTIVE first, with three slots at IDs 0..2. */
	printf("onyx-gen8: APPS RSC DRV2 read-only inventory begins\n");
	vm_offset_t base = ml_io_map(0x17a20000, 0x10000);
	if (!base) { printf("onyx-gen8: RSC mapping failed\n"); return; }
	uint32_t version = *(volatile uint32_t *)base;
	struct onyx_rsc_layout layout;
	int error = onyx_rsc_layout(version, &layout);
	printf("onyx-gen8: RSC version %#x layout status %d\n", version, error);
	if (error) return;
	/* Stock v3 reads solver capability at +4; v2.7 defines this at +0.
	 * A solver-capable controller needs the firmware/PM handoff path rather
	 * than assuming an unused active TCS is ours to trigger. */
	uint32_t solver = *(volatile uint32_t *)(base + (((version >> 16) & 255) == 3 ? 4 : 0));
	printf("onyx-gen8: RSC solver config %#x capability %u; voltage writes remain blocked\n",
	    solver, !!(solver & (1u << 24)));
	uint32_t config = *(volatile uint32_t *)(base + 0x0c);
	unsigned slots = (config >> 12) & 63, commands = (config >> 27) & 31;
	printf("onyx-gen8: RSC DRV2 config %#x TCSs %u commands-per-TCS %u\n", config, slots, commands);
	if (slots < 3 || !commands) return;
	for (unsigned n = 0; n < 3; n++) {
		vm_offset_t tcs = base + 0xd00 + n * 0x2a0;
		printf("onyx-gen8: before RSC active TCS %u reads\n", n);
		uint32_t control = *(volatile uint32_t *)(tcs + layout.control);
		uint32_t status = *(volatile uint32_t *)(tcs + layout.status);
		uint32_t enable = *(volatile uint32_t *)(tcs + layout.enable);
		uint32_t wait = *(volatile uint32_t *)(tcs + layout.wait);
		printf("onyx-gen8: RSC TCS %u control %#x status %#x enabled %#x wait %#x\n",
		    n, control, status, enable, wait);
		for (unsigned command = 0; command < commands && command < 16; command++) {
			if (!(enable & (1u << command))) continue;
			uint32_t at = command * layout.stride;
			printf("onyx-gen8: before RSC TCS %u command %u read\n", n, command);
			uint32_t message = *(volatile uint32_t *)(tcs + layout.message + at);
			uint32_t address = *(volatile uint32_t *)(tcs + layout.address + at);
			uint32_t data = *(volatile uint32_t *)(tcs + layout.data + at);
			uint32_t command_status = *(volatile uint32_t *)(tcs + layout.command_status + at);
			printf("onyx-gen8: RSC TCS %u command %u message %#x resource %#x data %#x status %#x\n",
			    n, command, message, address, data, command_status);
		}
	}
	printf("onyx-gen8: RSC inventory complete; no ownership claimed or requests issued\n");
}

/* FDT: qcom,tuna-gpucc at 0x3d90000/0x9800. Register offsets from
 * onyx-oss/konoha-kernel-gki d6b71af752d00499a238ceff5bab30592cc8d0a8,
 * drivers/clk/qcom/gpucc-tuna.c. No inherited SC7280 offsets are used.
 * This is an opt-in controller probe, not a GMU power-on sequence. */
static void
onyx_gen8_probe_gpucc(void)
{
	static const struct { uint32_t offset; const char *name; } registers[] = {
		{ 0x9008, "cxo_aon_cbcr" },
		{ 0x9080, "cx_gdscr" },
		{ 0x9094, "cx_hw_control" },
		{ 0x90bc, "ahb_cbcr" },
		{ 0x90d4, "cx_gmu_cbcr" },
		{ 0x90e4, "cxo_cbcr" },
		{ 0x90e8, "hub_cx_int_cbcr" },
		{ 0x9318, "gmu_rcg_command" },
		{ 0x931c, "gmu_rcg_config" },
		{ 0x93e8, "hub_aon_cbcr" },
		{ 0x93ec, "hub_rcg_command" },
		{ 0x93f0, "hub_rcg_config" },
	};
	printf("onyx-gen8: GPUCC read-only probe begins; no GMU/GPU MMIO or writes\n");
	vm_offset_t gpucc = ml_io_map(0x03d90000, 0x9800);
	uint32_t values[sizeof(registers) / sizeof(registers[0])] = { 0 };
	if (!gpucc) {
		printf("onyx-gen8: cannot map GPUCC; probe stopped\n");
		return;
	}
	for (unsigned n = 0; n < sizeof(registers) / sizeof(registers[0]); n++) {
		printf("onyx-gen8: GPUCC before read %s offset %#x\n", registers[n].name, registers[n].offset);
		uint32_t value = *(volatile uint32_t *)(gpucc + registers[n].offset);
		values[n] = value;
		printf("onyx-gen8: GPUCC %s = %#x\n", registers[n].name, value);
	}
	uint32_t missing = onyx_gen8_power_missing(values[1], values[4], values[5], values[6], values[7]);
	printf("onyx-gen8: power prerequisites mask %#x: CX off=%u collapsed=%u GMU clock off=%u CXO off=%u HUB off=%u GMU RCG off=%u\n",
	    missing, !!(missing & ONYX_CX_OFF), !!(missing & ONYX_CX_COLLAPSED),
	    !!(missing & ONYX_GMU_CLOCK_OFF), !!(missing & ONYX_CXO_CLOCK_OFF),
	    !!(missing & ONYX_HUB_CLOCK_OFF), !!(missing & ONYX_GMU_RCG_OFF));
	printf("onyx-gen8: startup blocked: regulator/RPMh, secure initialization and GPU/GMU mappings not implemented\n");
	printf("onyx-gen8: GPUCC probe complete; power and clocks unchanged; GPU is not started\n");
}

static void
onyx_gen8_validate(void *parameter, __unused wait_result_t result)
{
	struct onyx_gen8_fw_block blocks[64];
	struct onyx_hfi hfi = { 0 };
	uint64_t bytes = 0;
	size_t count = 0;
	uint32_t command[] = { 0x20e, 0 }; /* CORE_FW_START wire shape, not sent to hardware */
	delay_for_interval(30, NSEC_PER_SEC);
	if ((uintptr_t)parameter >= 5 && (uintptr_t)parameter <= 7) {
		onyx_gen8_power_test((unsigned)(uintptr_t)parameter);
		return;
	}
	if ((uintptr_t)parameter == 3) {
		onyx_gen8_probe_supplies();
		return;
	}
	if ((uintptr_t)parameter == 4) {
		onyx_gen8_probe_supplies();
		onyx_gen8_probe_rsc();
		return;
	}
	if ((uintptr_t)parameter == 2) {
		onyx_gen8_probe_gpucc();
		return;
	}
	printf("onyx-gen8: kernel-core-20261102; validation only; GPU is not started\n");
	/* This address only exercises queue ABI arithmetic in CPU memory.
	 * It is not a physical mapping or a programmed GMU allocation. */
	if (onyx_hfi_init(&hfi, hfi_check_memory, sizeof(hfi_check_memory), 0x60002000) != ONYX_HFI_OK ||
	    onyx_hfi_send(&hfi, command, 2) != ONYX_HFI_OK) {
		printf("onyx-gen8: CPU queue check failed\n");
		return;
	}
	printf("onyx-gen8: CPU queue ABI check passed; no hardware doorbell issued\n");
	struct onyx_gen8_context_queue context_queue = { 0 };
	struct onyx_gen8_ib ib = { 0x100000, 4 }; /* test address only; no GPU mapping */
	uint32_t packet[11];
	if (onyx_gen8_submit_packet(1, 1, 1, &ib, 1, packet, 11, &count) != ONYX_HFI_OK ||
	    onyx_gen8_context_init(&context_queue, hfi_check_memory, 4096, 0x60002000, 1) != ONYX_HFI_OK ||
	    onyx_gen8_context_enqueue(&context_queue, packet, count) != ONYX_HFI_OK) {
		printf("onyx-gen8: CPU submission ABI check failed\n");
		return;
	}
	printf("onyx-gen8: CPU submission ABI check passed; no GPU job dispatched\n");
	const uint8_t *image = hakios_image_file("fw", "gen80600_gmu.bin", &bytes, NULL);
	if (!image || !bytes) {
		printf("onyx-gen8: no preloaded fw/gen80600_gmu.bin; firmware validation unavailable\n");
		return;
	}
	enum onyx_gen8_fw_status status = onyx_gen8_fw_plan(image, (size_t)bytes, blocks, 64, &count);
	printf("onyx-gen8: GMU firmware validation status %u blocks %lu; hardware startup unavailable\n",
	    (unsigned)status, (unsigned long)count);
}

static void
onyx_gen8_init(void)
{
	int stage = 0;
	thread_t thread;
	if (!PE_parse_boot_argn("onyx_gpu_stage", &stage, sizeof(stage)) || stage < 1 || stage > 7) return;
	if (kernel_thread_start(onyx_gen8_validate, (void *)(uintptr_t)stage, &thread) == KERN_SUCCESS)
		thread_deallocate(thread);
}
STARTUP(EARLY_BOOT, STARTUP_RANK_LAST, onyx_gen8_init);
