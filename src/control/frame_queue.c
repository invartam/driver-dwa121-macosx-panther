#include "frame_queue.h"
#include <string.h>
void dwa_frame_queue_reset(struct dwa_frame_queue *q)
{ if(q) memset(q,0,sizeof(*q)); }
int dwa_frame_queue_push(struct dwa_frame_queue *q,unsigned int kind,unsigned int channel,
                         rtl_u32 now,const rtl_u8 *data,unsigned int n)
{
    unsigned int i,j,index; struct dwa_frame *f;
    if(!q || !data || !n || n>DWA_FRAME_MAX || kind<1 || kind>4 || channel<1 || channel>11)
        return RTL_BAD_ARGUMENT;
    if(q->count==DWA_FRAME_SLOTS) {
        if(kind==DWA_FRAME_BEACON) { ++q->dropped; return 0; }
        for(i=0;i<q->count;++i)
            if(q->slots[(q->head+i)%DWA_FRAME_SLOTS].kind==DWA_FRAME_BEACON ||
               ((kind==DWA_FRAME_MANAGEMENT||kind==DWA_FRAME_EAPOL)&&
                q->slots[(q->head+i)%DWA_FRAME_SLOTS].kind==DWA_FRAME_CCMP)) break;
        if(i==q->count) { ++q->dropped; return 0; }
        for(j=i;j+1<q->count;++j)
            q->slots[(q->head+j)%DWA_FRAME_SLOTS]=q->slots[(q->head+j+1)%DWA_FRAME_SLOTS];
        --q->count; ++q->dropped;
    }
    index=(q->head+q->count)%DWA_FRAME_SLOTS; f=&q->slots[index];
    memset(f,0,sizeof(*f)); f->kind=kind; f->channel=channel; f->received_ms=now; f->length=n;
    memcpy(f->data,data,n); ++q->count; return 1;
}
int dwa_frame_queue_pop(struct dwa_frame_queue *q,rtl_u32 now,struct dwa_frame *out)
{
    struct dwa_frame *f;
    if(!out) return RTL_BAD_ARGUMENT;
    memset(out,0,sizeof(*out)); if(!q) return RTL_BAD_ARGUMENT;
    while(q->count) {
        f=&q->slots[q->head]; --q->count; q->head=(q->head+1)%DWA_FRAME_SLOTS;
        if((rtl_u32)(now-f->received_ms)>DWA_FRAME_MAX_AGE_MS) {
            ++q->expired; memset(f,0,sizeof(*f)); continue;
        }
        *out=*f; memset(f,0,sizeof(*f)); ++q->delivered; return 1;
    }
    return 0;
}
