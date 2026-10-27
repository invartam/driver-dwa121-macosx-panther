#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/control/protocol.h"
#include "../src/rtl8188cu/rx.h"
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static struct dwa_frame_queue queue;
static struct dwa_frame out;
static const rtl_u8 mac[6]={2,1,2,3,4,5},ap[6]={2,6,7,8,9,10};
static unsigned int calls,last_kind,last_length;
static void put32(rtl_u8 *p,rtl_u32 v)
{ unsigned int i; for(i=0;i<4;++i) p[i]=(rtl_u8)(v>>(8*i)); }
static void sink(void *cookie,unsigned int kind,const rtl_u8 *f,unsigned int n)
{
    CHECK(cookie==&queue); ++calls; last_kind=kind; last_length=n;
    CHECK(dwa_frame_queue_push(&queue,kind,11,100,f,n)==1);
}
/* Independently expressed bit-at-a-time polynomial division for test FCS. */
static rtl_u32 crc(const rtl_u8 *f,unsigned int n)
{
    rtl_u32 c=0xffffffffU; unsigned int i,b;
    for(i=0;i<n;++i) for(b=0;b<8;++b) {
        rtl_u32 bit=(c^(f[i]>>b))&1U; c>>=1;
        if(bit) c^=0xedb88320U;
    }
    return c^0xffffffffU;
}
static unsigned int pack(rtl_u8 *usb,const rtl_u8 *f,unsigned int n)
{
    memset(usb,0,4096); put32(usb,n+4); usb[10]=1;
    memcpy(usb+24,f,n); put32(usb+24+n,crc(f,n)); return 28+n;
}
static void header(rtl_u8 *f,unsigned int type,unsigned int flags)
{
    memset(f,0,2304); f[0]=(rtl_u8)type; f[1]=(rtl_u8)flags;
    memcpy(f+4,mac,6); memcpy(f+10,ap,6); memcpy(f+16,ap,6);
}
static unsigned int eapol(rtl_u8 *f,int qos)
{
    static const rtl_u8 llc[]={0xaa,0xaa,3,0,0,0,0x88,0x8e};
    unsigned int h=qos?26:24;
    header(f,qos?0x88:8,2); memcpy(f+h,llc,8);
    f[h+8]=2; f[h+9]=3; f[h+11]=95; f[h+12]=2;
    return h+8+99;
}
static void parse_check(const rtl_u8 *f,unsigned int n,const struct rtl_rx_filter *filter,unsigned int kind)
{
    rtl_u8 usb[4096]; struct rtl_rx_stats s; unsigned int bytes=pack(usb,f,n);
    memset(&s,0,sizeof(s)); dwa_frame_queue_reset(&queue); calls=0;
    CHECK(rtl8188cu_rx_parse_control(usb,bytes,&s,filter,sink,&queue)==0);
    CHECK(calls==(kind?1U:0U)); CHECK(s.accepted==calls);
    if(filter->selected && n>=30 && n<=2304 && f[0]==0xb0) {
        CHECK(s.auth_seen==1 && s.auth_last_length==n);
        CHECK(s.auth_to_us==(memcmp(f+4,filter->mac,6)==0));
        CHECK(s.auth_from_ap==(memcmp(f+10,filter->bssid,6)==0));
    }
    if(kind) CHECK(last_kind==kind && last_length==n && queue.count==1);
}
int main(void)
{
    rtl_u8 f[2304],copy[2304],usb[4096]; struct rtl_rx_filter filter;
    struct rtl_rx_stats stats; struct dwa_control_request r;
    unsigned int i,j,n,bytes,seed=12345;
    CHECK(sizeof(r)==16 && sizeof(struct dwa_control_reply)==2364);
    memset(&r,0,sizeof(r)); r.version=1; r.operation=DWA_CONTROL_STATUS;
    CHECK(dwa_control_validate(&r,mac)==0);
    r.bssid[0]=2; CHECK(dwa_control_validate(&r,mac)!=0);
    r.operation=DWA_CONTROL_SELECT; CHECK(dwa_control_validate(&r,mac)==0);
    r.bssid[0]=3; CHECK(dwa_control_validate(&r,mac)!=0);
    memcpy(r.bssid,mac,6); CHECK(dwa_control_validate(&r,mac)!=0);
    memset(r.bssid,0,6); CHECK(dwa_control_validate(&r,mac)!=0);
    memcpy(r.bssid,ap,6); CHECK(dwa_control_validate(&r,mac)==0);
    r.version=2; CHECK(dwa_control_validate(&r,mac)!=0); r.version=1;
    r.reserved[1]=1; CHECK(dwa_control_validate(&r,mac)!=0); r.reserved[1]=0;
    r.operation=5; CHECK(dwa_control_validate(&r,mac)!=0);
    CHECK(dwa_control_validate(0,mac)!=0 && dwa_control_validate(&r,0)!=0);
    /* Overflow prioritizes control, preserves ordering, and never leaks old data. */
    memset(f,0xa5,sizeof(f)); dwa_frame_queue_reset(&queue);
    CHECK(dwa_frame_queue_push(&queue,1,11,100,f,2305)<0);
    CHECK(dwa_frame_queue_push(&queue,1,12,100,f,1)<0);
    CHECK(dwa_frame_queue_push(&queue,5,11,100,f,1)<0);
    for(i=0;i<16;++i) { f[0]=(rtl_u8)i; CHECK(dwa_frame_queue_push(&queue,i==3?1:2,11,100,f,1)==1); }
    CHECK(dwa_frame_queue_push(&queue,1,11,100,f,1)==0);
    f[0]=16; CHECK(dwa_frame_queue_push(&queue,3,11,100,f,1)==1);
    CHECK(dwa_frame_queue_push(&queue,2,11,100,f,1)==0 && queue.dropped==3);
    for(i=0;i<17;++i) {
        if(i==3) continue;
        CHECK(dwa_frame_queue_pop(&queue,5100,&out)==1 && out.data[0]==i);
        for(j=1;j<sizeof(out.data);++j) CHECK(out.data[j]==0);
    }
    memset(&out,0xa5,sizeof(out)); CHECK(dwa_frame_queue_pop(&queue,5100,&out)==0);
    for(i=0;i<sizeof(out);++i) CHECK(((rtl_u8 *)&out)[i]==0);
    for(i=0;i<sizeof(queue.slots);++i) CHECK(((rtl_u8 *)queue.slots)[i]==0);
    /* Repeat at wrapped ring positions, discard expired frames, wrap milliseconds. */
    for(i=0;i<100;++i) {
        CHECK(dwa_frame_queue_push(&queue,1,11,0xfffffff0U,f,2304)==1);
        CHECK(dwa_frame_queue_pop(&queue,20,&out)==1 && out.length==2304);
    }
    CHECK(dwa_frame_queue_push(&queue,1,11,100,f,1)==1);
    CHECK(dwa_frame_queue_push(&queue,3,11,101,f,1)==1);
    CHECK(dwa_frame_queue_pop(&queue,5101,&out)==1 && out.kind==3 && queue.expired==1);
    dwa_frame_queue_reset(&queue);
    for(i=0;i<sizeof(queue);++i) CHECK(((rtl_u8 *)&queue)[i]==0);
    for(i=0;i<16;++i)CHECK(dwa_frame_queue_push(&queue,4,11,100,f,48)==1);
    CHECK(dwa_frame_queue_push(&queue,4,11,100,f,48)==0);
    CHECK(dwa_frame_queue_push(&queue,3,11,100,f,131)==1); /* data flood cannot block EAPOL */
    CHECK(dwa_frame_queue_push(&queue,2,11,100,f,26)==1);
    dwa_frame_queue_reset(&queue);
    memset(&filter,0,sizeof(filter)); memcpy(filter.mac,mac,6); memcpy(filter.bssid,ap,6);
    header(f,0xb0,0); parse_check(f,30,&filter,0);
    filter.selected=1; parse_check(f,30,&filter,2);
    /* Real APs append optional IEs to open authentication replies. */
    f[30]=221; f[31]=9; memset(f+32,0,9); parse_check(f,41,&filter,2);
    parse_check(f,40,&filter,0); parse_check(f,31,&filter,0);
    f[31]=255; parse_check(f,41,&filter,0);
    header(f,0xb0,0);
    for(i=0;i<30;++i) parse_check(f,i,&filter,0);
    for(i=0;i<6;++i) {
        memcpy(copy,f,30);
        if(i<3) copy[4+i*6]^=2;
        if(i==3) copy[1]|=0x40;
        if(i==4) copy[1]|=4;
        if(i==5) copy[22]=1;
        parse_check(copy,30,&filter,0);
    }
    f[1]=8; parse_check(f,30,&filter,2); /* retry is legal */
    header(f,0x10,0); parse_check(f,30,&filter,2);
    f[30]=1; f[31]=2; f[32]=0x82; f[33]=0x84; parse_check(f,34,&filter,2);
    parse_check(f,33,&filter,0);
    header(f,0xc0,0); parse_check(f,26,&filter,2);
    header(f,0xa0,0); parse_check(f,26,&filter,2);
    for(j=0;j<2;++j) {
        n=eapol(f,(int)j); parse_check(f,n,&filter,3);
        for(i=0;i<n;++i) parse_check(f,i,&filter,0);
        for(i=0;i<10;++i) {
            memcpy(copy,f,n);
            if(i<3) copy[4+i*6]^=2;
            if(i==3) copy[1]|=0x40;
            if(i==4) copy[1]=1;
            if(i==5) copy[(j?26:24)+6]=0;
            if(i==6) copy[(j?26:24)+9]=0;
            if(i==7) copy[(j?26:24)+11]++;
            if(i==8) copy[(j?26:24)+12]=1;
            if(i==9) copy[1]|=0x80;
            parse_check(copy,n,&filter,0);
        }
        if(j) { f[24]=0x80; parse_check(f,n,&filter,0); }
    }
    n=eapol(f,0); bytes=pack(usb,f,n); usb[bytes-1]^=1;
    memset(&stats,0,sizeof(stats)); calls=0;
    CHECK(rtl8188cu_rx_parse_control(usb,bytes,&stats,&filter,sink,&queue)==0);
    CHECK(!calls && stats.fcs_errors==1);
    /* Hardware error flags reject even with a good FCS. */
    for(i=0;i<2;++i) {
        pack(usb,f,n); usb[1]|=i?0x80:0x40; memset(&stats,0,sizeof(stats)); calls=0;
        CHECK(rtl8188cu_rx_parse_control(usb,bytes,&stats,&filter,sink,&queue)==0 && !calls);
        CHECK(stats.crc_errors+stats.icv_errors==1);
    }
    /* Arbitrary USB records are bounded; structurally valid mutations exercise filters. */
    for(j=0;j<2;++j) {
        unsigned int h=j?26:24;
        header(f,j?0x88:8,0x42);f[h]=1;f[h+3]=0x20;n=h+24;
        bytes=pack(usb,f,n);usb[3]|=8;calls=0;memset(&stats,0,sizeof(stats));
        CHECK(rtl8188cu_rx_parse_control(usb,bytes,&stats,&filter,sink,&queue)==0&&!calls);
        filter.encrypted=1;dwa_frame_queue_reset(&queue);
        CHECK(rtl8188cu_rx_parse_control(usb,bytes,&stats,&filter,sink,&queue)==0&&calls==1&&last_kind==4&&stats.encrypted==1);
        for(i=0;i<7;++i) {
            memcpy(copy,f,n);
            if(i==0)copy[10]^=2;
            if(i==1)copy[4]^=2;
            if(i==2)copy[1]|=4;
            if(i==3)copy[h+3]=0;
            if(i==4)copy[22]=1;
            if(i==5)copy[h+3]|=0x40; /* unicast group KeyID */
            bytes=pack(usb,copy,n);if(i!=6)usb[3]|=8;calls=0;dwa_frame_queue_reset(&queue);
            CHECK(rtl8188cu_rx_parse_control(usb,bytes,&stats,&filter,sink,&queue)==0&&!calls);
        }
        f[4]=1;f[h+3]=0x60;bytes=pack(usb,f,n);usb[3]|=8;calls=0;
        CHECK(rtl8188cu_rx_parse_control(usb,bytes,&stats,&filter,sink,&queue)==0&&calls==1);
        usb[bytes-1]^=1;calls=0;
        CHECK(rtl8188cu_rx_parse_control(usb,bytes,&stats,&filter,sink,&queue)==0&&!calls);
        filter.encrypted=0;
    }
    for(i=0;i<10000;++i) {
        seed=seed*1664525U+1013904223U; n=seed%sizeof(usb);
        for(j=0;j<n;++j) { seed=seed*1664525U+1013904223U; usb[j]=(rtl_u8)(seed>>24); }
        dwa_frame_queue_reset(&queue); memset(&stats,0,sizeof(stats));
        rtl8188cu_rx_parse_control(usb,n,&stats,&filter,sink,&queue);
    }
    puts("PASS: control ABI, selected-peer/FCS/EAPOL filters, truncations, 10000 USB fuzz cases; queue priority, wipe, expiry and clock/ring wrap");
    puts("PASS: opt-in raw CCMP RX, SWDEC required, selected AP/destination/KeyID checks, multicast/QoS, FCS and EAPOL priority over data flood");
    return 0;
}
