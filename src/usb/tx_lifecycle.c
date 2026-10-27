#include "tx_lifecycle.h"
static void finish(struct rtl_tx_lifecycle *s,unsigned int result)
{ s->phase=RTL_TX_DONE; s->result=result; }
int rtl_tx_life_begin(struct rtl_tx_lifecycle *s,int zlp,int empty)
{
    if(!s || s->stopped || s->phase!=RTL_TX_IDLE || s->pending || s->ready) return 0;
    s->phase=empty?RTL_TX_ZLP:RTL_TX_DATA; s->needs_zlp=zlp!=0;
    s->result=0; s->abort_requested=0; return 1;
}
int rtl_tx_life_reserve(struct rtl_tx_lifecycle *s)
{
    if(!s || s->stopped || s->pending || s->ready ||
       (s->phase!=RTL_TX_DATA && s->phase!=RTL_TX_ZLP)) return 0;
    s->pending=1; s->abort_requested=0; return 1;
}
int rtl_tx_life_complete(struct rtl_tx_lifecycle *s)
{
    if(!s || !s->pending || s->ready) return 0;
    s->ready=1; return 1;
}
int rtl_tx_life_consume(struct rtl_tx_lifecycle *s,int success)
{
    if(!s || !s->pending || !s->ready) return 0;
    s->pending=s->ready=s->abort_requested=0;
    if(s->stopped) finish(s,s->stop_result);
    else if(!success) {
        s->stopped=1; s->stop_result=RTL_TX_FAILED; finish(s,RTL_TX_FAILED);
    } else if(s->phase==RTL_TX_DATA && s->needs_zlp) s->phase=RTL_TX_ZLP;
    else finish(s,RTL_TX_SUCCESS);
    return 1;
}
int rtl_tx_life_reject(struct rtl_tx_lifecycle *s)
{
    if(!s || !s->pending || s->ready) return 0;
    s->pending=s->abort_requested=0;
    if(!s->stopped) { s->stopped=1; s->stop_result=RTL_TX_FAILED; }
    finish(s,s->stop_result); return 1;
}
int rtl_tx_life_stop(struct rtl_tx_lifecycle *s,int failed)
{
    if(!s) return 0;
    if(!s->stopped) s->stop_result=failed?RTL_TX_FAILED:RTL_TX_CANCELLED;
    s->stopped=1;
    if(s->pending) {
        if(!s->ready && !s->abort_requested) { s->abort_requested=1; return 1; }
    } else if(s->phase==RTL_TX_DATA || s->phase==RTL_TX_ZLP) finish(s,s->stop_result);
    return 0;
}
unsigned int rtl_tx_life_take(struct rtl_tx_lifecycle *s)
{
    unsigned int result;
    if(!s || s->phase!=RTL_TX_DONE || s->pending || s->ready) return 0;
    result=s->result; s->result=0; s->phase=RTL_TX_IDLE; s->needs_zlp=0;
    return result;
}
