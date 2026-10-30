/* Original implementation under the project MIT license. Wire format reference:
 * IEEE 802.11 RSN IE, as documented by hostap wpa_parse_wpa_ie_rsn.
 * No platform APIs, allocation, packed casts, or retained input pointers. */
#include "beacon.h"
static unsigned int le16(const rtl_u8 *p)
{return (unsigned int)p[0]|((unsigned int)p[1]<<8);}
static void zero(void *p,unsigned int n)
{rtl_u8 *b=(rtl_u8 *)p;while(n--)*b++=0;}
static void copy(rtl_u8 *to,const rtl_u8 *from,unsigned int n)
{while(n--)*to++=*from++;}
static int equal(const rtl_u8 *a,const rtl_u8 *b,unsigned int n)
{while(n--)if(*a++!=*b++)return 0;return 1;}
static int suite(const rtl_u8 *p,unsigned int type)
{return p[0]==0&&p[1]==15&&p[2]==172&&p[3]==type;}
int rtl_rsn_parse(const rtl_u8 *ie,unsigned int size,struct rtl_rsn *out)
{
    struct rtl_rsn r;
    unsigned int pos=4,count,i;
    if(!out)return RTL_BAD_ARGUMENT;
    zero(out,sizeof(*out));out->status=RTL_RSN_MALFORMED;
    zero(&r,sizeof(r));r.status=RTL_RSN_VALID;
    r.group_ccmp=r.pairwise_ccmp=1;
    if(!ie||size<4||size>257||ie[0]!=48||ie[1]!=size-2)return RTL_BAD_ARGUMENT;
    if(le16(ie+2)!=1){out->status=RTL_RSN_UNSUPPORTED;return RTL_UNSUPPORTED;}
    if(pos==size)goto done;
    if(size-pos<4)goto bad;
    r.group_ccmp=suite(ie+pos,4);pos+=4;
    if(pos==size)goto done;
    if(size-pos<2)goto bad;
    count=le16(ie+pos);pos+=2;r.pairwise_ccmp=0;
    if(!count||count>(size-pos)/4)goto bad;
    for(i=0;i<count;++i,pos+=4)if(suite(ie+pos,4))r.pairwise_ccmp=1;
    if(pos==size)goto done;
    if(size-pos<2)goto bad;
    count=le16(ie+pos);pos+=2;
    if(!count||count>(size-pos)/4)goto bad;
    for(i=0;i<count;++i,pos+=4)if(suite(ie+pos,2))r.psk=1;
    if(pos==size)goto done;
    if(size-pos<2)goto bad;
    r.capabilities=le16(ie+pos);pos+=2;
    /* Required management protection must also be advertised as capable. */
    if((r.capabilities&0xc0)==0x40)goto bad;
    if(pos==size)goto done;
    if(size-pos<2)goto bad;
    count=le16(ie+pos);pos+=2;
    if(count>(size-pos)/16)goto bad;
    pos+=count*16;
    if(pos==size)goto done;
    /* Optional group-management selector is exactly four bytes. We do not
     * implement PMF and make no support claim for any selector here. */
    if(size-pos!=4)goto bad;
done:*out=r;return RTL_OK;
bad:return RTL_BAD_ARGUMENT;
}
/* WPA vendor IE: OUI/type, version, optional group selector, pairwise and
 * AKM lists, optional capabilities. Strict trailing bounds; unknown selectors
 * do not become a claim of cipher support. Reference: hostap wpa_common.c. */
