/* Prepare real firmware/HFI backing in a private, unattached GMU domain.
 * Allocator supplies zeroable CPU memory + contiguous 40-bit physical pages.
 * The flush callback must publish both data and translation tables to DMA.
 * No CM3 reset, MMIO, context-bank attachment or doorbell occurs here.
 */
#ifndef ONYX_GMU_MEMORY_H
#define ONYX_GMU_MEMORY_H
#include "hakios.onyx_gmu_mmu.h"
#include "hakios.onyx_gen8_firmware.h"
#include "hakios.onyx_gen8_hfi.h"
#define ONYX_GMU_MEMORY_MAX 16
struct onyx_gmu_backing { uint32_t va, bytes; void *cpu; uint64_t pa; unsigned flags; };
struct onyx_gmu_allocator {
 void *context;
 int (*allocate)(void *, size_t, void **, uint64_t *);
 void (*release)(void *, void *, size_t);
 void (*publish)(void *, void *, size_t);
};
struct onyx_gmu_memory {
 struct onyx_gmu_backing region[ONYX_GMU_MEMORY_MAX];
 size_t count;
 struct onyx_hfi hfi;
};
enum onyx_gmu_memory_result { ONYX_GMU_MEMORY_OK, ONYX_GMU_MEMORY_ARGUMENT,
 ONYX_GMU_MEMORY_FIRMWARE, ONYX_GMU_MEMORY_LAYOUT, ONYX_GMU_MEMORY_ALLOCATE,
 ONYX_GMU_MEMORY_MAP, ONYX_GMU_MEMORY_HFI };
int onyx_gmu_memory_prepare(struct onyx_gmu_memory *, struct onyx_gmu_mmu *,
 const struct onyx_gmu_allocator *, const void *, size_t);
/* Caller must detach/quiesce a live domain before freeing its backing. */
void onyx_gmu_memory_release(struct onyx_gmu_memory *, const struct onyx_gmu_allocator *);
#endif
