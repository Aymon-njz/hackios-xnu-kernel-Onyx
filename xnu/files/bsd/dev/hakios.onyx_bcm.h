#ifndef ONYX_BCM_H
#define ONYX_BCM_H
#include <stdint.h>
#include <stddef.h>
struct onyx_bcm_resource { uint32_t address,unit; uint16_t width; uint8_t vcd; };
struct onyx_bcm_node { uint64_t average,peak; unsigned width,channels; };
/* Command DB parser: 0 success, 1 absent, 2 malformed; output unchanged
 * on failure. Does not interpret BCM auxiliary data as ARC voltage levels. */
int onyx_bcm_lookup(const void *,size_t,const char *,struct onyx_bcm_resource *);
/* Input bandwidth units and scale match Linux icc-rpmh (kB/s, scale1000).
 * Nodes contain already aggregated users: average=sum, peak=max per node.
 * No MMIO; values are clamped to the BCM14-bit wire maximum as stock does.
 */
int onyx_bcm_aggregate(const struct onyx_bcm_resource *,const struct onyx_bcm_node *,
 size_t,unsigned scale,unsigned enable_mask,unsigned keepalive,unsigned *x,unsigned *y);
#endif
