/* Onyx Gen8 kernel integration. Stage 1 validates firmware/queues in CPU memory;
 * stage 2 reads the Tuna GPU clock controller. Neither writes hardware or
 * accesses the GMU, 3D engine or GPU SMMU.
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
	if (!PE_parse_boot_argn("onyx_gpu_stage", &stage, sizeof(stage)) || stage < 1 || stage > 4) return;
	if (kernel_thread_start(onyx_gen8_validate, (void *)(uintptr_t)stage, &thread) == KERN_SUCCESS)
		thread_deallocate(thread);
}
STARTUP(EARLY_BOOT, STARTUP_RANK_LAST, onyx_gen8_init);
