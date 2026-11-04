/* XNU physical backing and startup orchestration. Platform handoff required. */
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
static const void *native_firmware;
static size_t native_firmware_bytes;
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
 if(native_ready) return firmware==native_firmware && bytes==native_firmware_bytes ? 0 : -1;
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
 native_firmware=firmware; native_firmware_bytes=bytes;
 native_ready=1;
 printf("onyx-gen8: native backing ready: %lu regions, %lu tables, TTBR PA %#llx; software walks verified, hardware translation untested\n",
   native_memory.count,native_tables.used,native_tables.physical);
 return 0;
}

#include "hakios.onyx_gmu_native.h"
#include "hakios.onyx_gmu_upload.h"
static struct onyx_gmu_attachment native_attachment;
static struct onyx_gen8_fw_block native_upload_blocks[64];
static unsigned native_start_attempted;
int onyx_gmu_native_start(const void *firmware,size_t bytes,struct onyx_gmu_platform *p)
{
 /* No boot argument may manufacture these platform operations or lease. */
 if (!p || !p->smmu || !p->gpu || !p->control || !p->verify_translation ||
     !p->configure || !p->event || !p->scm || native_start_attempted) return -1;
 if (!p->lease.handoff_verified || !p->lease.cm3_reset_verified) return -1;
 int rc=onyx_gmu_native_prepare(firmware,bytes);
 if(rc) return rc;
 native_start_attempted=1;
 p->lease.tables_published=1;
 rc=onyx_gmu_attach(p->smmu,&p->lease,&native_tables,&native_attachment);
 printf("onyx-gen8: native SMMU attach result %d\n",rc);
 if(rc) return -2;
 /* Software table walks do not satisfy this hardware translation check. */
 if(p->verify_translation(p->context,&native_attachment)) return -3;
 struct onyx_gmu_upload_report upload;
 rc=onyx_gmu_upload_tcm(p->gpu,firmware,bytes,native_upload_blocks,64,&upload);
 printf("onyx-gen8: TCM upload result %d words %u offset %#x\n",rc,
     upload.words_verified,upload.failed_offset);
 if(rc) return -4;
 rc=onyx_gmu_secure_cx(p->scm,p->secure_features);
 printf("onyx-gen8: secure CX result %d firmware %lld\n",rc,p->scm->firmware_status);
 if(rc) return -8;
 struct onyx_gmu_start_ready ready={0};
 /* Platform performs operating clocks, RSCC, register
  * configuration and bandwidth votes, recording only completed operations. */
 if(p->configure(p->context,native_memory.hfi.gmu_address,&ready)) return -5;
 ready.secure_cx_initialized=1;
 ready.domain=&native_attachment;
 ready.translation_verified=1;
 ready.firmware_uploaded=1;
 rc=onyx_gmu_start(p->control,&ready);
 printf("onyx-gen8: CM3/HFI startup result %d\n",rc);
 if(rc) return -6;
 struct onyx_gmu_exchange exchange={.hfi=&native_memory.hfi,.io=p->control,
     .context=p->context,.event=p->event,.sequence=1,.ready=1};
 uint32_t firmware_result=0;
 rc=onyx_gmu_core_fw_start(&exchange,&firmware_result);
 printf("onyx-gen8: CORE_FW_START result %d firmware %#x\n",rc,firmware_result);
 if(rc) {
  p->control->write(p->control->context,0,1);
  p->control->quarantined=1;
  return -7;
 }
 return 0;
}
