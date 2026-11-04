#include "hakios.onyx_bcm.h"
static uint16_t u16(const uint8_t *p) { return p[0]|(uint16_t)p[1]<<8; }
static uint32_t u32(const uint8_t *p) { return u16(p)|(uint32_t)u16(p+2)<<16; }
int onyx_bcm_lookup(const void *data,size_t bytes,const char *name,struct onyx_bcm_resource *out)
{
 const uint8_t *b=data;uint8_t key[8]={0};unsigned len=0,found=0;
 struct onyx_bcm_resource r={0};
 if(!b || !name || !out || bytes<144 || u32(b+4)!=0x0c0330db) return 2;
 while(len<8 && name[len]) {key[len]=(uint8_t)name[len];len++;}
 if(!len || (len==8 && name[len]))return 2;
 size_t available=bytes-144;
 for(unsigned s=0;s<8;s++) {
  const uint8_t *slave=b+8+16*s;
  if(!u16(slave))continue;
  size_t headers=u16(slave+2),aux=u16(slave+4),count=u16(slave+6);
  if(headers>available || count>(available-headers)/24 || aux>available)return 2;
  for(size_t n=0;n<count;n++) {
   const uint8_t *entry=b+144+headers+24*n;
   size_t size=u16(entry+20),offset=u16(entry+22);
   if(offset>available-aux || size>available-aux-offset)return 2;
   unsigned match=1;for(unsigned i=0;i<8;i++)if(key[i]!=entry[i])match=0;
   if(!match)continue;
   if(found++ || u16(slave)!=5 || size!=8)return 2;
   const uint8_t *a=b+144+aux+offset;
   r.address=u32(entry+16);r.unit=u32(a);r.width=u16(a+4);r.vcd=a[6];
   if(((r.address>>16)&7)!=5 || (r.address&3) || !r.unit || a[7])return 2;
  }
 }
 if(!found)return 1;
 *out=r;return 0;
}
static uint64_t divide(uint64_t value,uint64_t by) { return value && value<by ? 1:value/by; }
int onyx_bcm_aggregate(const struct onyx_bcm_resource *r,const struct onyx_bcm_node *nodes,
 size_t count,unsigned scale,unsigned mask,unsigned keepalive,unsigned *x,unsigned *y)
{
 if(!r || !nodes || !count || !scale || !x || !y || !r->unit || mask>0x3fff || keepalive>1 || (!mask && !r->width))return 2;
 uint64_t ax=0,ay=0;
 for(size_t n=0;n<count;n++) {
  const struct onyx_bcm_node *p=&nodes[n];
  if(!p->width || !p->channels)return 2;
  if(mask) {if(p->average || p->peak)ay|=mask;continue;}
  if(p->average>UINT64_MAX/r->width || p->peak>UINT64_MAX/r->width)return 2;
  uint64_t a=divide(p->average*r->width,(uint64_t)p->width*p->channels);
  uint64_t peak=divide(p->peak*r->width,p->width);
  if(a>ax)ax=a;if(peak>ay)ay=peak;
 }
 if(mask) {if(keepalive)ay|=mask;} else {
  if(ax>UINT64_MAX/scale || ay>UINT64_MAX/scale)return 2;
  ax=divide(ax*scale,r->unit);ay=divide(ay*scale,r->unit);
  if(keepalive && !ax && !ay)ax=ay=1;
 }
 *x=ax>0x3fff?0x3fff:(unsigned)ax;*y=ay>0x3fff?0x3fff:(unsigned)ay;
 return 0;
}
