#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/rtl8188cu/rx.h"
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);} }while(0)
static void put(rtl_u8 *p,rtl_u32 v,unsigned int w)
{unsigned int i;for(i=0;i<w;++i)p[i]=(rtl_u8)(v>>(8*i));}
static rtl_u32 get(rtl_u8 *p,unsigned int w)
{unsigned int i;rtl_u32 v=0;for(i=0;i<w;++i)v|=(rtl_u32)p[i]<<(8*i);return v;}
/* Fixture generated with Python zlib.crc32, independent of the C parser. */
static const rtl_u8 beacon[] = {0x80,0x0,0x0,0x0,0xff,0xff,0xff,0xff,0xff,0xff,0x2,0x11,0x22,0x33,0x44,0x55,0x2,0x11,0x22,0x33,0x44,0x55,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x64,0x0,0x0,0x0,0x0,0x2,0x78,0x78,0xf2,0x88,0x51,0xef};
struct mock {rtl_u8 regs[65536],llt[256];unsigned int calls,fail;int after,timeout,corrupt;};
static int rd(void *c,rtl_u16 a,unsigned int w,rtl_u32 *v)
{struct mock*m=c;++m->calls;if(m->calls==m->fail)return -1;*v=get(m->regs+a,w);return 0;}
static int wr(void *c,rtl_u16 a,unsigned int w,rtl_u32 v)
{
    struct mock*m=c;int fail;unsigned int addr;
    ++m->calls;fail=m->calls==m->fail;if(fail&&!m->after)return -1;
    if(a==0x100)CHECK(v==0||v==0x2fa); /* TX DMA remains disabled; the MAC TX clock is enabled. */
    if(a==0x522)CHECK(v==0xff);
    if(a==0x1e0&&!m->timeout){
        addr=(v>>8)&255;
        if(v&0x40000000)m->llt[addr]=(rtl_u8)v;
        v=(addr<<8)|(m->llt[addr]^(m->corrupt?1:0));
    }
    put(m->regs+a,v,w);return fail?-1:0;
}
static void delay(void*c,unsigned int us){(void)c;CHECK(us==5);}
static void setup(struct mock*m,struct rtl_bus*b)
{memset(m,0,sizeof(*m));b->cookie=m;b->read=rd;b->write=wr;b->delay_us=delay;}
static unsigned int packet(rtl_u8 *p,unsigned int shift)
{
    unsigned int len=24+32+shift+sizeof(beacon);
    memset(p,0,128);put(p,(rtl_u32)sizeof(beacon)|(4U<<16)|(shift<<24),4);
    put(p+8,1U<<16,4);memcpy(p+24+32+shift,beacon,sizeof(beacon));return len;
}
int main(void)
{
    static struct mock m;static rtl_u8 buf[RTL_RX_BUFFER_SIZE+1];
    struct rtl_bus b;struct rtl_dma_report r;struct rtl_rx_stats s;
    unsigned int calls,fault,mode,n,len,seed=0x12345678,enable_calls,stop_calls,control_calls;
    setup(&m,&b);CHECK(rtl8188cu_dma_init(&b,2,&r)==0);calls=m.calls;
    CHECK(r.llt_written==256&&r.llt_verified==256);
    CHECK(m.llt[247]==248&&m.llt[248]==255&&m.llt[249]==250&&m.llt[255]==249);
    CHECK(get(m.regs+0x200,4)==0x80ea000cU&&get(m.regs+0x116,2)==0x27ff);
    CHECK((get(m.regs+0x10c,2)&0xfff4)==0xfaf4);
    CHECK(get(m.regs+0x280,1)==48&&get(m.regs+0xfe5b,1)==4);
    m.calls=0;CHECK(rtl8188cu_rx_enable(&b,&r)==0);enable_calls=m.calls;
    CHECK(r.control_at_rx==0x2fa&&get(m.regs+0x6a0,2)==0xffff);
    m.calls=0;CHECK(rtl8188cu_rx_control_enable(&b)==0);control_calls=m.calls;
    CHECK(get(m.regs+0x608,4)==0x9000680fU && get(m.regs+0x6a4,2)==0xffff);
    CHECK(get(m.regs+0x100,2)==0x2fa && get(m.regs+0x522,1)==0xff && !get(m.regs+0x680,2));
    m.calls=0;CHECK(rtl8188cu_rx_disable(&b)==0);stop_calls=m.calls;
    for(mode=0;mode<2;++mode)for(fault=1;fault<=calls;++fault){
        setup(&m,&b);m.fail=fault;m.after=mode;
        CHECK(rtl8188cu_dma_init(&b,2,&r)!=0);CHECK(get(m.regs+0x100,2)==0);
    }
    for(mode=0;mode<2;++mode)for(fault=1;fault<=enable_calls;++fault){
        setup(&m,&b);r.llt_verified=256;m.fail=fault;m.after=mode;
        CHECK(rtl8188cu_rx_enable(&b,&r)!=0);CHECK(get(m.regs+0x100,2)==0);
    }
    for(fault=1;fault<=stop_calls;++fault){
        setup(&m,&b);m.fail=fault;CHECK(rtl8188cu_rx_disable(&b)!=0);
    }
    for(mode=0;mode<2;++mode)for(fault=1;fault<=control_calls;++fault){
        setup(&m,&b);r.llt_verified=256;CHECK(rtl8188cu_rx_enable(&b,&r)==0);
        m.calls=0;m.fail=fault;m.after=mode;
        CHECK(rtl8188cu_rx_control_enable(&b)!=0 && get(m.regs+0x100,2)==0);
        CHECK(get(m.regs+0x6a4,2)==0);
    }
    setup(&m,&b);CHECK(rtl8188cu_rx_control_enable(&b)==RTL_UNSUPPORTED);
    setup(&m,&b);m.timeout=1;CHECK(rtl8188cu_dma_init(&b,2,&r)==RTL_TIMEOUT);
    setup(&m,&b);m.corrupt=1;CHECK(rtl8188cu_dma_init(&b,2,&r)==RTL_IO);
    setup(&m,&b);CHECK(rtl8188cu_dma_init(&b,3,&r)==RTL_UNSUPPORTED&&m.calls==0);
    for(n=0;n<4;++n){
        len=packet(buf,n);memset(&s,0,sizeof(s));
        CHECK(rtl8188cu_rx_parse(buf,len,&s)==0&&s.beacons==1&&s.bytes==40);
        for(mode=0;mode<len;++mode){
            memset(&s,0,sizeof(s));CHECK(rtl8188cu_rx_parse(buf,mode,&s)==RTL_RX_MALFORMED&&s.beacons==0);
        }
    }
    len=packet(buf,0);packet(buf+128,3);put(buf+8,2U<<16,4);memset(&s,0,sizeof(s));
    CHECK(rtl8188cu_rx_parse(buf,128+len+3,&s)==0&&s.beacons==2);
    len=packet(buf,0);buf[56+38]^=1;memset(&s,0,sizeof(s));
    CHECK(rtl8188cu_rx_parse(buf,len,&s)==0&&s.fcs_errors==1&&!s.accepted);
    len=packet(buf,0);buf[1]|=0x40;memset(&s,0,sizeof(s));
    CHECK(rtl8188cu_rx_parse(buf,len,&s)==0&&s.crc_errors==1&&!s.accepted);
    len=packet(buf,0);buf[1]|=0x80;memset(&s,0,sizeof(s));
    CHECK(rtl8188cu_rx_parse(buf,len,&s)==0&&s.icv_errors==1&&!s.accepted);
    for(n=0;n<10000;++n){
        unsigned int i;
        seed=seed*1664525U+1013904223U;len=seed%(sizeof(buf));
        for(i=0;i<len;++i){seed=seed*1664525U+1013904223U;buf[i]=(rtl_u8)(seed>>24);}
        memset(&s,0,sizeof(s));rtl8188cu_rx_parse(buf,len,&s);
    }
    printf("PASS: LLT readback/topology, control RX filter, TX DMA/queues blocked, %u injected DMA/RX faults; descriptor bounds/shift/aggregation/FCS, 10000 malformed-buffer trials\n",calls*2+enable_calls*2+stop_calls+control_calls*2);
    return 0;
}
