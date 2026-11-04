#include "hakios.onyx_gmu_mmu.h"
#define PA_MASK 0x000000fffffff000ULL
static void clear_page(uint64_t *p) { for (unsigned n=0;n<512;n++) p[n]=0; }
static uint64_t *table(const struct onyx_gmu_mmu *m, uint64_t entry)
{
    uint64_t pa=entry&PA_MASK;
    if ((entry&3)!=3 || pa<m->physical ||
        pa-m->physical>=m->used*4096 || (entry&~(PA_MASK|3ULL))) return NULL;
    return m->tables+(size_t)((pa-m->physical)/8);
}
int onyx_gmu_mmu_init(struct onyx_gmu_mmu *m, void *memory, uint64_t pa, size_t pages)
{
    if (!m || !memory || ((uintptr_t)memory&4095) || !pa || (pa&4095) ||
        !pages || pages>256 || pa>=(1ULL<<40) || pages>((1ULL<<40)-pa)/4096)
        return ONYX_GMU_MAP_ARGUMENT;
    *m=(struct onyx_gmu_mmu){memory,pa,pages,1}; clear_page(m->tables);
    return ONYX_GMU_MAP_OK;
}
int onyx_gmu_mmu_map(struct onyx_gmu_mmu *m, uint64_t va, uint64_t pa, size_t bytes, unsigned flags)
{
    if (!m || !m->tables || !m->used || m->used>m->pages || !bytes ||
        ((va|pa|bytes)&4095) || va>=(1ULL<<32) || bytes>(1ULL<<32)-va ||
        pa>=(1ULL<<40) || bytes>(1ULL<<40)-pa || !(flags&ONYX_GMU_READ) ||
        (flags&~15u) || ((flags&ONYX_GMU_WRITE)&&(flags&ONYX_GMU_EXEC)))
        return ONYX_GMU_MAP_ARGUMENT;
    /* Preflight the entire mapping. On error no descriptor or allocation changes. */
    size_t needed=0; uint64_t last1=UINT64_MAX,last2=UINT64_MAX;
    for (uint64_t v=va;v<va+bytes;v+=4096) {
        unsigned a=(unsigned)(v>>30), b=(unsigned)((v>>21)&511), c=(unsigned)((v>>12)&511);
        uint64_t *l2=NULL,*l3=NULL;
        if (m->tables[a]) { l2=table(m,m->tables[a]); if (!l2) return ONYX_GMU_MAP_CORRUPT; }
        else if (last1!=a) { needed++; last1=a; }
        if (l2 && l2[b]) { l3=table(m,l2[b]); if (!l3) return ONYX_GMU_MAP_CORRUPT; }
        else if (last2!=(v>>21)) { needed++; last2=v>>21; }
        if (l3 && l3[c]) return ONYX_GMU_MAP_CONFLICT;
    }
    if (needed>m->pages-m->used) return ONYX_GMU_MAP_CAPACITY;
    uint64_t attrs=3ULL|(1ULL<<10)|(3ULL<<8)|(1ULL<<6);
    if (!(flags&ONYX_GMU_WRITE)) attrs|=1ULL<<7;
    if (!(flags&ONYX_GMU_EXEC)) attrs|=(1ULL<<54)|(1ULL<<53);
    if (flags&ONYX_GMU_WB) attrs|=1ULL<<2;
    for (uint64_t v=va,p=pa;v<va+bytes;v+=4096,p+=4096) {
        uint64_t *parent=m->tables;
        unsigned indices[2]={(unsigned)(v>>30),(unsigned)((v>>21)&511)};
        for (unsigned level=0;level<2;level++) {
            uint64_t *slot=&parent[indices[level]];
            if (!*slot) { size_t n=m->used++; clear_page(m->tables+n*512); *slot=(m->physical+n*4096)|3; }
            parent=table(m,*slot);
        }
        parent[(v>>12)&511]=(p&PA_MASK)|attrs;
    }
    return ONYX_GMU_MAP_OK;
}
int onyx_gmu_mmu_lookup(const struct onyx_gmu_mmu *m, uint64_t va, uint64_t *pa, uint64_t *descriptor)
{
    if (!m || !m->tables || !pa || !descriptor || va>=(1ULL<<32)) return ONYX_GMU_MAP_ARGUMENT;
    uint64_t *l2=table(m,m->tables[va>>30]);
    uint64_t *l3=l2?table(m,l2[(va>>21)&511]):NULL;
    if (!l3 || (l3[(va>>12)&511]&3)!=3) return ONYX_GMU_MAP_CONFLICT;
    *descriptor=l3[(va>>12)&511]; *pa=(*descriptor&PA_MASK)|(va&4095);
    return ONYX_GMU_MAP_OK;
}
