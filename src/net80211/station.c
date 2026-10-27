/* Original MIT-licensed, bounded 802.11b/g infrastructure station core. */
#include "station.h"
#include <string.h>
const rtl_u8 rtl_station_rsn[RTL_STA_RSN_SIZE] = {
    48,20,1,0,0,15,172,4,1,0,0,15,172,4,1,0,0,15,172,2,0,0
};
static unsigned int le16(const rtl_u8 *p)
{ return p[0] | ((unsigned int)p[1]<<8); }
static void put16(rtl_u8 *p,unsigned int n)
{ p[0]=(rtl_u8)n; p[1]=(rtl_u8)(n>>8); }
static int unicast(const rtl_u8 *m)
{ unsigned int i,n=0; for(i=0;i<6;i++)n|=m[i]; return n && !(m[0]&1); }
static int fail(struct rtl_station *s,unsigned int status)
{ s->state=RTL_STA_FAILED;s->status=status;s->tx_length=0;return RTL_UNSUPPORTED; }
static int rate_supported(unsigned int rate)
{
    static const rtl_u8 supported[]={2,4,11,22,12,18,24,36,48,72,96,108};
    unsigned int i; for(i=0;i<sizeof(supported);i++)if(rate==supported[i])return 1;
    return 0;
}
static int rates(struct rtl_station *s,const rtl_u8 *f,unsigned int n,unsigned int pos)
{
    unsigned int seen=0,ext=0,i,j,id,len,r;
    while(pos<n) {
        if(n-pos<2)return 0;
        id=f[pos];len=f[pos+1];pos+=2;
        if(len>n-pos)return 0;
        if(id==1||id==50) {
            if(!len||(id==1&&(seen++||len>8))||(id==50&&ext++))return 0;
            for(i=0;i<len;i++) {
                r=f[pos+i]&127;
                if(!rate_supported(r)) { if(f[pos+i]&128)return 0;continue; }
                for(j=0;j<s->rate_count;j++)if((s->rates[j]&127)==r)break;
                if(j==s->rate_count) {
                    if(j==12)return 0;
                    s->rates[s->rate_count++]=f[pos+i];
                } else s->rates[j]|=f[pos+i]&128;
            }
        }
        pos+=len;
    }
    return seen==1 && s->rate_count!=0;
}
static void header(struct rtl_station *s,unsigned int subtype)
{
    memset(s->tx,0,sizeof(s->tx));s->tx[0]=(rtl_u8)subtype;
    memcpy(s->tx+4,s->target.bssid,6);memcpy(s->tx+10,s->mac,6);
    memcpy(s->tx+16,s->target.bssid,6);put16(s->tx+22,(s->sequence++&4095)<<4);
}
static int association(struct rtl_station *s)
{
    unsigned int n=28,first=s->rate_count>8?8:s->rate_count;
    header(s,0); /* ESS + Privacy, optional short preamble/slot from AP. */
    put16(s->tx+24,0x11|(s->target.capabilities&0x420));put16(s->tx+26,10);
    s->tx[n++]=0;s->tx[n++]=(rtl_u8)s->target.ssid_length;
    memcpy(s->tx+n,s->target.ssid,s->target.ssid_length);n+=s->target.ssid_length;
    s->tx[n++]=1;s->tx[n++]=(rtl_u8)first;memcpy(s->tx+n,s->rates,first);n+=first;
    if(s->rate_count>first) {
        s->tx[n++]=50;s->tx[n++]=(rtl_u8)(s->rate_count-first);
        memcpy(s->tx+n,s->rates+first,s->rate_count-first);n+=s->rate_count-first;
    }
    memcpy(s->tx+n,rtl_station_rsn,RTL_STA_RSN_SIZE);n+=RTL_STA_RSN_SIZE;
    s->tx_length=n;s->state=RTL_STA_ASSOC;s->attempts=1;return 1;
}
int rtl_station_begin(struct rtl_station *s,const rtl_u8 mac[6],
    const rtl_u8 *f,unsigned int n,const rtl_u8 *ssid,unsigned int ssid_n,
    unsigned int channel,unsigned int age_ms)
{
    /* Userland core: discovery scratch is intentionally not on a kernel stack. */
    struct rtl_bss_table table;
    if(!s)return RTL_BAD_ARGUMENT;
    memset(s,0,sizeof(*s));s->state=RTL_STA_FAILED;
    if(!mac||!unicast(mac)||!ssid||!ssid_n||ssid_n>32||channel<1||channel>11||age_ms>5000)
        return RTL_BAD_ARGUMENT;
    memset(&table,0,sizeof(table));
    if(rtl_bss_observe(&table,f,n,channel)!=RTL_OK||table.count!=1)return RTL_BAD_ARGUMENT;
    s->target=table.entries[0];
    if(!rtl_bss_advertises_psk_ccmp(&s->target)||(s->target.rsn.capabilities&0x40)||
       (s->target.capabilities&3)!=1||s->target.channel!=channel||
       s->target.hidden||s->target.ssid_length!=ssid_n||memcmp(s->target.ssid,ssid,ssid_n)||
       !memcmp(mac,s->target.bssid,6)||!rates(s,f,n,36))return RTL_UNSUPPORTED;
    memcpy(s->mac,mac,6);header(s,0xb0);
    put16(s->tx+24,0);put16(s->tx+26,1);put16(s->tx+28,0);
    s->tx_length=30;s->state=RTL_STA_AUTH;s->attempts=1;return 1;
}
int rtl_station_receive(struct rtl_station *s,const rtl_u8 *f,unsigned int n)
{
    unsigned int status,aid,pos,len,rsn_seen=0;
    struct rtl_station response;
    struct rtl_rsn rsn;
    if(!s||!f||n<24||n>4092)return RTL_BAD_ARGUMENT;
    /* Fixed management header only; reject DS, fragments, protection, order. */
    if((f[0]&15)||(f[1]&0xc7)||(le16(f+22)&15)||memcmp(f+4,s->mac,6)||
       memcmp(f+10,s->target.bssid,6)||memcmp(f+16,s->target.bssid,6))return RTL_BAD_ARGUMENT;
    if((f[0]==0xc0||f[0]==0xa0)&&n==26) {
        if(s->state==RTL_STA_IDLE||s->state==RTL_STA_FAILED)return 0;
        return fail(s,le16(f+24));
    }
    if(s->state==RTL_STA_AUTH&&f[0]==0xb0) {
        if(n<30||n>512||le16(f+24)!=0||le16(f+26)!=2)return RTL_BAD_ARGUMENT;
        /* Open-system responses may append optional IEs (e.g. vendor data).
         * Ignore their contents, but never accept a partial/overlong TLV. */
        for(pos=30;pos<n;pos+=len+2) {
            if(n-pos<2)return RTL_BAD_ARGUMENT;
            len=f[pos+1];if(len>n-pos-2)return RTL_BAD_ARGUMENT;
        }
        status=le16(f+28);if(status)return fail(s,status);
        return association(s);
    }
    if(s->state==RTL_STA_ASSOC&&f[0]==0x10) {
        if(n<30)return RTL_BAD_ARGUMENT;
        status=le16(f+26);if(status)return fail(s,status);
        aid=le16(f+28);
        if((aid&0xc000)!=0xc000||(aid&0x3fff)==0||(aid&0x3fff)>2007||
           (le16(f+24)&0x13)!=0x11)return RTL_BAD_ARGUMENT;
        /* Reject truncated TLVs and RSN policy changes, if supplied. */
        for(pos=30;pos<n;pos+=len+2) {
            if(n-pos<2)return RTL_BAD_ARGUMENT;
            len=f[pos+1];if(len>n-pos-2)return RTL_BAD_ARGUMENT;
            if(f[pos]==48) {
                if(rsn_seen++||rtl_rsn_parse(f+pos,len+2,&rsn)||!rsn.psk||
                   !rsn.group_ccmp||!rsn.pairwise_ccmp||(rsn.capabilities&0x40))return RTL_UNSUPPORTED;
            }
        }
        memset(&response,0,sizeof(response));
        if(!rates(&response,f,n,30))return RTL_UNSUPPORTED;
        s->aid=aid&0x3fff;s->state=RTL_STA_KEYS;s->tx_length=0;return 0;
    }
    return 0;
}
int rtl_station_timeout(struct rtl_station *s)
{
    if(!s)return RTL_BAD_ARGUMENT;
    if(s->state==RTL_STA_KEYS)return fail(s,16);
    if(s->state!=RTL_STA_AUTH&&s->state!=RTL_STA_ASSOC)return 0;
    if(s->attempts>=3)return fail(s,16);
    ++s->attempts;s->tx[1]|=8;return 1;
}
void rtl_station_disconnect(struct rtl_station *s)
{ if(s){memset(s,0,sizeof(*s));s->state=RTL_STA_IDLE;} }
int rtl_station_keys_ready(struct rtl_station *s,int installed)
{
    if(!s||s->state!=RTL_STA_KEYS)return RTL_BAD_ARGUMENT;
    if(!installed)return fail(s,15);
    s->state=RTL_STA_AUTHORIZED;return RTL_OK;
}
