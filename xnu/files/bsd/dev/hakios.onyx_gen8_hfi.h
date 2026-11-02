/* Gen8 HFI wire layout: SM8735 KGSL adreno_hfi.h and
 * adreno_gen8_hwsched_hfi.c, revision 8500748dae32f10507c02f1c94bc971553cb525b.
 * Host-side queue component only; this does not start firmware or ring MMIO.
 * Requires coherent or uncached shared memory, little-endian CPU, a single
 * producer and consumer per queue, and externally serialized host accesses.
 * Cacheable noncoherent memory requires cache maintenance not provided here. */
#ifndef ONYX_GEN8_HFI_H
#define ONYX_GEN8_HFI_H
#include <stddef.h>
#include <stdint.h>

#define ONYX_HFI_QUEUES 3u
#define ONYX_HFI_HEADER_SLOTS 17u
#define ONYX_HFI_QUEUE_WORDS 1024u
#define ONYX_HFI_DATA_OFFSET 848u
/* Match KGSL's allocation including its trailing alignment allowance. */
#define ONYX_HFI_SHARED_BYTES 13144u

struct onyx_hfi_qheader {
	uint32_t status, start_addr, type, queue_size, msg_size, reserved[5];
	uint32_t read_index, write_index;
};
struct onyx_hfi_table {
	uint32_t version, size, header_offset, header_size, queues, active;
	struct onyx_hfi_qheader queue[ONYX_HFI_HEADER_SLOTS];
};
struct onyx_hfi {
	void *shared;
	uint32_t gmu_address;
};
enum onyx_hfi_status {
	ONYX_HFI_OK, ONYX_HFI_ARGUMENT, ONYX_HFI_CORRUPT,
	ONYX_HFI_FULL, ONYX_HFI_EMPTY, ONYX_HFI_INCOMPLETE, ONYX_HFI_CAPACITY
};

/* Initializes command/message/debug queues before the GMU can access them.
 * Other 14 header slots stay disabled. GMU address is a virtual mapping. */
enum onyx_hfi_status onyx_hfi_init(struct onyx_hfi *hfi, void *shared,
    size_t bytes, uint32_t gmu_address);
/* Send only on host-to-firmware command queue 0. Header size must match words.
 * Caller rings the hardware doorbell only after this returns OK. */
enum onyx_hfi_status onyx_hfi_send(struct onyx_hfi *hfi,
    const uint32_t *message, size_t words);
/* Receive only on firmware-to-host message/debug queues 1 and 2.
 * Errors leave the consumer index and output buffer unchanged. */
enum onyx_hfi_status onyx_hfi_receive(struct onyx_hfi *hfi, unsigned queue,
    uint32_t *message, size_t capacity, size_t *words);

struct onyx_gen8_ib {
	uint64_t address; /* GPU virtual address, not a CPU pointer */
	uint32_t bytes;
};
/* Build an inline H2F_MSG_ISSUE_CMD packet. No indirect list, profiling or
 * sync objects are supported yet. This encodes bytes but does not validate
 * that GPU addresses are mapped, register a context, or submit a job. */
enum onyx_hfi_status onyx_gen8_submit_packet(uint32_t context, uint32_t timestamp,
    unsigned sequence, const struct onyx_gen8_ib *ibs, size_t count,
    uint32_t *packet, size_t capacity, size_t *words);

struct onyx_gen8_context_header {
	uint32_t version, start_addr, queue_size, out_fence_ts, sync_obj_ts;
	uint32_t read_index, write_index, fence_address, fence_bytes, reserved[3];
};
struct onyx_gen8_context_queue {
	void *shared;
	uint32_t gmu_address, context;
};
/* Prepare a 4 KiB context queue; actual GMU mapping and HFI context
 * registration/acknowledgement must precede any hardware notification. */
enum onyx_hfi_status onyx_gen8_context_init(struct onyx_gen8_context_queue *queue,
    void *shared, size_t bytes, uint32_t gmu_address, uint32_t context);
/* Enqueue a validated ISSUE_CMD packet into CPU shared memory. No hardware
 * doorbell is issued. Caller owns mapping, cache policy, lifetime and fences. */
enum onyx_hfi_status onyx_gen8_context_enqueue(struct onyx_gen8_context_queue *queue,
    const uint32_t *packet, size_t words);
#endif
