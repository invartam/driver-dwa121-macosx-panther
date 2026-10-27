#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/usb/tx_lifecycle.h"
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1); } } while(0)
struct model {
    struct rtl_tx_lifecycle life;
    unsigned int accepted, callbacks, consumed, refs, aborts, data, zlp;
};
static void verify(struct model *m)
{
    CHECK(!m->life.ready || m->life.pending);
    CHECK(!m->life.abort_requested || (m->life.pending && m->life.stopped));
    CHECK(m->consumed<=m->callbacks && m->callbacks<=m->accepted);
    CHECK(m->accepted-m->consumed==m->life.pending);
    CHECK(m->refs==1+m->accepted-m->callbacks && m->refs<=2);
    CHECK(m->data<=1 && m->zlp<=1);
    if(m->life.phase==RTL_TX_DONE) CHECK(!m->life.pending && m->life.result);
    if(m->life.result==RTL_TX_SUCCESS) {
        CHECK(!m->life.pending && !m->life.ready);
        CHECK(m->data || m->zlp);
        if(m->life.needs_zlp) CHECK(m->zlp==1);
    }
}
static void complete(struct model *m)
{
    if(rtl_tx_life_complete(&m->life)) { ++m->callbacks; --m->refs; }
    verify(m);
}
static void consume(struct model *m,int success)
{
    if(rtl_tx_life_consume(&m->life,success)) ++m->consumed;
    verify(m);
}
static void stop(struct model *m,int failed)
{
    if(rtl_tx_life_stop(&m->life,failed)) ++m->aborts;
    CHECK(m->life.stopped); verify(m);
}
static void submit(struct model *m,unsigned int mode)
{
    if(rtl_tx_life_reserve(&m->life)) {
        ++m->refs;
        if(mode==2) { CHECK(rtl_tx_life_reject(&m->life)); --m->refs; }
        else {
            ++m->accepted;
            if(m->life.phase==RTL_TX_DATA) ++m->data; else ++m->zlp;
            if(mode==1) complete(m); /* inline callback before Write returns */
        }
    }
    verify(m);
}
static void initialize(struct model *m,int zlp,int empty)
{
    memset(m,0,sizeof(*m)); m->refs=1;
    CHECK(rtl_tx_life_begin(&m->life,zlp,empty));
}
int main(void)
{
    struct model m; unsigned int sequence,word,i,mode,n,result;
    /* Normal 64/512 boundary decision is tested in tx_hardware_test.
     * Here data success cannot finish a transaction that still needs a ZLP. */
    for(mode=0;mode<2;++mode) {
        initialize(&m,mode,0); submit(&m,1); CHECK(!rtl_tx_life_take(&m.life));
        consume(&m,1);
        if(mode) {
            CHECK(m.life.phase==RTL_TX_ZLP && !m.life.result);
            CHECK(!rtl_tx_life_take(&m.life));
            submit(&m,1); consume(&m,1);
        }
        CHECK(m.life.result==RTL_TX_SUCCESS && m.refs==1);
        CHECK(!rtl_tx_life_begin(&m.life,0,0)); /* completion must be acknowledged */
        CHECK(rtl_tx_life_take(&m.life)==RTL_TX_SUCCESS && !rtl_tx_life_take(&m.life));
        CHECK(rtl_tx_life_begin(&m.life,0,0));
    }
    /* Failure/rejection during DATA or ZLP must poison the transaction and
     * prohibit retries; the peer may already have received part/all of it. */
    for(mode=0;mode<2;++mode) for(n=0;n<2;++n) {
        initialize(&m,1,0);
        if(mode) { submit(&m,1); consume(&m,1); }
        submit(&m,n?2:1); if(!n) consume(&m,0);
        CHECK(rtl_tx_life_take(&m.life)==RTL_TX_FAILED && m.life.stopped);
        CHECK(!rtl_tx_life_begin(&m.life,0,0) && m.refs==1);
    }
    initialize(&m,1,0); submit(&m,1); consume(&m,1); stop(&m,0);
    CHECK(rtl_tx_life_take(&m.life)==RTL_TX_CANCELLED && !m.zlp);
    /* Missing callback keeps the DMA reference, including on watchdog stop. */
    initialize(&m,0,1); submit(&m,0); stop(&m,1);
    for(i=0;i<500;++i) { stop(&m,0); consume(&m,1); CHECK(m.refs==2 && !rtl_tx_life_take(&m.life)); }
    CHECK(m.aborts==1); complete(&m); consume(&m,1);
    CHECK(rtl_tx_life_take(&m.life)==RTL_TX_FAILED && m.refs==1);
    /* 3 x 7^7 schedules: delayed/inline/rejected Write, callback, success/error
     * processing and stop. Shared real driver state machine, independent
     * ownership/accounting oracle. Each sequence finally drains or preserves
     * callback ownership until a simulated late callback arrives. */
    for(mode=0;mode<3;++mode) for(sequence=0;sequence<823543;++sequence) {
        initialize(&m,mode==1,mode==2); word=sequence;
        for(i=0;i<7;++i) {
            switch(word%7) {
            case 0: submit(&m,0); break;
            case 1: submit(&m,1); break;
            case 2: submit(&m,2); break;
            case 3: complete(&m); break;
            case 4: consume(&m,1); break;
            case 5: consume(&m,0); break;
            case 6: stop(&m,0); break;
            }
            word/=7;
        }
        stop(&m,0); complete(&m); consume(&m,1);
        CHECK(m.refs==1 && !m.life.pending && !m.life.ready && m.aborts<=1);
        result=rtl_tx_life_take(&m.life); CHECK(result>=1 && result<=3);
        CHECK(!rtl_tx_life_take(&m.life) && !rtl_tx_life_begin(&m.life,0,0));
    }
    CHECK(!rtl_tx_life_begin(0,0,0) && !rtl_tx_life_reserve(0) && !rtl_tx_life_complete(0));
    CHECK(!rtl_tx_life_consume(0,1) && !rtl_tx_life_reject(0) && !rtl_tx_life_stop(0,0) && !rtl_tx_life_take(0));
    puts("PASS: 2470629 TX schedules; DATA/ZLP completion, busy ownership, inline/delayed/rejected requests, failed ZLP, stop/watchdog/late callback, no ambiguous retry");
    return 0;
}
