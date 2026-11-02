#include "hakios.onyx_gen8_hfi.h"

_Static_assert(sizeof(struct onyx_hfi_qheader) == 48, "HFI queue ABI");
_Static_assert(sizeof(struct onyx_hfi_table) == 840, "HFI table ABI");
_Static_assert(offsetof(struct onyx_hfi_qheader, read_index) == 40, "HFI index ABI");

static void shared_barrier(void)
{
#if defined(__aarch64__)
	/* GMU is a device, not another CPU in the inner-shareable domain. */
	__asm__ volatile ("dsb sy" ::: "memory");
#else
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
#endif
}

static uint32_t *ring(struct onyx_hfi *hfi, unsigned queue)
{
	return (uint32_t *)((uint8_t *)hfi->shared + ONYX_HFI_DATA_OFFSET + queue * 4096);
}

enum onyx_hfi_status
onyx_hfi_init(struct onyx_hfi *hfi, void *shared, size_t bytes, uint32_t address)
{
	struct onyx_hfi_table *table = shared;
	if (!hfi || !shared || ((uintptr_t)shared & 15) || (address & 15) ||
	    bytes < ONYX_HFI_SHARED_BYTES || address > UINT32_MAX - ONYX_HFI_SHARED_BYTES)
		return ONYX_HFI_ARGUMENT;
	for (size_t n = 0; n < ONYX_HFI_SHARED_BYTES / 4; n++) ((uint32_t *)shared)[n] = 0;
	table->size = 210; /* 840 bytes, including all 17 ABI header slots */
	table->header_offset = 6;
	table->header_size = 12;
	table->queues = table->active = ONYX_HFI_QUEUES;
	for (unsigned q = 0; q < ONYX_HFI_QUEUES; q++) {
		table->queue[q].status = 1;
		table->queue[q].start_addr = address + ONYX_HFI_DATA_OFFSET + q * 4096;
		table->queue[q].type = q;
		table->queue[q].queue_size = ONYX_HFI_QUEUE_WORDS;
	}
	shared_barrier();
	hfi->shared = shared;
	hfi->gmu_address = address;
	return ONYX_HFI_OK;
}

static enum onyx_hfi_status
indices(struct onyx_hfi *hfi, unsigned queue, uint32_t *read, uint32_t *write)
{
	if (!hfi || !hfi->shared || queue >= ONYX_HFI_QUEUES) return ONYX_HFI_ARGUMENT;
	struct onyx_hfi_qheader *q = &((struct onyx_hfi_table *)hfi->shared)->queue[queue];
	if (__atomic_load_n(&q->status, __ATOMIC_ACQUIRE) != 1 ||
	    q->queue_size != ONYX_HFI_QUEUE_WORDS || q->msg_size != 0 || q->type != queue ||
	    q->start_addr != hfi->gmu_address + ONYX_HFI_DATA_OFFSET + queue * 4096)
		return ONYX_HFI_CORRUPT;
	*read = __atomic_load_n(&q->read_index, __ATOMIC_ACQUIRE);
	*write = __atomic_load_n(&q->write_index, __ATOMIC_ACQUIRE);
	shared_barrier();
	if (*read >= ONYX_HFI_QUEUE_WORDS || *write >= ONYX_HFI_QUEUE_WORDS ||
	    (*read & 3) || (*write & 3)) return ONYX_HFI_CORRUPT;
	return ONYX_HFI_OK;
}

enum onyx_hfi_status
onyx_hfi_send(struct onyx_hfi *hfi, const uint32_t *message, size_t words)
{
	uint32_t read, write;
	if (!message || !words || words > 255 || ((message[0] >> 8) & 255) != words)
		return ONYX_HFI_ARGUMENT;
	enum onyx_hfi_status status = indices(hfi, 0, &read, &write);
	if (status != ONYX_HFI_OK) return status;
	uint32_t padded = ((uint32_t)words + 3) & ~3u;
	uint32_t used = (write + ONYX_HFI_QUEUE_WORDS - read) % ONYX_HFI_QUEUE_WORDS;
	/* Leave an empty slot so equal indices always mean empty. */
	if (ONYX_HFI_QUEUE_WORDS - used <= padded) return ONYX_HFI_FULL;
	uint32_t *data = ring(hfi, 0);
	for (uint32_t n = 0; n < padded; n++)
		data[(write + n) % ONYX_HFI_QUEUE_WORDS] = n < words ? message[n] : 0xfafafafa;
	struct onyx_hfi_qheader *q = &((struct onyx_hfi_table *)hfi->shared)->queue[0];
	shared_barrier();
	__atomic_store_n(&q->write_index, (write + padded) % ONYX_HFI_QUEUE_WORDS, __ATOMIC_RELEASE);
	/* Publish the index before a caller rings a device doorbell. */
	shared_barrier();
	return ONYX_HFI_OK;
}

enum onyx_hfi_status
onyx_hfi_receive(struct onyx_hfi *hfi, unsigned queue, uint32_t *message,
    size_t capacity, size_t *words)
{
	uint32_t read, write;
	if (!words) return ONYX_HFI_ARGUMENT;
	*words = 0;
	if (!message || queue < 1 || queue > 2) return ONYX_HFI_ARGUMENT;
	enum onyx_hfi_status status = indices(hfi, queue, &read, &write);
	if (status != ONYX_HFI_OK) return status;
	if (read == write) return ONYX_HFI_EMPTY;
	uint32_t *data = ring(hfi, queue);
	uint32_t length = (data[read] >> 8) & 255;
	if (!length) return ONYX_HFI_CORRUPT;
	uint32_t padded = (length + 3) & ~3u;
	uint32_t available = (write + ONYX_HFI_QUEUE_WORDS - read) % ONYX_HFI_QUEUE_WORDS;
	if (padded > available) return ONYX_HFI_INCOMPLETE;
	if (length > capacity) return ONYX_HFI_CAPACITY;
	for (uint32_t n = 0; n < length; n++) message[n] = data[(read + n) % ONYX_HFI_QUEUE_WORDS];
	struct onyx_hfi_qheader *q = &((struct onyx_hfi_table *)hfi->shared)->queue[queue];
	/* Finish consuming payload before allowing firmware to reuse it. */
	shared_barrier();
	__atomic_store_n(&q->read_index, (read + padded) % ONYX_HFI_QUEUE_WORDS, __ATOMIC_RELEASE);
	*words = length;
	return ONYX_HFI_OK;
}
