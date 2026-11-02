#include "hakios.onyx_rpmh.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void p16(uint8_t *p, unsigned v) { p[0] = v; p[1] = v >> 8; }
int main(void)
{
	uint8_t db[200] = { 0 };
	struct onyx_rpmh_resource result, sentinel;
	memset(&sentinel, 0xa5, sizeof(sentinel));
	db[4] = 0xdb; db[5] = 0x30; db[6] = 3; db[7] = 0x0c;
	p16(db + 8, 3); p16(db + 12, 24); p16(db + 14, 1);
	memcpy(db + 144, "cx.lvl", 6);
	db[162] = 3; /* ARC resource address 0x30000 */
	p16(db + 164, 8);
	p16(db + 168, 0); p16(db + 170, 0x10); p16(db + 172, 0x180);
	assert(!onyx_rpmh_lookup(db, sizeof(db), "cx.lvl", &result));
	assert(result.address == 0x30000 && result.count == 3 && result.levels[2] == 0x180);
	uint32_t address = 0xdead, index = 0xbeef;
	assert(!onyx_rpmh_arc_vote(&result, 1, 0, &address, &index));
	assert(address == 0x30000 && index == 1);
	assert(!onyx_rpmh_arc_vote(&result, 0x10, 0x180, &address, &index) && index == 2);
	assert(onyx_rpmh_arc_vote(&result, 0x181, 0, &address, &index) == 1);
	result.address = 0x40000;
	assert(onyx_rpmh_arc_vote(&result, 0x10, 0, &address, &index) == 2);
	struct onyx_rsc_layout layout;
	assert(!onyx_rsc_layout(0x30000, &layout) && layout.control == 0x24 && layout.stride == 24);
	assert(!onyx_rsc_layout(0x20700, &layout) && layout.control == 0x14 && layout.stride == 20);
	assert(onyx_rsc_layout(0x40000, &layout) == 1);
	assert(onyx_rsc_layout(0x20600, &layout) == 1);
	result = sentinel;
	assert(onyx_rpmh_lookup(db, sizeof(db), "mx.lvl", &result) == 1);
	assert(!memcmp(&result, &sentinel, sizeof(result)));
	for (unsigned size = 0; size < 176; size++) {
		result = sentinel;
		assert(onyx_rpmh_lookup(db, size, "cx.lvl", &result) == 2);
		assert(!memcmp(&result, &sentinel, sizeof(result)));
	}
	p16(db + 172, 0x10);
	assert(onyx_rpmh_lookup(db, sizeof(db), "cx.lvl", &result) == 2);
	p16(db + 172, 0x180); p16(db + 166, 0xffff);
	assert(onyx_rpmh_lookup(db, sizeof(db), "cx.lvl", &result) == 2);
	p16(db + 166, 0); p16(db + 10, 0xffff);
	assert(onyx_rpmh_lookup(db, sizeof(db), "cx.lvl", &result) == 2);
	puts("RPMh command DB bounds, levels, missing resource and output preservation passed");
}
