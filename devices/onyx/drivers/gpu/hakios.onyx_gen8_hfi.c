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

enum onyx_hfi_status
onyx_gen8_submit_packet(uint32_t context, uint32_t timestamp, unsigned sequence,
    const struct onyx_gen8_ib *ibs, size_t count, uint32_t *packet,
    size_t capacity, size_t *words)
{
	if (!words) return ONYX_HFI_ARGUMENT;
	*words = 0;
	/* Header carries an eight-bit word count; each inline IB takes three. */
	if (!packet || !ibs || !context || !count || count > 82 || sequence > 4095)
		return ONYX_HFI_ARGUMENT;
	size_t length = 8 + 3 * count;
	if (capacity < length) return ONYX_HFI_CAPACITY;
	for (size_t n = 0; n < count; n++) {
		if (!ibs[n].bytes || (ibs[n].bytes & 3) || (ibs[n].address & 3) ||
		    ibs[n].address > UINT64_MAX - ibs[n].bytes) return ONYX_HFI_ARGUMENT;
	}
	packet[0] = 130u | (uint32_t)length << 8 | (uint32_t)sequence << 20;
	packet[1] = context;
	packet[2] = 0; /* flags */
	packet[3] = timestamp;
	packet[4] = packet[5] = 0; /* no profiling buffer */
	packet[6] = (uint32_t)count;
	packet[7] = 0; /* no indirect IB list */
	for (size_t n = 0; n < count; n++) {
		packet[8 + 3 * n] = (uint32_t)ibs[n].address;
		packet[9 + 3 * n] = (uint32_t)(ibs[n].address >> 32);
		packet[10 + 3 * n] = ibs[n].bytes;
	}
	*words = length;
	return ONYX_HFI_OK;
}

_Static_assert(sizeof(struct onyx_gen8_context_header) == 48, "context HFI ABI");
_Static_assert(offsetof(struct onyx_gen8_context_header, read_index) == 20, "context HFI index ABI");

enum onyx_hfi_status
onyx_gen8_context_init(struct onyx_gen8_context_queue *queue, void *shared,
    size_t bytes, uint32_t address, uint32_t context)
{
	if (!queue || !shared || !context || ((uintptr_t)shared & 15) ||
	    (address & 15) || bytes != 4096 || address > UINT32_MAX - 4096)
		return ONYX_HFI_ARGUMENT;
	for (size_t n = 0; n < 1024; n++) ((uint32_t *)shared)[n] = 0;
	struct onyx_gen8_context_header *header = shared;
	header->start_addr = address + 48;
	header->queue_size = 1012; /* 4096 minus the 48-byte header, in words */
	shared_barrier();
	queue->shared = shared;
	queue->gmu_address = address;
	queue->context = context;
	return ONYX_HFI_OK;
}

enum onyx_hfi_status
onyx_gen8_context_enqueue(struct onyx_gen8_context_queue *queue,
    const uint32_t *packet, size_t words)
{
	if (!queue || !queue->shared || !packet || words < 11 || words > 254 ||
	    (packet[0] & 0xff) != 130 || ((packet[0] >> 8) & 255) != words ||
	    ((packet[0] >> 16) & 15) != 0 || packet[1] != queue->context ||
	    packet[2] || packet[4] || packet[5] || packet[7] ||
	    !packet[6] || packet[6] > 82 || 8u + 3u * packet[6] != words)
		return ONYX_HFI_ARGUMENT;
	for (size_t n = 8; n < words; n += 3) {
		uint64_t address = packet[n] | (uint64_t)packet[n + 1] << 32;
		if (!packet[n + 2] || (packet[n] & 3) || (packet[n + 2] & 3) ||
		    address > UINT64_MAX - packet[n + 2]) return ONYX_HFI_ARGUMENT;
	}
	struct onyx_gen8_context_header *header = queue->shared;
	uint32_t read = __atomic_load_n(&header->read_index, __ATOMIC_ACQUIRE);
	uint32_t write = __atomic_load_n(&header->write_index, __ATOMIC_ACQUIRE);
	shared_barrier();
	if (header->version || header->start_addr != queue->gmu_address + 48 ||
	    header->queue_size != 1012 || read >= 1012 || write >= 1012 ||
	    (read & 3) || (write & 3)) return ONYX_HFI_CORRUPT;
	uint32_t padded = ((uint32_t)words + 3) & ~3u;
	uint32_t used = (write + 1012 - read) % 1012;
	if (1012 - used <= padded) return ONYX_HFI_FULL;
	uint32_t *data = (uint32_t *)((uint8_t *)queue->shared + 48);
	for (uint32_t n = 0; n < padded; n++)
		data[(write + n) % 1012] = n < words ? packet[n] : 0xfafafafa;
	shared_barrier();
	__atomic_store_n(&header->write_index, (write + padded) % 1012, __ATOMIC_RELEASE);
	shared_barrier();
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
