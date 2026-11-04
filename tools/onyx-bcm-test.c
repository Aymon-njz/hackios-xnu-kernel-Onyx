#include "hakios.onyx_bcm.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void put(uint8_t *p,uint32_t x){p[0]=x;p[1]=x>>8;p[2]=x>>16;p[3]=x>>24;}
int main(void) {
 uint8_t db[176]={0};put(db+4,0x0c0330db);db[8]=5;db[12]=24;db[14]=1;
 memcpy(db+144,"MC0",3);put(db+160,0x50000);db[164]=8;
 put(db+168,4800000);db[172]=16;
 struct onyx_bcm_resource r={0};
 assert(!onyx_bcm_lookup(db,sizeof(db),"MC0",&r));
 assert(r.address==0x50000 && r.unit==4800000 && r.width==16);
 struct onyx_bcm_resource save=r;
 for(unsigned n=0;n<sizeof(db);n++) {
  assert(onyx_bcm_lookup(db,n,"MC0",&r)==2 && !memcmp(&r,&save,sizeof(r)));
 }
 assert(onyx_bcm_lookup(db,sizeof(db),"SH0",&r)==1);
 struct onyx_bcm_node node={.average=19200000,.peak=19200000,.width=4,.channels=4};
 unsigned x=99,y=99;
 assert(!onyx_bcm_aggregate(&r,&node,1,1000,0,1,&x,&y) && x==4000 && y==16000);
 node.average=node.peak=0;
 assert(!onyx_bcm_aggregate(&r,&node,1,1000,0,1,&x,&y) && x==1 && y==1);
 assert(!onyx_bcm_aggregate(&r,&node,1,1000,1,1,&x,&y) && x==0 && y==1);
 node.peak=UINT64_MAX;x=y=77;
 assert(onyx_bcm_aggregate(&r,&node,1,1000,0,0,&x,&y)==2 && x==77 && y==77);
 db[164]=7;assert(onyx_bcm_lookup(db,sizeof(db),"MC0",&r)==2);
 puts("BCM metadata bounds and bandwidth arithmetic tests passed");
}
