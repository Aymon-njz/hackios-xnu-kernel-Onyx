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

static uint8_t hfi_check_memory[ONYX_HFI_SHARED_BYTES] __attribute__((aligned(16)));

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);

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
	if (!gpucc) {
		printf("onyx-gen8: cannot map GPUCC; probe stopped\n");
		return;
	}
	for (unsigned n = 0; n < sizeof(registers) / sizeof(registers[0]); n++) {
		printf("onyx-gen8: GPUCC before read %s offset %#x\n", registers[n].name, registers[n].offset);
		uint32_t value = *(volatile uint32_t *)(gpucc + registers[n].offset);
		printf("onyx-gen8: GPUCC %s = %#x\n", registers[n].name, value);
	}
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
	if (!PE_parse_boot_argn("onyx_gpu_stage", &stage, sizeof(stage)) || (stage != 1 && stage != 2)) return;
	if (kernel_thread_start(onyx_gen8_validate, (void *)(uintptr_t)stage, &thread) == KERN_SUCCESS)
		thread_deallocate(thread);
}
STARTUP(EARLY_BOOT, STARTUP_RANK_LAST, onyx_gen8_init);