unsigned int rtl_wpa_status(const rtl_u8 *p,unsigned int size)
{
    unsigned int pos=8,n,lists;
    if(!p||size<8||size>257||p[0]!=221||p[1]!=size-2||
       p[2]!=0||p[3]!=0x50||p[4]!=0xf2||p[5]!=1)return RTL_RSN_MALFORMED;
    if(le16(p+6)!=1)return RTL_RSN_UNSUPPORTED;
    if(pos==size)return RTL_RSN_VALID;
    if(size-pos<4)return RTL_RSN_MALFORMED;
    pos+=4;
    for(lists=0;lists<2;++lists){
        if(pos==size)return RTL_RSN_VALID;
        if(size-pos<2)return RTL_RSN_MALFORMED;
        n=le16(p+pos);pos+=2;
        if(!n||n>(size-pos)/4)return RTL_RSN_MALFORMED;
        pos+=4*n;
    }
    return size==pos||size-pos==2?RTL_RSN_VALID:RTL_RSN_MALFORMED;
}
const char *rtl_bss_security(const struct rtl_bss *b)
{
    if(!b)return "unknown";
    if((b->rsn.status!=RTL_RSN_ABSENT&&b->rsn.status!=RTL_RSN_VALID)||
       (b->wpa_status!=RTL_RSN_ABSENT&&b->wpa_status!=RTL_RSN_VALID))return "unknown-invalid-IE";
    if(!(b->capabilities&0x10))
        return b->rsn.status||b->wpa_status?"unknown-inconsistent":"open";
    if(b->rsn.status==RTL_RSN_VALID)
        return b->wpa_status==RTL_RSN_VALID?"WPA+RSN":"RSN";
    if(b->wpa_status==RTL_RSN_VALID)return "WPA";
    return "privacy-unspecified"; /* WEP is possible, not established by one bit. */
}
int rtl_bss_advertises_psk_ccmp(const struct rtl_bss *b)
{
    return b&&b->rsn.status==RTL_RSN_VALID&&b->rsn.group_ccmp&&
        b->rsn.pairwise_ccmp&&b->rsn.psk&&(b->capabilities&0x10)!=0;
}
int rtl_bss_observe_at(struct rtl_bss_table *t,const rtl_u8 *f,unsigned int size,
                    unsigned int received_channel,rtl_u32 now)
{
    struct rtl_bss b;
    unsigned int pos=36,n,id,i,ssid=0,ds=0,ht=0,rsn=0,wpa=0,ds_channel=0,ht_channel=0;
    unsigned int nonzero=0;
    if(!t)return RTL_BAD_ARGUMENT;
    if(t->count>RTL_BSS_MAX)return RTL_BAD_ARGUMENT;
    if(!f||size<36||size>4092||received_channel<1||received_channel>14)goto bad;
    if(f[0]!=0x80||(f[1]&0x47)||(le16(f+22)&15))goto bad;
    for(i=4;i<10;++i)if(f[i]!=255)goto bad;
    if(f[16]&1||!equal(f+10,f+16,6))goto bad;
    for(i=16;i<22;++i)nonzero|=f[i];
    if(!nonzero)goto bad;
    zero(&b,sizeof(b));copy(b.bssid,f+16,6);
    b.received_channel=received_channel;b.last_seen_ms=now;
    b.beacon_interval=le16(f+32);b.capabilities=le16(f+34);
    while(pos<size) {
        if(size-pos<2)goto bad;
        id=f[pos];n=f[pos+1];pos+=2;
        if(n>size-pos)goto bad;
        if(id==0) {
            if(ssid++||n>32)goto bad;
            b.ssid_length=n;copy(b.ssid,f+pos,n);b.hidden=1;
            for(i=0;i<n;++i)if(f[pos+i])b.hidden=0;
        } else if(id==3) {
            if(ds++||n!=1||f[pos]<1||f[pos]>14)goto bad;
            ds_channel=f[pos];
        } else if(id==61) {
            if(ht++||n!=22||f[pos]<1||f[pos]>14)goto bad;
            ht_channel=f[pos];
        } else if(id==221&&n>=4&&f[pos]==0&&f[pos+1]==0x50&&f[pos+2]==0xf2&&f[pos+3]==1) {
            if(wpa++==0){
                b.wpa_length=n+2;copy(b.wpa_ie,f+pos-2,n+2);
                b.wpa_status=rtl_wpa_status(b.wpa_ie,b.wpa_length);
            } else {b.wpa_status=RTL_RSN_MALFORMED;b.wpa_length=0;}
        } else if(id==48) {
            ++rsn;
            if(rsn==1) {
                b.rsn_length=n+2;copy(b.rsn_ie,f+pos-2,n+2);
                rtl_rsn_parse(b.rsn_ie,b.rsn_length,&b.rsn);
            } else {
                /* Do not fall back to open or to the first RSN on ambiguity. */
                zero(&b.rsn,sizeof(b.rsn));b.rsn.status=RTL_RSN_MALFORMED;
                b.rsn_length=0;
            }
        }
        pos+=n;
    }
    if(!ssid||!b.beacon_interval||(ds&&ht&&ds_channel!=ht_channel))goto bad;
    /* Zero means not advertised; never substitute the tuned channel. */
    b.channel=ds?ds_channel:ht_channel;
    for(i=0;i<t->count;++i)if(equal(t->entries[i].bssid,b.bssid,6))break;
    ++t->accepted;
    if(i==t->count) {
        if(t->count==RTL_BSS_MAX){++t->dropped;return RTL_OK;}
        ++t->count;b.observations=1;
    } else b.observations=t->entries[i].observations+1;
    t->entries[i]=b;return RTL_OK;
bad:++t->rejected;return RTL_BAD_ARGUMENT;
}

int rtl_bss_observe(struct rtl_bss_table *t,const rtl_u8 *f,unsigned int n,unsigned int c)
{return rtl_bss_observe_at(t,f,n,c,0);}
