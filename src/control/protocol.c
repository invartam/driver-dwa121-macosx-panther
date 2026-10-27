#include "protocol.h"
static int same(const rtl_u8 *a,const rtl_u8 *b)
{ unsigned int i; for(i=0;i<6;++i) if(a[i]!=b[i]) return 0; return 1; }
int dwa_control_validate(const struct dwa_control_request *r,const rtl_u8 mac[6])
{
    static const rtl_u8 zero[6]={0,0,0,0,0,0};
    if(!r || !mac || r->version!=DWA_CONTROL_VERSION || r->reserved[0] || r->reserved[1] ||
       r->operation<DWA_CONTROL_STATUS || r->operation>DWA_CONTROL_NEXT) return RTL_BAD_ARGUMENT;
    if(r->operation==DWA_CONTROL_SELECT) {
        if((r->bssid[0]&1) || same(r->bssid,zero) || same(r->bssid,mac))
            return RTL_BAD_ARGUMENT;
    } else if(!same(r->bssid,zero)) return RTL_BAD_ARGUMENT;
    return RTL_OK;
}
