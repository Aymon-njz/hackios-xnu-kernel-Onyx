#ifndef ONYX_GMU_SCM_H
#define ONYX_GMU_SCM_H
#include <stdint.h>
/* Serialized SMC64 SIP transport. Caller establishes firmware handoff and
 * supplies SMCCC-compliant invocation. r[6] carries Qualcomm continuation.
 * No global firmware waitqueue support: asynchronous results quarantine it.
 */
struct onyx_scm {
 void *context;
 void (*invoke)(void *, uint64_t r[8]);
 unsigned owned, quarantined;
 int64_t firmware_status;
};
enum onyx_scm_result { ONYX_SCM_OK, ONYX_SCM_ARGUMENT, ONYX_SCM_UNAVAILABLE,
 ONYX_SCM_ERROR, ONYX_SCM_ASYNC, ONYX_SCM_INTERRUPTED_LIMIT };
int onyx_gmu_secure_cx(struct onyx_scm *, unsigned feature_requests);
/* Actual AArch64 SMC instruction; never invoked by host tests. */
void onyx_scm_arm64_invoke(void *, uint64_t r[8]);
#endif
