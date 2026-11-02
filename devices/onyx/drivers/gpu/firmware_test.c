/* Host-only validation; never linked into XNU. Optional argument: GMU image. */
#include "hakios.onyx_gen8_firmware.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put32(uint8_t *p, uint32_t v)
{
	for (unsigned n = 0; n < 4; n++) p[n] = (uint8_t)(v >> (8 * n));
}

int main(int argc, char **argv)
{
	uint8_t image[36] = { 0 }, changed[36];
	struct onyx_gen8_fw_block plan[64], untouched[64];
	size_t count;
	/* One metadata reservation, followed by a four-byte ITCM payload. */
	put32(image + 8, 1); put32(image + 12, 4);
	put32(image + 20, 4); put32(image + 24, 0xefbeadde);
	put32(image + 28, 0xefbeadde); put32(image + 32, 0x12345678);
	assert(onyx_gen8_fw_plan(image, sizeof(image), plan, 64, &count) == ONYX_GEN8_FW_OK);
	assert(count == 2 && plan[0].payload == NULL && plan[0].value == 4);
	assert(plan[1].payload == image + 32 && plan[1].region == ONYX_GEN8_ITCM);
	memset(untouched, 0xa5, sizeof(untouched));
	for (size_t n = 0; n < sizeof(image); n++) {
		memcpy(plan, untouched, sizeof(plan)); count = 99;
		assert(onyx_gen8_fw_plan(image, n, plan, 64, &count) != ONYX_GEN8_FW_OK);
		assert(count == 0 && memcmp(plan, untouched, sizeof(plan)) == 0);
	}
	assert(onyx_gen8_fw_plan(image, sizeof(image), plan, 1, &count) == ONYX_GEN8_FW_CAPACITY);
	memcpy(changed, image, sizeof(image)); put32(changed + 16, 0xfffffffc);
	assert(onyx_gen8_fw_plan(changed, sizeof(changed), plan, 64, &count) == ONYX_GEN8_FW_RANGE);
	memcpy(changed, image, sizeof(image)); put32(changed + 16, 0x3ffc); put32(changed + 20, 8);
	/* Available payload is checked before its address range. */
	assert(onyx_gen8_fw_plan(changed, sizeof(changed), plan, 64, &count) == ONYX_GEN8_FW_TRUNCATED);
	memcpy(changed, image, sizeof(image)); put32(changed + 8, 99);
	assert(onyx_gen8_fw_plan(changed, sizeof(changed), plan, 64, &count) == ONYX_GEN8_FW_METADATA);
	memcpy(changed, image, sizeof(image)); put32(changed + 12, 0xffffffff);
	assert(onyx_gen8_fw_plan(changed, sizeof(changed), plan, 64, &count) == ONYX_GEN8_FW_RANGE);
	memcpy(changed, image, sizeof(image)); put32(changed + 16, 0x4000);
	assert(onyx_gen8_fw_plan(changed, sizeof(changed), plan, 64, &count) == ONYX_GEN8_FW_BACKING);
	put32(changed, 0x4000);
	assert(onyx_gen8_fw_plan(changed, sizeof(changed), plan, 64, &count) == ONYX_GEN8_FW_OK);
	assert(plan[1].region == ONYX_GEN8_CACHE);
	assert(onyx_gen8_fw_plan(NULL, 1, plan, 64, &count) == ONYX_GEN8_FW_ARGUMENT);
	if (argc == 2) {
		FILE *f = fopen(argv[1], "rb"); assert(f);
		assert(fseek(f, 0, SEEK_END) == 0); long length = ftell(f); assert(length > 0);
		rewind(f); uint8_t *data = malloc((size_t)length); assert(data);
		assert(fread(data, 1, (size_t)length, f) == (size_t)length); fclose(f);
		assert(onyx_gen8_fw_plan(data, (size_t)length, plan, 64, &count) == ONYX_GEN8_FW_OK);
		printf("GMU firmware: %zu validated blocks\n", count);
		for (size_t n = 0; n < count; n++)
			printf("%s address=%#x bytes=%#x region=%d type=%#x\n",
			    plan[n].payload ? "load" : "metadata", plan[n].address,
			    plan[n].payload ? plan[n].length : plan[n].value, plan[n].region, plan[n].type);
		free(data);
	}
	puts("firmware parser checks passed");
	return 0;
}
