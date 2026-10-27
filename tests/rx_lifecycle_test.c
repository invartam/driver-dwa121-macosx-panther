#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/usb/rx_lifecycle.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
struct model {
    struct rtl_rx_lifecycle life;
    unsigned int submitted, completed, consumed, refs, stops, delivered, aborts;
};
static void verify(struct model *m)
{
    CHECK(!m->life.ready||m->life.pending);
    CHECK(!m->life.abort_requested||(!m->life.running&&m->life.pending));
    CHECK(m->completed<=m->submitted&&m->consumed<=m->completed);
    CHECK(m->submitted-m->consumed==m->life.pending);
    CHECK(m->refs==1+m->submitted-m->completed);
    CHECK(m->refs>=1&&m->refs<=2);
}
static void complete(struct model *m)
{
    if(m->life.pending&&!m->life.ready){
        CHECK(rtl_rx_life_complete(&m->life));++m->completed;--m->refs;
    }
    verify(m);
}
static void consume(struct model *m)
{
    int deliver=-1;
    if(rtl_rx_life_consume(&m->life,&deliver)){
        ++m->consumed;if(deliver)++m->delivered;
        CHECK(!m->stops||!deliver);
    }
    verify(m);
}
static void stop(struct model *m)
{
    ++m->stops;if(rtl_rx_life_stop(&m->life))++m->aborts;
    CHECK(!m->life.running);verify(m);
}
static void submit(struct model *m,unsigned int mode)
{
    if(rtl_rx_life_reserve(&m->life)){
        CHECK(!m->stops);++m->refs;
        if(mode==2){
            CHECK(rtl_rx_life_reject(&m->life));--m->refs;
        }else{
            ++m->submitted;
            if(mode==1)complete(m); /* before submit returns */
        }
    }
    verify(m);
}
static void initialize(struct model *m)
{memset(m,0,sizeof(*m));m->refs=1;CHECK(rtl_rx_life_start(&m->life));}
int main(void)
{
    struct model m;unsigned int i,j,mode,sequence,word;int deliver;
    /* All placements of stop around submission/inline or delayed completion,
     * duplicate stop and drain. Buffer cannot be reserved while pending/ready. */
    for(mode=0;mode<3;++mode)for(i=0;i<5;++i){
        initialize(&m);
        for(j=0;j<5;++j){
            if(i==j)stop(&m);
            if(j==0)submit(&m,mode);
            if(j==1&&m.life.pending)CHECK(!rtl_rx_life_reserve(&m.life));
            if(j==2)complete(&m);
            if(j==3)consume(&m);
            if(j==4)stop(&m);
        }
        stop(&m);complete(&m);consume(&m);CHECK(m.refs==1&&!m.life.pending);
        CHECK(!rtl_rx_life_complete(&m.life));CHECK(!rtl_rx_life_reject(&m.life));
    }
    /* Exhaustive 6^7 schedules: async/inline submission, callback, workloop,
     * stop, submission error. Simulate one kernel owner + callback reference. */
    for(sequence=0;sequence<279936;++sequence){
        initialize(&m);word=sequence;
        for(i=0;i<7;++i){
            switch(word%6){
            case 0:submit(&m,0);break;
            case 1:submit(&m,1);break;
            case 2:complete(&m);break;
            case 3:consume(&m);break;
            case 4:stop(&m);break;
            case 5:submit(&m,2);break;
            }
            word/=6;
        }
        stop(&m);complete(&m);consume(&m);
        CHECK(!m.life.pending&&!m.life.running&&m.refs==1);
    }
    initialize(&m);submit(&m,0);stop(&m);
    /* A missing callback must keep DMA resources pinned, not fake a drain. */
    for(i=0;i<500;++i){consume(&m);CHECK(m.life.pending&&m.refs==2);}
    CHECK(m.aborts==1);complete(&m);consume(&m);CHECK(m.refs==1&&!m.delivered);
    memset(&m,0,sizeof(m));CHECK(!rtl_rx_life_consume(&m.life,&deliver));
    CHECK(!rtl_rx_life_start(0)&&!rtl_rx_life_reserve(0)&&!rtl_rx_life_complete(0));
    CHECK(!rtl_rx_life_stop(0)&&!rtl_rx_life_reject(0)&&!rtl_rx_life_consume(0,&deliver));
    puts("PASS: 279936 RX schedules, inline/delayed completion, submission rejection, stop/abort/drain ordering, no rearm after stop, late callback ownership");
    return 0;
}
