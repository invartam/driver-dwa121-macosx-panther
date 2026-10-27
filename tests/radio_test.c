#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/rtl8188cu/radio.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
struct mock {
    rtl_u8 regs[65536]; rtl_u32 rf[64];
    unsigned int calls, fail_at, shots, latched_rf, lc_starts, lc_reads;
    int fail_after, dead, iq_bad, iq_unstable, lc_stuck, rf_bad, lost_write;
    unsigned int max_delay, elapsed;
};
static rtl_u32 get(struct mock *m,unsigned int a,unsigned int w)
{
    rtl_u32 v=0; unsigned int i;
    for (i=0;i<w;++i) v|=(rtl_u32)m->regs[a+i]<<(8*i);
    return v;
}
static void put(struct mock *m,unsigned int a,unsigned int w,rtl_u32 v)
{
    unsigned int i; for(i=0;i<w;++i)m->regs[a+i]=(rtl_u8)(v>>(8*i));
}
static int readreg(void *c,rtl_u16 a,unsigned int w,rtl_u32 *v)
{
    struct mock *m=c;
    ++m->calls;
    if(m->dead || m->calls==m->fail_at)return -1;
    if(a==0x8a0 || a==0x8b8) {
        if(m->latched_rf==0x18 && !m->lc_stuck) {
            m->rf[0x18]&=~0x8000U; ++m->lc_reads;
        }
        *v=m->rf_bad?0xfffff:m->rf[m->latched_rf]; return 0;
    }
    *v=get(m,a,w);return 0;
}
static int writereg(void *c,rtl_u16 a,unsigned int w,rtl_u32 v)
{
    struct mock *m=c;
    int fail;
    ++m->calls; fail=m->calls==m->fail_at;
    if(m->dead || (fail && !m->fail_after))return -1;
    /* Invariant independent of implementation: no network TX/RX/DMA enabled. */
    if(a==0x100)CHECK(v==0);
    if(a==0x522)CHECK(v==0xff);
    CHECK(w==1 || w==2 || w==4);
    if(m->lost_write && a==0x800)return 0;
    put(m,a,w,v);
    if(a==0x840) {
        unsigned int rfaddr=(v>>20)&0x3f;
        m->rf[rfaddr]=v&0xfffff;
        if(rfaddr==0x18 && (v&0x8000))++m->lc_starts;
    }
    /* Read address is latched on the rising edge of HSSI READ_EDGE. */
    if(a==0x824) {
        if(!(v&0x80000000U))m->regs[0xffff]=0;
        else if(!m->regs[0xffff]) {m->latched_rf=(v>>23)&0x3f;m->regs[0xffff]=1;}
    }
    if(a==0xe48 && v==0xf8000000U) {
        unsigned int delta;
        ++m->shots;
        delta=m->iq_unstable && m->shots%2 ? 20:0;
        put(m,0xe94,4,(0x100U+delta)<<16);
        put(m,0xe9c,4,0x3feU<<16);
        put(m,0xea4,4,0x101U<<16);
        put(m,0xeac,4,(3U<<16) | (m->iq_bad?0x10000000U:0));
    }
    return fail?-1:0;
}
static void delay(void *c,unsigned int us)
{
    struct mock *m=c; m->elapsed+=us;if(us>m->max_delay)m->max_delay=us;
}
static void setup(struct mock *m,struct rtl_bus *b,struct rtl_identity *id)
{
    memset(m,0,sizeof(*m));memset(id,0,sizeof(*id));
    put(m,0x80,4,0x300c6);put(m,0x2,2,0x400);put(m,0x522,1,0xff);
    id->vid=0x2001;id->pid=0x3308;id->sys_cfg=0x04400735;id->pa_setting=0xfe;
    b->cookie=m;b->read=readreg;b->write=writereg;b->delay_us=delay;
}
static void check_stopped(struct mock *m)
{
    CHECK(get(m,0x100,2)==0 && get(m,0x1f,1)==0);
    CHECK(!(get(m,2,2)&0x403));CHECK(get(m,0x80,1)==0);
    CHECK(get(m,0xe28,4)==0 && get(m,0x522,1)==0xff);
}
int main(void)
{
    static struct mock m;
    struct rtl_bus b;
    struct rtl_identity id;
    struct rtl_radio_report r;
    struct rtl_iq_sample a={{256,1023},{256,0},1}, c;
    unsigned int calls,fault,mode,stop_calls;
    int error;
    rtl_u32 tuned;
    setup(&m,&b,&id);
    error=rtl8188cu_radio_init(&b,&id,&r);
    if(error)fprintf(stderr,"radio error=%d stage=%d restore=%d channel=%x/%x bb=%x counts=%u/%u/%u/%u\n",error,r.stage,r.calibration_restore_error,r.rf_channel_before,r.rf_channel_after,r.bb_rfmod,r.mac_writes,r.bb_writes,r.agc_writes,r.rf_writes);
    CHECK(error==0);
    calls=m.calls;
    CHECK(r.stage==RTL_RADIO_DONE && r.iq.valid && r.iq_runs==2 && r.lc_verified);
    CHECK(r.mac_writes==86 && r.bb_writes==187 && r.agc_writes==160 && r.rf_writes==137);
    CHECK(r.rf_channel_after==0x7401 && m.lc_starts==2 && m.shots==2);
    CHECK(get(&m,0x820,4)==0x01000100 && get(&m,0xc04,4)==0x03a05611);
    CHECK(get(&m,0xc80,4)==0x403e0100); /* signed -2 TX correction */
    CHECK(get(&m,0xc14,4)==0x40000d01); /* RX corrections preserve upper bits */
    m.calls=0;CHECK(rtl8188cu_radio_quiesce(&b)==0);check_stopped(&m);
    stop_calls=m.calls;
    setup(&m,&b,&id);m.rf[0x18]=0x7401;
    CHECK(rtl8188cu_set_rx_channel(&b,0,&tuned)==RTL_UNSUPPORTED && m.calls==0);
    CHECK(rtl8188cu_set_rx_channel(&b,12,&tuned)==RTL_UNSUPPORTED && m.calls==0);
    CHECK(rtl8188cu_set_rx_channel(&b,11,&tuned)==0 && tuned==0x740b);
    put(&m,0x100,2,0x9a);
    CHECK(rtl8188cu_set_rx_channel(&b,1,&tuned)==RTL_UNSUPPORTED);
    c=a;CHECK(rtl8188cu_iq_agrees(&a,&c));
    c.tx[0]=262;CHECK(!rtl8188cu_iq_agrees(&a,&c)); /* first component matters */
    c=a;c.tx[1]=1;CHECK(rtl8188cu_iq_agrees(&a,&c)); /* signed wrap */
    c=a;c.rx[0]=250;CHECK(!rtl8188cu_iq_agrees(&a,&c));
    c=a;c.valid=0;CHECK(!rtl8188cu_iq_agrees(&a,&c));
    for(mode=0;mode<2;++mode)for(fault=1;fault<=calls;++fault) {
        setup(&m,&b,&id);m.fail_at=fault;m.fail_after=mode;
        error=rtl8188cu_radio_init(&b,&id,&r);
        if(!error)fprintf(stderr,"fault unexpectedly passed: %u\n",fault);
        CHECK(error!=0);CHECK(r.stage!=RTL_RADIO_DONE);check_stopped(&m);
    }
    for(mode=0;mode<2;++mode)for(fault=1;fault<=stop_calls;++fault) {
        setup(&m,&b,&id);
        CHECK(rtl8188cu_radio_init(&b,&id,&r)==0);
        m.calls=0;m.fail_at=fault;m.fail_after=mode;
        CHECK(rtl8188cu_radio_quiesce(&b)!=0);
        /* Cleanup never silently reports success, even after an applied write. */
    }
    for(mode=0;mode<6;++mode) {
        setup(&m,&b,&id);
        m.iq_bad=mode==0;m.iq_unstable=mode==1;m.lc_stuck=mode==2;
        m.rf_bad=mode==3;m.lost_write=mode==4;m.dead=mode==5;
        CHECK(rtl8188cu_radio_init(&b,&id,&r)!=0);
        CHECK(r.stage!=RTL_RADIO_DONE);
        if(mode==5)CHECK(r.cleanup_error!=0);else check_stopped(&m);
        CHECK(m.calls<3000); /* All failure loops bounded. */
    }
    setup(&m,&b,&id);id.board_type=1;
    CHECK(rtl8188cu_radio_init(&b,&id,&r)==RTL_UNSUPPORTED && m.calls==0);
    setup(&m,&b,&id);id.sys_cfg|=0x80000;
    CHECK(rtl8188cu_radio_init(&b,&id,&r)==RTL_UNSUPPORTED && m.calls==0);
    printf("PASS: radio tables, signed IQ agreement/corrections, LC timeout, restoration, disabled traffic; %u injected USB faults, %u quiesce faults\n",calls*2,stop_calls*2);
    return 0;
}
