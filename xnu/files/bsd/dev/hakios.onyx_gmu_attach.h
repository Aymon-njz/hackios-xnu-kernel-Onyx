#ifndef ONYX_GMU_ATTACH_H
#define ONYX_GMU_ATTACH_H
#include "hakios.onyx_rpmh.h"
#include "hakios.onyx_gmu_mmu.h"
/* A lease is supplied only after platform handoff/context ownership checks.
 * Disabled translation or an invalid SMR alone does not establish ownership.
 */
struct onyx_gmu_lease { unsigned bank, slot, handoff_verified, tables_published, cm3_reset_verified; };
struct onyx_gmu_attachment { unsigned attached, quarantined; uint64_t ttbr; };
enum onyx_attach_result { ONYX_ATTACH_OK, ONYX_ATTACH_ARGUMENT,
 ONYX_ATTACH_IDENTITY, ONYX_ATTACH_BUSY, ONYX_ATTACH_READBACK, ONYX_ATTACH_TIMEOUT };
int onyx_gmu_attach(struct onyx_rsc_io *, const struct onyx_gmu_lease *,
 const struct onyx_gmu_mmu *, struct onyx_gmu_attachment *);
#endif
