/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2014 Kevin Lo <kevlo@FreeBSD.org>
 * Copyright (c) 2015 Stefan Sperling <stsp@openbsd.org>
 * Copyright (c) 2016 Nathanial Sloss <nathanialsloss@yahoo.com.au>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* RTL8192CU descriptor layout from OpenBSD if_urtwn/r92creg; see NOTICE. */
#include "tx.h"
static unsigned int le16(const rtl_u8 *p)
{ return p[0]|((unsigned int)p[1]<<8); }
static void put16(rtl_u8 *p,unsigned int v)
{ p[0]=(rtl_u8)v; p[1]=(rtl_u8)(v>>8); }
static void put32(rtl_u8 *p,rtl_u32 v)
{ unsigned int i; for(i=0;i<4;++i) p[i]=(rtl_u8)(v>>(8*i)); }
static int equal(const rtl_u8 *a,const rtl_u8 *b,unsigned int n)
{ unsigned int i; for(i=0;i<n;++i) if(a[i]!=b[i]) return 0; return 1; }
static int addresses(const rtl_u8 *mac,const rtl_u8 *ap)
{
    unsigned int i,m=0,a=0;
    if(!mac || !ap || (mac[0]&1) || (ap[0]&1) || equal(mac,ap,6)) return 0;
    for(i=0;i<6;++i) { m|=mac[i]; a|=ap[i]; }
    return m && a;
}
static int eapol_valid(const rtl_u8 *p,unsigned int n)
{
    return p && n>=99 && n<=2048 && (p[0]==1 || p[0]==2) &&
        p[1]==3 && (((unsigned int)p[2]<<8)|p[3])==n-4 && p[4]==2;
}
int rtl8188cu_eapol_frame(rtl_u8 *out,unsigned int cap,unsigned int *length,
                        const rtl_u8 mac[6],const rtl_u8 ap[6],unsigned int seq,
                        const rtl_u8 *eapol,unsigned int n)
{
    static const rtl_u8 llc[8]={0xaa,0xaa,3,0,0,0,0x88,0x8e};
    unsigned int i;
    if(!length) return RTL_BAD_ARGUMENT;
    *length=0;
    if(!out || !addresses(mac,ap) || seq>4095 || !eapol_valid(eapol,n) || cap<32+n)
        return RTL_BAD_ARGUMENT;
    for(i=0;i<32;++i) out[i]=0;
    out[0]=8; out[1]=1;
    for(i=0;i<6;++i) { out[4+i]=ap[i]; out[10+i]=mac[i]; out[16+i]=ap[i]; }
    put16(out+22,seq<<4);
    for(i=0;i<8;++i) out[24+i]=llc[i];
    for(i=0;i<n;++i) out[32+i]=eapol[i];
    *length=32+n; return RTL_OK;
}
static int prepare(struct rtl_tx_packet *p,const rtl_u8 *f,unsigned int n,
                        const rtl_u8 mac[6],const rtl_u8 ap[6],unsigned int mps,int ccmp)
{
    static const rtl_u8 llc[8]={0xaa,0xaa,3,0,0,0,0x88,0x8e};
    unsigned int i,sum=0,data=0; rtl_u32 dw1;
    if(!p) return RTL_BAD_ARGUMENT;
    p->length=0; p->endpoint=0; p->needs_zlp=0;
    if(!f || n<24 || n>RTL_TX_FRAME_MAX || (mps!=64 && mps!=512) ||
       !addresses(mac,ap) || !equal(f+4,ap,6) || !equal(f+10,mac,6) ||
       (!ccmp&&!equal(f+16,ap,6)) || (le16(f+22)&15)) return RTL_BAD_ARGUMENT;
    /* Only Retry may vary in FC1; no fragmentation/protection/power-save. */
    if(ccmp) {
        if(n<48||n>1548||f[0]!=8||f[1]!=0x41||f[26]||f[27]!=0x20||
           !(f[24]|f[25]|f[28]|f[29]|f[30]|f[31]))return RTL_BAD_ARGUMENT;
        data=1;
    } else if(f[0]==0xb0 && (f[1]&~8U)==0) {
        if(n!=30 || le16(f+24)!=0 || le16(f+26)!=1 || le16(f+28)!=0)
            return RTL_BAD_ARGUMENT;
    } else if(f[0]==0 && (f[1]&~8U)==0) {
        if(n<28 || n>512) return RTL_BAD_ARGUMENT;
        i=28;
        while(i<n) { if(n-i<2 || f[i+1]>n-i-2) return RTL_BAD_ARGUMENT; i+=2+f[i+1]; }
    } else if(f[0]==8 && (f[1]&~8U)==1) {
        if(n<32 || !equal(f+24,llc,8) || !eapol_valid(f+32,n-32)) return RTL_BAD_ARGUMENT;
        data=1;
    } else return RTL_BAD_ARGUMENT;
    for(i=0;i<32;++i) p->bytes[i]=0;
    put32(p->bytes,0x8c200000U|n);
    /* Conservative CCK1 long preamble, no aggregation, no HW sequence rewrite.
     * MACID BSS=0, 11B RAID=6; high/mgmt vs normal/BE endpoint mapping. */
    dw1=0x00060000U|(data ? 0x40U : 0x1200U);
    put32(p->bytes+4,dw1);
    put16(p->bytes+14,le16(f+22)>>4);
    put32(p->bytes+16,0x100U); /* driver selects rate */
    put32(p->bytes+20,0x001a0000U); /* retry limit enabled, 6 attempts */
    for(i=0;i<16;++i) sum^=le16(p->bytes+2*i);
    put16(p->bytes+28,sum);
    for(i=0;i<n;++i) p->bytes[32+i]=f[i];
    p->length=n+32; p->endpoint=data ? 3 : 2;
    p->needs_zlp=(p->length%mps)==0;
    return RTL_OK;
}
int rtl8188cu_tx_prepare(struct rtl_tx_packet *p,const rtl_u8 *f,unsigned int n,
    const rtl_u8 mac[6],const rtl_u8 ap[6],unsigned int mps)
{ return prepare(p,f,n,mac,ap,mps,0); }
int rtl8188cu_tx_prepare_ccmp(struct rtl_tx_packet *p,const rtl_u8 *f,unsigned int n,
    const rtl_u8 mac[6],const rtl_u8 ap[6],unsigned int mps)
{ return prepare(p,f,n,mac,ap,mps,1); }
