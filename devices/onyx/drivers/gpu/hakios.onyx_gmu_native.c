/* XNU physical backing adapter. Preparation only; not a live SMMU attach. */
#include <sys/systm.h>
#include <mach/mach_types.h>
#include <mach/vm_param.h>
#include <vm/vm_kern.h>
#include <vm/memory_types.h>
#include "hakios.onyx_gmu_memory.h"
extern uint64_t kvtophys(vm_offset_t);
extern void FlushPoC_DcacheRegion(vm_offset_t,size_t);
static struct onyx_gmu_mmu native_tables;
static struct onyx_gmu_memory native_memory;
static void *native_arena;
static int native_ready;
static int allocate(void *unused,size_t bytes,void **cpu,uint64_t *pa)
{
 (void)unused; vm_offset_t address=0;
 *cpu=NULL; *pa=0;
 if(kmem_alloc_contig(kernel_map,&address,bytes,PAGE_MASK,0,0,
   KMA_KOBJECT|KMA_ZERO|KMA_DATA,VM_KERN_MEMORY_IOKIT)!=KERN_SUCCESS) return -1;
 uint64_t first=kvtophys(address);
 for(size_t n=0;n<bytes;n+=4096) if(kvtophys(address+n)!=first+n) {
  kmem_free(kernel_map,address,bytes); return -1;
 }
 *cpu=(void *)address; *pa=first; return 0;
}
static void release(void *unused,void *cpu,size_t bytes)
{ (void)unused; kmem_free(kernel_map,(vm_offset_t)cpu,bytes); }
static void publish(void *unused,void *cpu,size_t bytes)
{ (void)unused; FlushPoC_DcacheRegion((vm_offset_t)cpu,bytes); __asm__ volatile("dsb sy" ::: "memory"); }
int onyx_gmu_native_prepare(const void *firmware,size_t bytes)
{
 const size_t arena_bytes=64*4096;
 struct onyx_gmu_allocator ops={NULL,allocate,release,publish};
 uint64_t pa=0;
 if(native_ready) return 0;
 if(allocate(NULL,arena_bytes,&native_arena,&pa)) return -1;
 int status=onyx_gmu_mmu_init(&native_tables,native_arena,pa,64);
 if(!status) status=onyx_gmu_memory_prepare(&native_memory,&native_tables,&ops,firmware,bytes);
 if(!status) {
  for(size_t n=0;n<native_memory.count;n++) {
   const struct onyx_gmu_backing *r=&native_memory.region[n];
   for(size_t page=0;page<r->bytes;page+=4096) {
    uint64_t translated,descriptor;
    if(onyx_gmu_mmu_lookup(&native_tables,r->va+page,&translated,&descriptor) || translated!=r->pa+page)
     status=-2;
   }
   printf("onyx-gen8: private backing VA %#x bytes %#x PA %#llx flags %#x\n",r->va,r->bytes,r->pa,r->flags);
  }
 }
 if(status) {
  onyx_gmu_memory_release(&native_memory,&ops);
  release(NULL,native_arena,arena_bytes); native_arena=NULL;
  printf("onyx-gen8: native firmware backing failed %d; no domain attached\n",status); return status;
 }
 native_ready=1;
 printf("onyx-gen8: native backing ready: %lu regions, %lu tables, TTBR PA %#llx; software walks verified, hardware translation untested\n",
   native_memory.count,native_tables.used,native_tables.physical);
 return 0;
}
