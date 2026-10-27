#include "wpa.h"
static int zero(const rtl_u8 *p,unsigned int n)
{ unsigned int i; for(i=0;i<n;++i) if(p[i]) return 0; return 1; }
static unsigned int be16(const rtl_u8 *p)
{ return ((unsigned int)p[0]<<8)|p[1]; }
int dwa_wpa_validate(const struct dwa_wpa_request *r)
{
    const rtl_u8 *e;unsigned int n,info;
    if(!r||r->version!=DWA_WPA_VERSION||r->operation<1||r->operation>7)return RTL_BAD_ARGUMENT;
    if(r->operation==DWA_WPA_ENTER)
        return r->aid>=1&&r->aid<=2007&&!r->length&&!r->index&&!r->pairwise&&
            zero(r->rsc,8)&&zero(r->data,2080)?RTL_OK:RTL_BAD_ARGUMENT;
    if(r->aid)return RTL_BAD_ARGUMENT;
    if(r->operation==DWA_WPA_KEY)
        return r->length==16&&r->index<=3&&r->pairwise<=1&&(!r->pairwise||!r->index)&&
            zero(r->rsc+6,2)&&zero(r->data+16,2064)?RTL_OK:RTL_BAD_ARGUMENT;
    if(r->index||r->pairwise||!zero(r->rsc,8))return RTL_BAD_ARGUMENT;
    if(r->operation==DWA_WPA_VERIFIED||r->operation==DWA_WPA_DATA_OPEN||r->operation==DWA_WPA_DATA_OFFER)
        return !r->length&&zero(r->data,2080)?RTL_OK:RTL_BAD_ARGUMENT;
    if(r->operation==DWA_WPA_DATA_SEND)
        return r->length>=48&&r->length<=1548&&r->data[0]==8&&r->data[1]==0x41&&
            !(r->data[22]&15)&&!r->data[26]&&r->data[27]==0x20&&
            zero(r->data+r->length,2080-r->length)?RTL_OK:RTL_BAD_ARGUMENT;
    if(r->length<131||r->length>2080||r->data[0]!=8||r->data[1]!=1||
       !zero(r->data+r->length,2080-r->length))return RTL_BAD_ARGUMENT;
    /* Addresses and exact LLC are additionally checked by tx_prepare. */
    e=r->data+32;n=r->length-32;info=be16(e+5);
    if((e[0]!=1&&e[0]!=2)||e[1]!=3||e[4]!=2||be16(e+2)!=n-4||be16(e+97)!=n-99)
        return RTL_BAD_ARGUMENT;
    if(info!=0x010a&&info!=0x030a)return RTL_UNSUPPORTED;
    if(info==0x030a&&n!=99)return RTL_BAD_ARGUMENT;
    return RTL_OK;
}
int dwa_wpa_data_reserve(struct dwa_data_budget *b,const struct dwa_wpa_request *r,
    const rtl_u8 mac[6],const rtl_u8 ap[6])
{ return dwa_wpa_data_reserve_limit(b,r,mac,ap,DWA_WPA_DATA_MAX_SEND); }
int dwa_wpa_data_reserve_limit(struct dwa_data_budget *b,const struct dwa_wpa_request *r,
    const rtl_u8 mac[6],const rtl_u8 ap[6],rtl_u32 limit)
{
    rtl_u8 pn[6];unsigned int i;int greater=0;
    if(!b||!mac||!ap||dwa_wpa_validate(r)||r->operation!=DWA_WPA_DATA_SEND||
       b->sent>=limit)return RTL_BAD_ARGUMENT;
    for(i=0;i<6;++i)if(r->data[4+i]!=ap[i]||r->data[10+i]!=mac[i])return RTL_BAD_ARGUMENT;
    pn[0]=r->data[24];pn[1]=r->data[25];for(i=2;i<6;++i)pn[i]=r->data[26+i];
    for(i=6;i>0;--i)if(pn[i-1]!=b->pn[i-1]){greater=pn[i-1]>b->pn[i-1];break;}
    if(!greater)return RTL_BAD_ARGUMENT;
    for(i=0;i<6;++i)b->pn[i]=pn[i];
    ++b->sent;return RTL_OK;
}
