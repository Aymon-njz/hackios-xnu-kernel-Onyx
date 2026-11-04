#ifndef ONYX_GMU_NATIVE_H
#define ONYX_GMU_NATIVE_H
#include "hakios.onyx_gmu_start.h"
#include "hakios.onyx_gmu_exchange.h"
#include "hakios.onyx_gmu_scm.h"
/* Platform-owned MMIO, acquired while CX is powered and CM3 held in reset.
 * These operations are not implemented by the allocator. Missing operations
 * fail before attachment. Call once, serialized; buffers survive all failures.
 * Firmware must remain immutable and alive from prepare through startup.
 */
struct onyx_gmu_platform {
 struct onyx_rsc_io *smmu, *gpu, *control;
 struct onyx_gmu_lease lease;
 struct onyx_scm *scm;
 unsigned secure_features;
 void *context;
 int (*verify_translation)(void *, const struct onyx_gmu_attachment *);
 int (*configure)(void *, uint32_t hfi_address, struct onyx_gmu_start_ready *);
 void (*event)(void *, const uint32_t *, size_t);
};
int onyx_gmu_native_prepare(const void *, size_t);
int onyx_gmu_native_start(const void *, size_t, struct onyx_gmu_platform *);
#endif
