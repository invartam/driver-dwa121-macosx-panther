/* Original bounded descriptor parser; layout/alignment from OpenBSD r92creg.h
 * and urtwn_rxeof. No casts to packed structs or unaligned integer pointers. */
#include "rx.h"
#include "../usb/rtl_wire.h"
static rtl_u32 fcs(const rtl_u8 *p,unsigned int size)
{
    rtl_u32 crc=0xffffffffU;unsigned int i,j;
    for(i=0;i<size;++i) {
        crc^=p[i];
        for(j=0;j<8;++j)crc=(crc>>1)^((crc&1)?0xedb88320U:0);
    }
    return ~crc;
}
static int same(const rtl_u8 *a,const rtl_u8 *b,unsigned int n)
{ unsigned int i; for(i=0;i<n;++i) if(a[i]!=b[i]) return 0; return 1; }
static unsigned int encrypted_kind(const rtl_u8 *f,unsigned int n,const struct rtl_rx_filter *p,rtl_u32 d0)
{
    unsigned int h;const rtl_u8 *cc;
    if(!p||!p->selected||!p->encrypted||!(d0&0x08000000U)||n<48||n>1550||
       (f[0]!=8&&f[0]!=0x88)||(f[1]&0xc7)!=0x42||(f[22]&15)||
       !same(f+10,p->bssid,6)||(!(f[4]&1)&&!same(f+4,p->mac,6)))return 0;
    h=f[0]==0x88?26:24;if(n<h+24||(h==26&&((f[24]&0x80)||f[25])))return 0;
    cc=f+h;if(cc[2]||(cc[3]&0x3f)!=0x20||(!(f[4]&1)&&(cc[3]>>6)))return 0;
    return RTL_RX_CCMP;
}
static unsigned int control_kind(const rtl_u8 *f,unsigned int n,const struct rtl_rx_filter *p)
{
    static const rtl_u8 llc[8]={0xaa,0xaa,3,0,0,0,0x88,0x8e};
    unsigned int h,pos,len; const rtl_u8 *e;
    if(!p || !p->selected || n<24 || (f[22]&15) || (f[1]&0xc4) ||
       !same(f+4,p->mac,6) || !same(f+10,p->bssid,6)) return 0;
    if((f[1]&3)==0 && same(f+16,p->bssid,6)) {
        if(f[0]==0xb0 && n>=30 && n<=512) {
            pos=30;
            while(pos<n) { if(n-pos<2 || f[pos+1]>n-pos-2) return 0; pos+=2+f[pos+1]; }
            return RTL_RX_MANAGEMENT;
        }
        if((f[0]==0xc0 || f[0]==0xa0) && n==26) return RTL_RX_MANAGEMENT;
        if(f[0]==0x10 && n>=30) {
            pos=30;
            while(pos<n) { if(n-pos<2 || f[pos+1]>n-pos-2) return 0; pos+=2+f[pos+1]; }
            return RTL_RX_MANAGEMENT;
        }
    }
    if((f[0]!=8 && f[0]!=0x88) || (f[1]&3)!=2 || !same(f+16,p->bssid,6)) return 0;
    h=f[0]==0x88 ? 26 : 24;
    if(n<h+8+99 || (h==26 && (f[24]&0x80)) || !same(f+h,llc,8)) return 0;
    e=f+h+8; len=n-h-8;
    if(len>2048 || (e[0]!=1 && e[0]!=2) || e[1]!=3 || e[4]!=2 ||
       (((unsigned int)e[2]<<8)|e[3])!=len-4) return 0;
    return RTL_RX_EAPOL;
}
static int parse(const rtl_u8 *data,unsigned int size,struct rtl_rx_stats *s,
                 rtl_beacon_sink sink,rtl_control_sink control,void *cookie,
                 const struct rtl_rx_filter *policy)
{
    unsigned int offset=0,count,index,length,info,shift,total,rounded;
    rtl_u32 d0;const rtl_u8 *frame; unsigned int kind;
    if(!data||!s)return RTL_BAD_ARGUMENT;
    ++s->transfers;
    if(size<24||size>RTL_RX_BUFFER_SIZE)goto malformed;
    count=(rtl_get_le32(data+8)>>16)&255;
    if(!count)goto malformed;
    for(index=0;index<count;++index) {
        if(offset>size||size-offset<24)goto malformed;
        d0=rtl_get_le32(data+offset);
        length=d0&0x3fff;info=((d0>>16)&15)*8;shift=(d0>>24)&3;
        total=24+info+shift+length;
        if(!length||total>size-offset)goto malformed;
        ++s->descriptors;
        frame=data+offset+24+info+shift;
        /* Diagnostic metadata only, before strict auth body validation. This
         * distinguishes absent replies from replies rejected by the parser.
         * Never publish frame contents or accept a frame on this basis. */
        if(policy && policy->selected && !(d0&0xc000) && length>=34 && length<=2308 &&
           frame[0]==0xb0 && fcs(frame,length-4)==rtl_get_le32(frame+length-4)) {
            ++s->auth_seen; s->auth_last_length=length-4;
            if(same(frame+4,policy->mac,6)) ++s->auth_to_us;
            if(same(frame+10,policy->bssid,6)) ++s->auth_from_ap;
        }
        if(d0&0x4000)++s->crc_errors;
        else if(d0&0x8000)++s->icv_errors;
        else if(length>=28 && length<=2308 &&
                ((kind=control_kind(frame,length-4,policy))!=0||
                 (kind=encrypted_kind(frame,length-4,policy,d0))!=0)) {
            if(fcs(frame,length-4)!=rtl_get_le32(frame+length-4)) ++s->fcs_errors;
            else {
                ++s->accepted; s->bytes+=length-4;
                if(kind==RTL_RX_MANAGEMENT) ++s->management;
                else if(kind==RTL_RX_CCMP)++s->encrypted;else ++s->eapol;
                if(control) control(cookie,kind,frame,length-4);
            }
        }
        else if(length<40||length>4096||(frame[0]&0xfc)!=0x80||(frame[0]&3)||
                (frame[1]&0xc7)||(frame[22]&15))++s->filtered;
        else if(fcs(frame,length-4)!=rtl_get_le32(frame+length-4))++s->fcs_errors;
        else {
            /* Verify broadcast destination, complete fixed beacon body and IEs. */
            unsigned int pos=36,i;int valid=1;
            for(i=4;i<10;++i)if(frame[i]!=255)valid=0;
            while(pos<length-4) {
                unsigned int left=length-4-pos;
                if(left<2||(unsigned int)frame[pos+1]>left-2){valid=0;break;}
                pos+=2+frame[pos+1];
            }
            if(valid){
                ++s->accepted;++s->beacons;s->bytes+=length-4;
                if(sink)sink(cookie,frame,length-4);
                if(control)control(cookie,RTL_RX_BEACON,frame,length-4);
            }
            else ++s->filtered;
        }
        rounded=(total+127)&~127U;
        if(index+1<count) {
            if(rounded>size-offset)goto malformed;
            offset+=rounded;
        } else {
            /* Last record may omit alignment padding; reject extra records. */
            if(size-offset>rounded)goto malformed;
        }
    }
    return RTL_OK;
malformed:++s->malformed;return RTL_RX_MALFORMED;
}
int rtl8188cu_rx_parse_beacons(const rtl_u8 *data,unsigned int size,struct rtl_rx_stats *s,
                              rtl_beacon_sink sink,void *cookie)
{ return parse(data,size,s,sink,0,cookie,0); }
int rtl8188cu_rx_parse_control(const rtl_u8 *data,unsigned int size,struct rtl_rx_stats *s,
                              const struct rtl_rx_filter *p,rtl_control_sink sink,void *cookie)
{ return parse(data,size,s,0,sink,cookie,p); }
int rtl8188cu_rx_parse(const rtl_u8 *data,unsigned int size,struct rtl_rx_stats *s)
{return rtl8188cu_rx_parse_beacons(data,size,s,0,0);}
