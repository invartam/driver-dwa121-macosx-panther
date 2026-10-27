#include "association.h"
static int zero(const rtl_u8 *p,unsigned int n)
{ unsigned int i; for(i=0;i<n;++i) if(p[i]) return 0; return 1; }
int dwa_assoc_validate(const struct dwa_assoc_request *r,const rtl_u8 mac[6])
{
    unsigned int i,same=1;
    if(!r || !mac || r->version!=DWA_ASSOC_VERSION || !zero(r->reserved,2) ||
       r->operation<DWA_ASSOC_BEGIN || r->operation>DWA_ASSOC_END) return RTL_BAD_ARGUMENT;
    if(r->operation==DWA_ASSOC_BEGIN) {
        for(i=0;i<6;++i) if(r->bssid[i]!=mac[i]) same=0;
        if(r->channel<1 || r->channel>11 || r->length || same ||
           (r->bssid[0]&1) || zero(r->bssid,6) || !zero(r->frame,512)) return RTL_BAD_ARGUMENT;
    } else {
        if(r->channel || !zero(r->bssid,6)) return RTL_BAD_ARGUMENT;
        if(r->operation==DWA_ASSOC_SEND) {
            if(r->length<28 || r->length>512 || (r->frame[0]!=0 && r->frame[0]!=0xb0) ||
               !zero(r->frame+r->length,512-r->length)) return RTL_BAD_ARGUMENT;
        } else if(r->length || !zero(r->frame,512)) return RTL_BAD_ARGUMENT;
    }
    return RTL_OK;
}
int dwa_assoc_admit(const struct dwa_assoc_budget *b,rtl_u32 now,unsigned int subtype)
{
    if(!b || (rtl_u32)(now-b->start_ms)>=DWA_ASSOC_MAX_MS) return RTL_TIMEOUT;
    if(subtype==0xb0) return !b->assoc && b->auth<3 ? RTL_OK : RTL_UNSUPPORTED;
    if(subtype==0) return b->auth && b->assoc<3 ? RTL_OK : RTL_UNSUPPORTED;
    return RTL_UNSUPPORTED;
}
