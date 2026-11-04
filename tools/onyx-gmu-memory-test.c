#include "hakios.onyx_gmu_memory.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static _Alignas(4096) uint64_t tables[64*512];
struct fake { unsigned calls, fail, alive, publishes; };
static int alloc(void *p,size_t n,void **cpu,uint64_t *pa)
{ struct fake *f=p; f->calls++; if(f->calls==f->fail)return -1;
 *cpu=aligned_alloc(4096,n); if(!*cpu)return -1; *pa=0x94000000ULL+f->calls*0x200000; f->alive++; return 0; }
static void release(void *p,void *cpu,size_t n) { (void)n; ((struct fake *)p)->alive--; free(cpu); }
static void publish(void *p,void *cpu,size_t n) { assert(cpu && n); ((struct fake *)p)->publishes++; }
int main(int argc,char **argv)
{
 long n; void *image;
 if(argc==2) {
  FILE *f=fopen(argv[1],"rb");assert(f); fseek(f,0,SEEK_END);n=ftell(f);rewind(f);
  image=malloc(n);assert(image && fread(image,1,n,f)==(size_t)n);fclose(f);
 } else {
  uint32_t fixture[]={0x4000,0,1,0x1000,0x4000,4,0xefbeadde,0,0x12345678};
  n=sizeof(fixture);image=malloc(n);assert(image);memcpy(image,fixture,n);
 }
 struct onyx_gmu_mmu pt; struct onyx_gmu_memory m={0}; struct fake a={0};
 struct onyx_gmu_allocator ops={&a,alloc,release,publish};
 assert(!onyx_gmu_mmu_init(&pt,tables,0x90000000,64));
 assert(onyx_gmu_memory_prepare(&m,&pt,&ops,image,n)==0);
 assert(a.alive==m.count && a.publishes==m.count+1 && m.count>1);
 for(size_t i=0;i<m.count;i++) { uint64_t pa,d;
  assert(!onyx_gmu_mmu_lookup(&pt,m.region[i].va,&pa,&d) && pa==m.region[i].pa);
  assert(!(d&(1ULL<<6))); /* IOMMU_PRIV: no unprivileged access */
  assert(!(d&4)); /* Stock default has no IOMMU_CACHE attribute. */
 }
 struct onyx_gen8_fw_block blocks[64];size_t count;
 assert(!onyx_gen8_fw_plan(image,n,blocks,64,&count));
 for(size_t i=0;i<count;i++) if(blocks[i].payload && blocks[i].region==ONYX_GEN8_CACHE) {
  int found=0; for(size_t j=0;j<m.count;j++) if(m.region[j].va==blocks[i].address) {
   assert(!memcmp(m.region[j].cpu,blocks[i].payload,blocks[i].length));found=1;
  } assert(found);
 }
 size_t allocations=m.count;
 onyx_gmu_memory_release(&m,&ops); assert(!a.alive && !m.count);
 for(unsigned failure=1;failure<=allocations;failure++) {
  assert(!onyx_gmu_mmu_init(&pt,tables,0x90000000,64)); a=(struct fake){.fail=failure};
  assert(onyx_gmu_memory_prepare(&m,&pt,&ops,image,n)!=0);
  assert(!a.alive && !m.count && pt.used==1 && !a.publishes);
  for(unsigned j=0;j<512;j++) assert(!tables[j]);
 }
 free(image); puts("GMU real firmware backing: payload copies, HFI mappings, publication and every allocation-failure rollback passed");
}
