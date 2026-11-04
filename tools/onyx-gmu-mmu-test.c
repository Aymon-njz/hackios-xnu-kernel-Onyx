#include "hakios.onyx_gmu_mmu.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static _Alignas(4096) uint64_t pages[16*512], saved[16*512];
int main(void)
{
 struct onyx_gmu_mmu m; uint64_t pa,d;
 assert(onyx_gmu_mmu_init(&m,pages,0x90000000,16)==0);
 /* Firmware CACHE: RX, normal WB; HFI: RW, normal NC, never executable. */
 assert(onyx_gmu_mmu_map(&m,0x4000,0x91000000,0x16000,ONYX_GMU_READ|ONYX_GMU_EXEC|ONYX_GMU_WB)==0);
 assert(onyx_gmu_mmu_lookup(&m,0x19fff,&pa,&d)==0 && pa==0x91015fff);
 assert((d&(1ULL<<7)) && (d&4) && !(d&(1ULL<<54)));
 assert(onyx_gmu_mmu_map(&m,0x60000000,0x92000000,0x2000,ONYX_GMU_READ|ONYX_GMU_WRITE)==0);
 assert(onyx_gmu_mmu_lookup(&m,0x60001005,&pa,&d)==0 && pa==0x92001005);
 assert(!(d&(1ULL<<7)) && !(d&4) && (d&(1ULL<<54)));
 memcpy(saved,pages,sizeof pages); size_t used=m.used;
 assert(onyx_gmu_mmu_map(&m,0x3000,0x93000000,0x2000,ONYX_GMU_READ)==ONYX_GMU_MAP_CONFLICT);
 assert(m.used==used && !memcmp(saved,pages,sizeof pages));
 assert(onyx_gmu_mmu_map(&m,0xfffff000,0x93000000,0x2000,ONYX_GMU_READ)==ONYX_GMU_MAP_ARGUMENT);
 assert(onyx_gmu_mmu_map(&m,0x2000,(1ULL<<40)-4096,8192,ONYX_GMU_READ)==ONYX_GMU_MAP_ARGUMENT);
 assert(onyx_gmu_mmu_map(&m,0x2000,0x93000000,4096,0x80)==ONYX_GMU_MAP_ARGUMENT);
 assert(onyx_gmu_mmu_lookup(&m,0x2000,&pa,&d)!=0);
 /* Cross L2 and L1 boundaries; independent page-table branches. */
 assert(onyx_gmu_mmu_map(&m,0xbffff000,0x94000000,0x2000,ONYX_GMU_READ)==0);
 assert(onyx_gmu_mmu_lookup(&m,0xc0000000,&pa,&d)==0 && pa==0x94001000);
 assert(onyx_gmu_mmu_init(&m,pages,0x90000000,2)==0);
 memcpy(saved,pages,sizeof pages);
 assert(onyx_gmu_mmu_map(&m,0x4000,0x91000000,4096,1)==ONYX_GMU_MAP_CAPACITY);
 assert(m.used==1 && !memcmp(saved,pages,sizeof pages));
 pages[0]=0xdead0003;
 assert(onyx_gmu_mmu_map(&m,0x4000,0x91000000,4096,1)==ONYX_GMU_MAP_CORRUPT);
 assert(onyx_gmu_mmu_init(&m,pages,0,16)==ONYX_GMU_MAP_ARGUMENT);
 puts("GMU page tables: firmware/HFI translation, permissions, boundary crossing, atomic overlap/capacity failures and corrupt descriptors passed");
}
