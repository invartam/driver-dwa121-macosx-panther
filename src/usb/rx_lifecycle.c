#include "rx_lifecycle.h"
int rtl_rx_life_start(struct rtl_rx_lifecycle *s)
{
    if(!s||s->running||s->pending||s->ready)return 0;
    s->running=1;s->abort_requested=0;return 1;
}
int rtl_rx_life_reserve(struct rtl_rx_lifecycle *s)
{
    if(!s||!s->running||s->pending||s->ready)return 0;
    s->pending=1;s->abort_requested=0;return 1;
}
int rtl_rx_life_complete(struct rtl_rx_lifecycle *s)
{
    if(!s||!s->pending||s->ready)return 0;
    s->ready=1;return 1;
}
int rtl_rx_life_consume(struct rtl_rx_lifecycle *s,int *deliver)
{
    if(!s||!deliver||!s->pending||!s->ready)return 0;
    *deliver=s->running!=0;
    s->pending=0;s->ready=0;s->abort_requested=0;return 1;
}
int rtl_rx_life_stop(struct rtl_rx_lifecycle *s)
{
    if(!s)return 0;
    s->running=0;
    if(s->pending&&!s->ready&&!s->abort_requested){s->abort_requested=1;return 1;}
    return 0;
}
int rtl_rx_life_reject(struct rtl_rx_lifecycle *s)
{
    if(!s||!s->pending||s->ready)return 0;
    s->running=0;s->pending=0;s->abort_requested=0;return 1;
}
