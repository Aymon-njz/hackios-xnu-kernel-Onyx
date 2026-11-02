/* Onyx Gen8 kernel integration. Stage 1 validates firmware and queue ABI in
 * CPU memory only. No power, clock, SMMU or GPU registers are accessed here.
 * The existing no-GPU stubs stay in force until a real backend is implemented. */
#include <sys/systm.h>
#include <kern/startup.h>
#include <kern/thread.h>
#include <kern/clock.h>
#include <pexpert/pexpert.h>
#include <sys/hakios.input.h>
#include "hakios.onyx_gen8_firmware.h"
#include "hakios.onyx_gen8_hfi.h"

static uint8_t hfi_check_memory[ONYX_HFI_SHARED_BYTES] __attribute__((aligned(16)));

static void
onyx_gen8_validate(__unused void *parameter, __unused wait_result_t result)
{
	struct onyx_gen8_fw_block blocks[64];
	struct onyx_hfi hfi = { 0 };
	uint64_t bytes = 0;
	size_t count = 0;
	uint32_t command[] = { 0x20e, 0 }; /* CORE_FW_START wire shape, not sent to hardware */
	delay_for_interval(30, NSEC_PER_SEC);
	printf("onyx-gen8: kernel-core-20261102; validation only; GPU is not started\n");
	/* This address only exercises queue ABI arithmetic in CPU memory.
	 * It is not a physical mapping or a programmed GMU allocation. */
	if (onyx_hfi_init(&hfi, hfi_check_memory, sizeof(hfi_check_memory), 0x60002000) != ONYX_HFI_OK ||
	    onyx_hfi_send(&hfi, command, 2) != ONYX_HFI_OK) {
		printf("onyx-gen8: CPU queue check failed\n");
		return;
	}
	printf("onyx-gen8: CPU queue ABI check passed; no hardware doorbell issued\n");
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
	if (!PE_parse_boot_argn("onyx_gpu_stage", &stage, sizeof(stage)) || stage != 1) return;
	if (kernel_thread_start(onyx_gen8_validate, NULL, &thread) == KERN_SUCCESS)
		thread_deallocate(thread);
}
STARTUP(EARLY_BOOT, STARTUP_RANK_LAST, onyx_gen8_init);
