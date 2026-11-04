/* Private, unattached AArch64 stage-1 tables for the GMU SMMUv2 domain.
 * 32-bit IOVA, 40-bit PA, 4 KiB pages, L1->L2->L3. No MMIO or allocation.
 * MAIR slot 0: normal non-cacheable (0x44); slot 1: normal WB (0xff). PRIV matches stock IOMMU_PRIV.
 * Caller must flush tables/backing and verify stream/context ownership before
 * installing TTBR/TCR/MAIR. A successful map does not attach a hardware domain.
 */
#ifndef ONYX_GMU_MMU_H
#define ONYX_GMU_MMU_H
#include <stdint.h>
#include <stddef.h>
#define ONYX_GMU_PAGE 4096u
#define ONYX_GMU_READ 1u
#define ONYX_GMU_WRITE 2u
#define ONYX_GMU_EXEC 4u
#define ONYX_GMU_WB 8u
#define ONYX_GMU_PRIV 16u
struct onyx_gmu_mmu { uint64_t *tables; uint64_t physical; size_t pages, used; };
enum onyx_gmu_map_result { ONYX_GMU_MAP_OK, ONYX_GMU_MAP_ARGUMENT,
    ONYX_GMU_MAP_CONFLICT, ONYX_GMU_MAP_CAPACITY, ONYX_GMU_MAP_CORRUPT };
int onyx_gmu_mmu_init(struct onyx_gmu_mmu *, void *, uint64_t, size_t);
int onyx_gmu_mmu_map(struct onyx_gmu_mmu *, uint64_t, uint64_t, size_t, unsigned);
int onyx_gmu_mmu_lookup(const struct onyx_gmu_mmu *, uint64_t, uint64_t *, uint64_t *);
#endif
