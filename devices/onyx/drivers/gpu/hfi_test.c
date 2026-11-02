#include "hakios.onyx_gen8_hfi.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static _Alignas(16) uint8_t memory[13152];
static struct onyx_hfi hfi;
static struct onyx_hfi_table *table = (void *)memory;
static uint32_t *data(unsigned q) { return (void *)(memory + 848 + q * 4096); }
static void reset(void)
{
	memset(memory, 0xa5, sizeof(memory));
	assert(onyx_hfi_init(&hfi, memory, sizeof(memory), 0x60002000) == ONYX_HFI_OK);
	for (size_t n = ONYX_HFI_SHARED_BYTES; n < sizeof(memory); n++) assert(memory[n] == 0xa5);
}

int main(void)
{
	uint32_t message[255], output[255]; size_t words;
	reset();
	/* Literal ABI numbers from KGSL, not derived from implementation macros. */
	const uint32_t expected_table[] = { 0, 210, 6, 12, 3, 3 };
	assert(memcmp(memory, expected_table, sizeof(expected_table)) == 0);
	assert(table->queue[0].start_addr == 0x60002350);
	assert(table->queue[1].start_addr == 0x60003350);
	assert(table->queue[2].start_addr == 0x60004350);
	assert(table->queue[2].type == 2 && table->queue[2].queue_size == 1024);
	for (unsigned q = 3; q < 17; q++) assert(table->queue[q].status == 0);
	assert(onyx_hfi_init(&hfi, memory + 1, sizeof(memory) - 1, 0x60002000) == ONYX_HFI_ARGUMENT);
	assert(onyx_hfi_init(&hfi, memory, sizeof(memory), 0xfffffff0) == ONYX_HFI_ARGUMENT);
	assert(onyx_hfi_init(&hfi, memory, 13143, 0x60002000) == ONYX_HFI_ARGUMENT);
	/* Exercise each encodable message length at both ends of the ring. */
	const unsigned positions[] = { 0, 4, 1000, 1020 };
	for (unsigned p = 0; p < 4; p++) for (unsigned n = 1; n <= 255; n++) {
		reset(); unsigned start = positions[p], padded = (n + 3) & ~3u;
		for (unsigned i = 0; i < n; i++) message[i] = 0xdead0000 + i;
		message[0] = 0x12300001 | n << 8;
		table->queue[0].read_index = table->queue[0].write_index = start;
		assert(onyx_hfi_send(&hfi, message, n) == ONYX_HFI_OK);
		assert(table->queue[0].write_index == (start + padded) % 1024);
		for (unsigned i = 0; i < padded; i++) {
			uint32_t expected = i < n ? message[i] : 0xfafafafa;
			assert(data(0)[(start + i) % 1024] == expected);
			/* Act as firmware producing the same message on message queue. */
			data(1)[(start + i) % 1024] = expected;
		}
		table->queue[1].read_index = start;
		table->queue[1].write_index = (start + padded) % 1024;
		memset(output, 0, sizeof(output));
		assert(onyx_hfi_receive(&hfi, 1, output, 255, &words) == ONYX_HFI_OK);
		assert(words == n && memcmp(output, message, n * 4) == 0);
		assert(table->queue[1].read_index == table->queue[1].write_index);
		assert(onyx_hfi_receive(&hfi, 1, output, 255, &words) == ONYX_HFI_EMPTY && !words);
	}
	reset(); message[0] = 0x101;
	for (unsigned n = 0; n < 255; n++) assert(onyx_hfi_send(&hfi, message, 1) == ONYX_HFI_OK);
	uint8_t snapshot[sizeof(memory)]; memcpy(snapshot, memory, sizeof(memory));
	assert(onyx_hfi_send(&hfi, message, 1) == ONYX_HFI_FULL);
	assert(memcmp(snapshot, memory, sizeof(memory)) == 0);
	table->queue[0].read_index = 1024;
	assert(onyx_hfi_send(&hfi, message, 1) == ONYX_HFI_CORRUPT);
	reset(); table->queue[0].write_index = 2;
	assert(onyx_hfi_send(&hfi, message, 1) == ONYX_HFI_CORRUPT);
	reset(); table->queue[0].queue_size = 0xffffffff;
	assert(onyx_hfi_send(&hfi, message, 1) == ONYX_HFI_CORRUPT);
	reset(); data(2)[0] = 0x501; table->queue[2].write_index = 4;
	memset(output, 0xa5, sizeof(output));
	assert(onyx_hfi_receive(&hfi, 2, output, 255, &words) == ONYX_HFI_INCOMPLETE);
	assert(!words && table->queue[2].read_index == 0 && output[0] == 0xa5a5a5a5);
	table->queue[2].write_index = 8;
	assert(onyx_hfi_receive(&hfi, 2, output, 4, &words) == ONYX_HFI_CAPACITY);
	assert(!words && table->queue[2].read_index == 0 && output[0] == 0xa5a5a5a5);
	data(2)[0] = 0;
	assert(onyx_hfi_receive(&hfi, 2, output, 255, &words) == ONYX_HFI_CORRUPT);
	assert(onyx_hfi_receive(&hfi, 0, output, 255, &words) == ONYX_HFI_ARGUMENT);
	assert(onyx_hfi_send(&hfi, message, 0) == ONYX_HFI_ARGUMENT);
	message[0] = 0x201;
	assert(onyx_hfi_send(&hfi, message, 1) == ONYX_HFI_ARGUMENT);
	/* Context submission uses a different 48-byte header and 1012-word ring. */
	static _Alignas(16) uint8_t context_memory[4096];
	struct onyx_gen8_context_queue context_queue;
	struct onyx_gen8_context_header *context_header = (void *)context_memory;
	struct onyx_gen8_ib ibs[83];
	uint32_t packet[255];
	for (unsigned n = 0; n < 83; n++) ibs[n] = (struct onyx_gen8_ib){ 0x123456780ull, 16 };
	assert(onyx_gen8_submit_packet(7, 9, 0x123, ibs, 1, packet, 255, &words) == ONYX_HFI_OK);
	const uint32_t golden[] = { 0x12300b82, 7, 0, 9, 0, 0, 1, 0, 0x23456780, 1, 16 };
	assert(words == 11 && memcmp(packet, golden, sizeof(golden)) == 0);
	assert(onyx_gen8_context_init(&context_queue, context_memory, 4096, 0x60002000, 7) == ONYX_HFI_OK);
	assert(context_header->start_addr == 0x60002030 && context_header->queue_size == 1012);
	context_header->read_index = context_header->write_index = 1008;
	assert(onyx_gen8_context_enqueue(&context_queue, packet, words) == ONYX_HFI_OK);
	assert(context_header->write_index == 8);
	uint32_t *context_data = (void *)(context_memory + 48);
	for (unsigned n = 0; n < 11; n++) assert(context_data[(1008 + n) % 1012] == golden[n]);
	assert(context_data[7] == 0xfafafafa);
	for (unsigned n = 1; n <= 82; n++) {
		assert(onyx_gen8_submit_packet(7, 0, 4095, ibs, n, packet, 255, &words) == ONYX_HFI_OK);
		assert(words == 8 + n * 3);
		assert(onyx_gen8_context_init(&context_queue, context_memory, 4096, 0x60002000, 7) == ONYX_HFI_OK);
		assert(onyx_gen8_context_enqueue(&context_queue, packet, words) == ONYX_HFI_OK);
	}
	assert(onyx_gen8_submit_packet(7, 9, 0, ibs, 83, packet, 255, &words) == ONYX_HFI_ARGUMENT && !words);
	assert(onyx_gen8_submit_packet(7, 9, 4096, ibs, 1, packet, 255, &words) == ONYX_HFI_ARGUMENT);
	assert(onyx_gen8_submit_packet(7, 9, 0, ibs, 1, packet, 10, &words) == ONYX_HFI_CAPACITY);
	ibs[0].address = UINT64_MAX - 3;
	assert(onyx_gen8_submit_packet(7, 9, 0, ibs, 1, packet, 255, &words) == ONYX_HFI_ARGUMENT);
	ibs[0].address = 0x1000;
	assert(onyx_gen8_submit_packet(7, 9, 0, ibs, 1, packet, 255, &words) == ONYX_HFI_OK);
	assert(onyx_gen8_context_init(&context_queue, context_memory, 4096, 0x60002000, 7) == ONYX_HFI_OK);
	context_header->write_index = 1008;
	uint8_t saved_context[4096]; memcpy(saved_context, context_memory, 4096);
	assert(onyx_gen8_context_enqueue(&context_queue, packet, words) == ONYX_HFI_FULL);
	assert(memcmp(saved_context, context_memory, 4096) == 0);
	context_header->write_index = 1012;
	assert(onyx_gen8_context_enqueue(&context_queue, packet, words) == ONYX_HFI_CORRUPT);
	packet[1] = 8;
	assert(onyx_gen8_context_enqueue(&context_queue, packet, words) == ONYX_HFI_ARGUMENT);
	puts("Gen8 HFI layout, framing, wraparound and malformed-peer checks passed");
	return 0;
}
