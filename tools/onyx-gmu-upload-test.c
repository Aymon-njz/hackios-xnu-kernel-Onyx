#include "hakios.onyx_gmu_upload.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct fake { uint32_t mem[0x80000/4]; unsigned writes,drop,lose_reset; };
static struct fake f;
static uint32_t rd(void *c,uint32_t off) {
 struct fake *s=c; assert(off<0x80000 && !(off&3));
 if(off==0x7e000 && s->lose_reset && s->writes) return 0;
 return s->mem[off/4];
}
static void wr(void *c,uint32_t off,uint32_t value) {
 struct fake *s=c;
 assert(off>=0x6d000 && off<0x75000 && !(off&3));
 s->writes++; if(!s->drop) s->mem[off/4]=value;
}
static struct onyx_rsc_io setup(void) {
 memset(&f,0,sizeof(f)); f.mem[0x7e000/4]=1;
 return (struct onyx_rsc_io){.context=&f,.read=rd,.write=wr,.owned=1};
}
int main(int argc,char **argv) {
 /* Two TCM payloads, including last DTCM word, little endian. */
 uint8_t data[]={0,0,0,0,4,0,0,0,0,0,0,0,0,0,0,0,0x78,0x56,0x34,0x12,
  0xfc,0x7f,0,0x10,4,0,0,0,0,0,0,0,0,0,0,0,0xef,0xbe,0xad,0xde};
 struct onyx_gen8_fw_block blocks[64];
 struct onyx_gmu_upload_report r;
 struct onyx_rsc_io io=setup();
 assert(onyx_gmu_upload_tcm(&io,data,sizeof(data),blocks,64,&r)==0);
 assert(r.words_verified==2 && f.writes==2);
 assert(f.mem[0x6d000/4]==0x12345678 && f.mem[0x74ffc/4]==0xdeadbeef);
 io=setup();
 assert(onyx_gmu_upload_tcm(&io,data,sizeof(data)-1,blocks,64,&r)==ONYX_GMU_UPLOAD_IMAGE);
 assert(f.writes==0);
 io=setup(); io.owned=0;
 assert(onyx_gmu_upload_tcm(&io,data,sizeof(data),blocks,64,&r)==ONYX_GMU_UPLOAD_ARGUMENT);
 assert(f.writes==0);
 io=setup(); f.mem[0x7e000/4]=0;
 assert(onyx_gmu_upload_tcm(&io,data,sizeof(data),blocks,64,&r)==ONYX_GMU_UPLOAD_RESET);
 assert(f.writes==0);
 io=setup(); f.drop=1;
 assert(onyx_gmu_upload_tcm(&io,data,sizeof(data),blocks,64,&r)==ONYX_GMU_UPLOAD_READBACK);
 assert(io.quarantined && r.failed_offset==0x6d000 && f.writes==1);
 io=setup(); f.lose_reset=1;
 assert(onyx_gmu_upload_tcm(&io,data,sizeof(data),blocks,64,&r)==ONYX_GMU_UPLOAD_RESET);
 assert(io.quarantined && f.writes==1);
 if(argc==2) {
  FILE *fp=fopen(argv[1],"rb"); assert(fp);
  assert(!fseek(fp,0,SEEK_END)); long size=ftell(fp); assert(size>0);
  rewind(fp); uint8_t *fw=malloc((size_t)size); assert(fw);
  assert(fread(fw,1,(size_t)size,fp)==(size_t)size); fclose(fp);
  io=setup(); assert(onyx_gmu_upload_tcm(&io,fw,(size_t)size,blocks,64,&r)==0);
  assert(r.words_verified>0); printf("vendor TCM: %u words verified in fake MMIO\n",r.words_verified);
  free(fw);
 }
 puts("TCM upload fake-MMIO tests passed");
}
