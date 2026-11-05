#include "airport_join.h"
#include <string.h>
static unsigned int word(const rtl_u8 *p)
{ return ((unsigned int)p[0]<<8)|p[1]; }
static int compare(const rtl_u8 *a,const rtl_u8 *b,unsigned int n)
{ unsigned int i;for(i=0;i<n;++i)if(a[i]!=b[i])return a[i]<b[i]?-1:1;return 0; }
int dwa_join_parse(const rtl_u8 *p,unsigned int n,struct dwa_join_selection *out)
{
    unsigned int i,len,mode,key,auth;
    if(!out)return 0;
    memset(out,0,sizeof(*out));
    if(!p||n!=DWA_JOIN_NATIVE_BYTES)return 0;
    mode=word(p);key=word(p+2);auth=word(p+20);
    if(mode==0&&key==0&&auth==1&&p[22]==255&&p[23]==255&&p[24]==255&&p[25]==255){
        for(i=4;i<20;++i)if(p[i])return 0;
        for(i=26;i<56;++i)if(p[i])return 0;
        out->operation=DWA_JOIN_DISCONNECT;return 1;
    }
    if(!((mode==0&&key==0&&auth==1)||(mode==1&&key==13&&auth==2)||
         (mode==1&&key==5&&auth==3)))return 0;
    if(!key)for(i=4;i<20;++i)if(p[i])return 0;
    if(auth==3)for(i=4;i<20;++i)if(p[i])return 0;
    /* Panther scan exposes at most 31-byte C strings. Reject truncation,
     * embedded-NUL aliases and nonzero padding rather than change identity. */
    for(len=0;len<32&&p[22+len];++len){}
    if(!len||len>=32)return 0;
    for(i=22+len;i<56;++i)if(p[i])return 0;
    memcpy(out->ssid,p+22,len);out->ssid_length=len;
    out->legacy_prompt=auth==3?2:(key!=0);out->operation=DWA_JOIN_SELECT;return 1;
}
int dwa_join_choose(const struct rtl_bss_table *t,const rtl_u8 *ssid,unsigned int n,
                    unsigned int preferred,rtl_u32 now,struct rtl_bss *out)
{
    unsigned int i;const struct rtl_bss *best=0;
    if(!out)return 0;
    memset(out,0,sizeof(*out));
    if(!t||t->count>RTL_BSS_MAX||!ssid||!n||n>=32)return 0;
    for(i=0;i<t->count;++i){
        const struct rtl_bss *b=&t->entries[i];unsigned int j,nonzero=0;
        if(b->ssid_length!=n||compare(b->ssid,ssid,n)||b->hidden||
           (rtl_u32)(now-b->last_seen_ms)>=DWA_SCAN_TTL_MS||
           b->channel<1||b->channel>11||(b->capabilities&3)!=1||
           !rtl_bss_advertises_psk_ccmp(b)||(b->rsn.capabilities&0x40)||
           (b->bssid[0]&1))continue;
        for(j=0;j<6;++j)nonzero|=b->bssid[j];
        if(!nonzero)continue;
        if(!best||(b->channel==preferred&&best->channel!=preferred)||
           ((b->channel==preferred)==(best->channel==preferred)&&
            compare(b->bssid,best->bssid,6)<0))best=b;
    }
    if(!best)return 0;
    *out=*best;return 1;
}
