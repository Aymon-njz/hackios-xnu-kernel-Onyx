#include "hakios.onyx_gen8_power.h"
#include <assert.h>
#include <stdio.h>
struct fake { uint64_t now; unsigned reads, waits, mode; };
static uint32_t read_counter(void *p, uint32_t at)
{
    struct fake *f = p;
    assert(at == 0x100 || at == 0x104);
    f->reads++;
    if (f->mode == 2 && at == 0x104) return f->reads;
    uint32_t value = at == 0x104 ? (uint32_t)(f->now >> 32) : (uint32_t)f->now;
    if (f->mode == 3 && f->reads == 2) f->now += 4;
    return value;
}
static void wait_counter(void *p, unsigned us)
{
    struct fake *f = p;
    assert(us == 1000);
    f->waits++;
    if (f->mode == 1 || f->mode == 3) f->now += 19200;
    if (f->mode == 4) f->now--;
}
int main(void)
{
    struct fake f = {0};
    struct onyx_rsc_io io = { &f, read_counter, NULL, wait_counter, 1, 0 };
    uint64_t a, b;
    assert(onyx_gen8_counter_sample(&io, &a, &b) == ONYX_COUNTER_STALLED);
    f.now = UINT64_MAX;
    assert(onyx_gen8_counter_sample(&io, &a, &b) == ONYX_COUNTER_STALLED);
    f = (struct fake){ .now=100, .mode=1 };
    assert(onyx_gen8_counter_sample(&io, &a, &b) == ONYX_COUNTER_OK && a==100 && b==19300);
    f = (struct fake){ .now=0xfffffffeu, .mode=3 };
    assert(onyx_gen8_counter_sample(&io, &a, &b) == ONYX_COUNTER_OK && a==0x100000002ULL);
    f = (struct fake){ .mode=2 };
    assert(onyx_gen8_counter_sample(&io, &a, &b) == ONYX_COUNTER_UNSTABLE && f.reads==24 && !f.waits);
    f = (struct fake){ .now=100, .mode=4 };
    assert(onyx_gen8_counter_sample(&io, &a, &b) == ONYX_COUNTER_BACKWARDS);
    io.owned=0; f.reads=0;
    assert(onyx_gen8_counter_sample(&io, &a, &b) == ONYX_COUNTER_ARGUMENT && !f.reads);
    io.owned=1; io.quarantined=1;
    assert(onyx_gen8_counter_sample(&io, &a, &b) == ONYX_COUNTER_ARGUMENT);
    assert(onyx_gen8_counter_sample(NULL, &a, &b) == ONYX_COUNTER_ARGUMENT);
    puts("GMUCX AO counter: advancing/stalled/backwards, 32-bit rollover, bounded unstable reads and ownership gates passed");
}
