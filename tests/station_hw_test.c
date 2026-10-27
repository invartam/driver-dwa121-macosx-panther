#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/rtl8188cu/station_hw.h"
#include "../src/control/association.h"
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static struct mock { unsigned char bytes[4096]; unsigned int calls,fail,after,dead,lost,writes,opened; } m;
static const rtl_u8 ap[6]={2,0x12,0x34,0x56,0x78,0x9a};
static const rtl_u8 mac[6]={2,1,2,3,4,5};
static rtl_u32 get(unsigned int a,unsigned int w)
{ unsigned int i; rtl_u32 v=0; for(i=0;i<w;++i) v|=(rtl_u32)m.bytes[a+i]<<(8*i); return v; }
static void set(unsigned int a,unsigned int w,rtl_u32 v)
{ unsigned int i; for(i=0;i<w;++i) m.bytes[a+i]=(rtl_u8)(v>>(8*i)); }
static int rd(void *c,rtl_u16 a,unsigned int w,rtl_u32 *v)
{
    (void)c; CHECK(a+w<=4096); ++m.calls;
    if(m.calls==m.fail || (m.dead && m.calls>=m.dead)) return -1;
    *v=get(a,w); return 0;
}
static int wr(void *c,rtl_u16 a,unsigned int w,rtl_u32 v)
{
    int fail;
    (void)c; CHECK(a+w<=4096); ++m.calls; ++m.writes; fail=m.calls==m.fail;
    if((m.dead && m.calls>=m.dead) || (fail && !m.after)) return -1;
    CHECK(a!=0x680); /* never enable encryption/decryption */
    if(a==0x522 && v!=255) {
        CHECK(v==0x6f); CHECK(get(0x100,2)==0x2ff);
        CHECK(!memcmp(m.bytes+0x610,mac,6) && !memcmp(m.bytes+0x618,ap,6));
        CHECK(get(0x42a,2)==0x0606 && get(0x440,4)==1);
        CHECK(get(0x608,4)==0x9000680eU && get(0x51b,1)==20);
        ++m.opened;
    }
    if(!m.lost) set(a,w,a==0x200 ? v&0x7fffffffU : v);
    return fail ? -1 : 0;
}
static void delay(void *c,unsigned int us) { (void)c;(void)us; }
static struct rtl_bus bus={&m,rd,wr,delay};
static void reset(struct rtl_identity *id)
{
    unsigned int i;
    memset(&m,0,sizeof(m)); memset(id,0,sizeof(*id));
    id->sys_cfg=0x04400735; id->rom[0]=0x29;id->rom[1]=0x81;
    memcpy(id->mac,mac,6);
    for(i=0;i<3;++i) { id->rom[0x5a+i]=20;id->rom[0x60+i]=25;id->rom[0x69+i]=3;id->rom[0x6c+i]=2; }
    set(0x100,2,0x2fa);set(0x522,1,255);set(0x608,4,0x9000680fU);
}
static void hardware(void)
{
    struct rtl_identity id; unsigned int n,i,after; int result;
    reset(&id); CHECK(rtl8188cu_station_hw_begin(&bus,&id,ap,11)==0); n=m.calls;
    CHECK(m.opened==1 && get(0x522,1)==0x6f && get(0x100,2)==0x2ff);
    CHECK(get(0x550,2)==0x1010 && get(0x680,2)==0);
    CHECK(rtl8188cu_station_hw_block(&bus)==0 && get(0x522,1)==255 && get(0x100,2)==0x2fa);
    for(after=0;after<2;++after) for(i=1;i<=n;++i) {
        reset(&id); m.fail=i; m.after=after;
        result=rtl8188cu_station_hw_begin(&bus,&id,ap,11);
        CHECK(result!=RTL_OK); CHECK(get(0x522,1)==255 && get(0x100,2)==0x2fa);
    }
    reset(&id); m.lost=1; CHECK(rtl8188cu_station_hw_begin(&bus,&id,ap,11)!=0 && !m.opened);
    reset(&id); m.dead=40; CHECK(rtl8188cu_station_hw_begin(&bus,&id,ap,11)==RTL_RESTORE);
    reset(&id); CHECK(rtl8188cu_station_hw_begin(&bus,&id,ap,0)==RTL_BAD_ARGUMENT && !m.calls);
    CHECK(rtl8188cu_station_hw_begin(&bus,&id,mac,11)==RTL_BAD_ARGUMENT && !m.calls);
    printf("PASS: station MAC/BSSID, timing, calibrated power, management-only queue; %u injected register faults, failed cleanup detected\n",2*n);
}
static void abi(void)
{
    struct dwa_assoc_request r; struct dwa_assoc_budget budget; unsigned int i;
    CHECK(sizeof(r)==536 && sizeof(struct dwa_assoc_reply)==48);
    memset(&r,0,sizeof(r));r.version=1;r.operation=DWA_ASSOC_BEGIN;r.channel=11;memcpy(r.bssid,ap,6);
    CHECK(dwa_assoc_validate(&r,mac)==0);
    for(i=0;i<512;++i) { r.frame[i]=1;CHECK(dwa_assoc_validate(&r,mac)!=0);r.frame[i]=0; }
    r.reserved[1]=1;CHECK(dwa_assoc_validate(&r,mac)!=0);r.reserved[1]=0;
    r.channel=12;CHECK(dwa_assoc_validate(&r,mac)!=0);
    memset(&r,0,sizeof(r));r.version=1;r.operation=DWA_ASSOC_SEND;r.length=30;r.frame[0]=0xb0;
    CHECK(dwa_assoc_validate(&r,mac)==0);
    r.frame[0]=8;CHECK(dwa_assoc_validate(&r,mac)!=0); /* no EAPOL/data yet */
    r.frame[0]=0;r.length=513;CHECK(dwa_assoc_validate(&r,mac)!=0);
    r.length=28;CHECK(dwa_assoc_validate(&r,mac)==0);
    r.frame[511]=1;CHECK(dwa_assoc_validate(&r,mac)!=0);
    memset(&r,0,sizeof(r));r.version=1;r.operation=DWA_ASSOC_END;
    CHECK(dwa_assoc_validate(&r,mac)==0);r.operation=5;CHECK(dwa_assoc_validate(&r,mac)!=0);
    memset(&budget,0,sizeof(budget));budget.start_ms=0xfffffff0U;
    CHECK(dwa_assoc_admit(&budget,20,0xb0)==0 && dwa_assoc_admit(&budget,20,0)!=0);
    budget.auth=3;CHECK(dwa_assoc_admit(&budget,20,0xb0)!=0 && dwa_assoc_admit(&budget,20,0)==0);
    budget.assoc=1;budget.auth=1;CHECK(dwa_assoc_admit(&budget,20,0xb0)!=0);
    budget.assoc=3;CHECK(dwa_assoc_admit(&budget,20,0)!=0);
    budget.assoc=0;CHECK(dwa_assoc_admit(&budget,0xfffffff0U+9999U,0xb0)==0);
    CHECK(dwa_assoc_admit(&budget,0xfffffff0U+10000U,0xb0)==RTL_TIMEOUT);
    CHECK(dwa_assoc_admit(&budget,20,8)!=0);
    puts("PASS: fixed association ABI, zero padding, management-only operations, 3+3 attempt budget, monotonic deadline and wrap");
}
int main(void) { hardware();abi();return 0; }
