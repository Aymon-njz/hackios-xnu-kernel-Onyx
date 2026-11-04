#include "hakios.onyx_gmu_memory.h"
static void zero(void *p, size_t n) { unsigned char *b=p; while(n--) *b++=0; }
static void copy(void *p, const void *s, size_t n)
{ unsigned char *b=p; const unsigned char *a=s; while(n--) *b++=*a++; }
void onyx_gmu_memory_release(struct onyx_gmu_memory *m,const struct onyx_gmu_allocator *a)
{
 if (!m || !a || !a->release) return;
 while(m->count) { struct onyx_gmu_backing *r=&m->region[--m->count];
  if(r->cpu) a->release(a->context,r->cpu,r->bytes);
 }
 zero(m,sizeof(*m));
}
static int add(struct onyx_gmu_memory *m,uint32_t va,uint32_t bytes,unsigned flags)
{
 if(!bytes || (va&4095) || bytes>0x100000 || m->count==ONYX_GMU_MEMORY_MAX) return 0;
 bytes=(bytes+4095)&~4095u;
 for(size_t i=0;i<m->count;i++) {
  uint64_t end=(uint64_t)m->region[i].va+m->region[i].bytes;
  if(va<end && (uint64_t)va+bytes>m->region[i].va) return 0;
 }
 m->region[m->count++]=(struct onyx_gmu_backing){va,bytes,NULL,0,flags}; return 1;
}
int onyx_gmu_memory_prepare(struct onyx_gmu_memory *m,struct onyx_gmu_mmu *pt,
 const struct onyx_gmu_allocator *a,const void *image,size_t bytes)
{
 struct onyx_gen8_fw_block blocks[64]; size_t count=0;
 int result=ONYX_GMU_MEMORY_LAYOUT;
 if(!m || m->count || !pt || !pt->tables || pt->used!=1 || !a ||
    !a->allocate || !a->release || !a->publish) return ONYX_GMU_MEMORY_ARGUMENT;
 for(unsigned i=0;i<512;i++) if(pt->tables[i]) return ONYX_GMU_MEMORY_ARGUMENT;
 if(onyx_gen8_fw_plan(image,bytes,blocks,64,&count)!=ONYX_GEN8_FW_OK)
  return ONYX_GMU_MEMORY_FIRMWARE;
 zero(m,sizeof(*m));
 for(size_t i=0;i<count;i++) {
  const struct onyx_gen8_fw_block *b=&blocks[i];
  if(b->payload || (b->type!=1 && b->type!=7) ||
     b->region==ONYX_GEN8_ITCM || b->region==ONYX_GEN8_DTCM) continue;
  unsigned flags=ONYX_GMU_READ|ONYX_GMU_WRITE;
  if(b->region==ONYX_GEN8_CACHE) flags=ONYX_GMU_READ|ONYX_GMU_EXEC|ONYX_GMU_WB;
  if(!add(m,b->address,b->value,flags)) goto fail;
 }
 /* Firmware metadata reserves 0x60000000/1000. HFI follows those pages. */
 if(!add(m,0x60002000,(ONYX_HFI_SHARED_BYTES+4095)&~4095u,
     ONYX_GMU_READ|ONYX_GMU_WRITE)) goto fail;
 for(size_t i=0;i<m->count;i++) {
  struct onyx_gmu_backing *r=&m->region[i];
  result=ONYX_GMU_MEMORY_ALLOCATE;
  if(a->allocate(a->context,r->bytes,&r->cpu,&r->pa) || !r->cpu ||
     ((uintptr_t)r->cpu&4095) || !r->pa || (r->pa&4095) || r->pa >= (1ULL<<40) ||
     r->bytes > (1ULL<<40)-r->pa) goto fail;
  /* Never accept overlapping physical allocations, including table memory. */
  if(r->pa<(pt->physical+pt->pages*4096) && r->pa+r->bytes>pt->physical) goto fail;
  for(size_t j=0;j<i;j++) if(r->pa<m->region[j].pa+m->region[j].bytes &&
      r->pa+r->bytes>m->region[j].pa) goto fail;
  zero(r->cpu,r->bytes);
  result=ONYX_GMU_MEMORY_MAP;
  if(onyx_gmu_mmu_map(pt,r->va,r->pa,r->bytes,r->flags)) goto fail;
 }
 for(size_t i=0;i<count;i++) {
  const struct onyx_gen8_fw_block *b=&blocks[i];
  if(!b->payload || b->region==ONYX_GEN8_ITCM || b->region==ONYX_GEN8_DTCM) continue;
  size_t j;
  for(j=0;j<m->count;j++) {
   struct onyx_gmu_backing *r=&m->region[j];
   if(b->address>=r->va && b->length<=r->bytes && b->address-r->va<=r->bytes-b->length) {
    copy((unsigned char *)r->cpu+b->address-r->va,b->payload,b->length); break;
   }
  }
  result=ONYX_GMU_MEMORY_LAYOUT; if(j==m->count) goto fail;
 }
 result=ONYX_GMU_MEMORY_HFI;
 struct onyx_gmu_backing *h=&m->region[m->count-1];
 if(onyx_hfi_init(&m->hfi,h->cpu,h->bytes,h->va)!=ONYX_HFI_OK) goto fail;
 for(size_t i=0;i<m->count;i++) a->publish(a->context,m->region[i].cpu,m->region[i].bytes);
 a->publish(a->context,pt->tables,pt->used*4096);
 return ONYX_GMU_MEMORY_OK;
fail:
 onyx_gmu_memory_release(m,a); zero(pt->tables,4096); pt->used=1; return result;
}
