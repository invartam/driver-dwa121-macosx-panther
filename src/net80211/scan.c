#include "scan.h"
static int same(const rtl_u8 *a,const rtl_u8 *b)
{unsigned int i;for(i=0;i<6;++i)if(a[i]!=b[i])return 0;return 1;}
void dwa_scan_expire(struct rtl_bss_table *t,rtl_u32 now)
{
    unsigned int i,n=0;
    if(!t||t->count>RTL_BSS_MAX)return;
    for(i=0;i<t->count;++i)
        if((rtl_u32)(now-t->entries[i].last_seen_ms)<DWA_SCAN_TTL_MS)
            t->entries[n++]=t->entries[i];
    t->count=n;
}
void dwa_scan_merge(struct rtl_bss_table *t,const struct rtl_bss_table *s,rtl_u32 now)
{
    unsigned int i,j;
    if(!t||!s||t->count>RTL_BSS_MAX||s->count>RTL_BSS_MAX)return;
    dwa_scan_expire(t,now);
    t->accepted+=s->accepted;t->rejected+=s->rejected;t->dropped+=s->dropped;
    for(i=0;i<s->count;++i) {
        const struct rtl_bss *b=&s->entries[i];
        if(b->ssid_length>32||(rtl_u32)(now-b->last_seen_ms)>=DWA_SCAN_TTL_MS)continue;
        for(j=0;j<t->count;++j)if(same(t->entries[j].bssid,b->bssid))break;
        if(j==t->count) {
            if(j==RTL_BSS_MAX){++t->dropped;continue;}
            ++t->count;
        }
        t->entries[j]=*b;
    }
}
static void be16(rtl_u8 *p,unsigned int v)
{p[0]=(rtl_u8)(v>>8);p[1]=(rtl_u8)v;}
int dwa_scan_native(const struct rtl_bss *b,rtl_u8 *out)
{
    unsigned int i;
    if(!b||!out||!b->ssid_length||b->ssid_length>=32||b->hidden)return 0;
    for(i=0;i<b->ssid_length;++i)if(!b->ssid[i])return 0;
    for(i=0;i<DWA_SCAN_NATIVE_BYTES;++i)out[i]=0;
    be16(out,b->channel);for(i=0;i<6;++i)out[6+i]=b->bssid[i];
    be16(out+12,b->beacon_interval);be16(out+14,b->capabilities);
    be16(out+16,b->ssid_length);
    for(i=0;i<b->ssid_length;++i)out[18+i]=b->ssid[i];
    return 1;
}
