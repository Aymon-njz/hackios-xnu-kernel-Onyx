#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "hakios.onyx_journal.h"
int main(void)
{
	uint64_t first = ONYX_JOURNAL_FIRST_LBA;
	uint64_t start = ONYX_USERDATA_START_LBA, blocks = ONYX_USERDATA_BYTES / 4096;
	assert(onyx_journal_geometry(4096, blocks, start, 0, 0xff, 0, start * 4096));
	assert(onyx_journal_geometry(4096, blocks, start, 0, 0, 0, start * 4096));
	assert(!onyx_journal_geometry(4096, blocks, start, 0, 1, 0, start * 4096));
	assert(!onyx_journal_geometry(4096, blocks, start, 0, 0xff, 1, start * 4096));
	assert(!onyx_journal_geometry(4096, blocks, start, 1, 0xff, 0, start * 4096));
	assert(!onyx_journal_geometry(512, blocks, start, 0, 0xff, 0, start * 4096));
	assert(!onyx_journal_geometry(4096, blocks - 1, start, 0, 0xff, 0, start * 4096));
	assert(!onyx_journal_geometry(4096, blocks, start + 1, 0, 0xff, 0, start * 4096));
	assert(!onyx_journal_geometry(4096, blocks, start, 0, 0xff, 0, start * 4096 + 4096));
	assert(onyx_journal_span(first, first, 512));
	assert(onyx_journal_span(first, first + 511, 1));
	assert(!onyx_journal_span(first, first - 1, 1));
	assert(!onyx_journal_span(first, first + 512, 1));
	assert(!onyx_journal_span(first, first, 513));
	assert(!onyx_journal_span(first, first, 0));
	assert(!onyx_journal_span(0, 0, 1));
	assert(!onyx_journal_span(first + 1, first + 1, 1));
	assert(!onyx_journal_span(UINT64_MAX, UINT64_MAX, 1));
	assert(ONYX_USERDATA_START_LBA + ONYX_USERDATA_BYTES / 4096 - 512 == first);
	assert(onyx_journal_crc((const uint8_t *)"123456789", 9) == 0xcbf43926);
	uint8_t command[32] = { 1, 0x20, 0, 7 };
	uint32_t lba = (uint32_t)first;
	command[13] = 1; /* 65536 bytes */
	command[16] = 0x2a; command[17] = 8;
	command[18] = lba >> 24; command[19] = lba >> 16;
	command[20] = lba >> 8; command[21] = lba;
	command[24] = 16;
	assert(onyx_journal_command(command, 0x12000000, lba, 16, 7));
	for (unsigned n = 0; n < 32; n++) for (unsigned bit = 0; bit < 8; bit++) {
		uint8_t altered[32]; memcpy(altered, command, 32); altered[n] ^= 1u << bit;
		assert(!onyx_journal_command(altered, 0x12000000, lba, 16, 7));
	}
	for (unsigned bit = 0; bit < 32; bit++)
		assert(!onyx_journal_command(command, 0x12000000 ^ (1u << bit), lba, 16, 7));
	puts("journal: exact 2 MiB range, CRC and 288 command mutations checked");
}
