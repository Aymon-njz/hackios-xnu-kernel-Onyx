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
#endif
