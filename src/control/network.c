#include "network.h"
static void clear(void *p,unsigned int n)
{ volatile rtl_u8 *q=(volatile rtl_u8 *)p;while(n--)*q++=0; }
static void copy(rtl_u8 *d,const rtl_u8 *s,unsigned int n)
{ while(n--)*d++=*s++; }
static int same(const rtl_u8 *a,const rtl_u8 *b,unsigned int n)
{ while(n--)if(*a++!=*b++)return 0;return 1; }
int dwa_net_validate(const struct dwa_net_request *r)
{
    unsigned int i;
    if(!r||r->version!=DWA_NET_VERSION||r->operation<1||r->operation>5)return 0;
    if(r->operation==DWA_NET_RX){if(r->length<14||r->length>DWA_NET_FRAME_MAX)return 0;}
    else if(r->length)return 0;
    for(i=r->length;i<sizeof(r->data);++i)if(r->data[i])return 0;
    return 1;
}
void dwa_net_reset(struct dwa_net_bridge *b) { clear(b,sizeof(*b)); }
void dwa_net_open(struct dwa_net_bridge *b,const rtl_u8 *mac,rtl_u32 now)
{ dwa_net_reset(b);copy(b->mac,mac,6);b->pulse=now;b->active=1; }
int dwa_net_active(const struct dwa_net_bridge *b,rtl_u32 now)
{ return b&&b->active&&(rtl_u32)(now-b->pulse)<DWA_NET_HEARTBEAT_MS; }
int dwa_net_push(struct dwa_net_bridge *b,int rx,const rtl_u8 *data,unsigned int n,rtl_u32 now)
{
    struct dwa_net_queue *q;struct dwa_net_frame *f;unsigned int type;
    if(!dwa_net_active(b,now)||!data||n<14||n>DWA_NET_FRAME_MAX)return 0;
    type=((unsigned int)data[12]<<8)|data[13];
    if(type<1536||type==0x888e||type==0x80f3||type==0x8137)return 0;
    if(rx ? (!(data[0]&1)&&!same(data,b->mac,6)) : !same(data+6,b->mac,6))return 0;
    q=rx?&b->rx:&b->tx;
    if(q->count==DWA_NET_SLOTS){++q->dropped;return 0;}
    f=&q->frames[(q->head+q->count)%DWA_NET_SLOTS];
    clear(f,sizeof(*f));f->length=n;f->time=now;copy(f->data,data,n);++q->count;return 1;
}
int dwa_net_pop(struct dwa_net_bridge *b,int rx,struct dwa_net_frame *out,rtl_u32 now)
{
    struct dwa_net_queue *q;struct dwa_net_frame *f;
    clear(out,sizeof(*out));if(!dwa_net_active(b,now))return 0;
    q=rx?&b->rx:&b->tx;
    while(q->count){
        f=&q->frames[q->head];q->head=(q->head+1)%DWA_NET_SLOTS;--q->count;
        if((rtl_u32)(now-f->time)<=DWA_NET_FRAME_MS){*out=*f;clear(f,sizeof(*f));return 1;}
        clear(f,sizeof(*f));++q->dropped;
    }
    return 0;
}
