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

/* RTL8188CU 1T1R power tables/packing from OpenBSD rtwn/r92creg; see NOTICE. */
#include "txpower.h"
static const rtl_u8 base[3][16]={
    {12,12,12,10,8,6,4,2,14,13,12,10,8,6,4,2},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {4,4,4,4,4,2,2,0,0,0,0,0,0,0,0,0}
};
int rtl8188cu_txpower_compute(const struct rtl_identity *id,unsigned int ch,
                            struct rtl_txpower *p)
{
    unsigned int g,i,reg,cck,ht,ofdm,diff,lim,v;
    if(!p) return RTL_BAD_ARGUMENT;
    for(i=0;i<20;++i) p->rate[i]=0;
    if(!id || ch<1 || ch>11) return RTL_BAD_ARGUMENT;
    if((id->sys_cfg&~0x3fU)!=(0x04400735U&~0x3fU) ||
       id->rom[0]!=0x29 || id->rom[1]!=0x81 || (id->rom[0x79]>>5)!=0)
        return RTL_UNSUPPORTED;
    reg=id->rom[0x79]&7; if(reg>3) return RTL_UNSUPPORTED;
    g=ch<=3 ? 0 : ch<=9 ? 1 : 2;
    cck=id->rom[0x5a+g]; ht=id->rom[0x60+g];
    if(cck>63 || ht>63 || id->rom[0x69+g]==255 ||
       id->rom[0x6c+g]==255 || (reg==3 && id->rom[0x72+g]==255))
        return RTL_BAD_ROM;
    ofdm=ht+(id->rom[0x6c+g]&15);
    diff=id->rom[0x69+g]&15; ht+=diff;
    lim=id->rom[0x72+g]&15;
    for(i=0;i<4;++i) p->rate[i]=(rtl_u8)cck;
    for(i=4;i<20;++i) {
        if(reg==2) v=0;
        else if(reg==1) v=base[g][i-4];
        else { v=base[0][i-4]; if(reg==3 && v>lim) v=lim; }
        v+=i<12 ? ofdm : ht;
        p->rate[i]=(rtl_u8)(v>63 ? 63 : v);
    }
    return RTL_OK;
}
static rtl_u32 pack(const rtl_u8 *p)
{ return (rtl_u32)p[0]|((rtl_u32)p[1]<<8)|((rtl_u32)p[2]<<16)|((rtl_u32)p[3]<<24); }
int rtl8188cu_txpower_program(const struct rtl_bus *b,const struct rtl_txpower *p)
{
    static const rtl_u16 addr[6]={0xe08,0x86c,0xe00,0xe04,0xe10,0xe14};
    rtl_u32 saved[6],out[6],v; unsigned int i,touched=0; int bad=0;
    if(!b || !b->read || !b->write || !p) return RTL_BAD_ARGUMENT;
    for(i=0;i<20;++i) if(p->rate[i]>63) return RTL_BAD_ARGUMENT;
    if(b->read(b->cookie,0x522,1,&v)) return RTL_IO;
    if(v!=255) return RTL_BAD_ARGUMENT;
    if(b->read(b->cookie,0x100,2,&v)) return RTL_IO;
    if(v&4) return RTL_BAD_ARGUMENT;
    for(i=0;i<6;++i) if(b->read(b->cookie,addr[i],4,&saved[i])) return RTL_IO;
    out[0]=(saved[0]&~0xff00U)|((rtl_u32)p->rate[0]<<8);
    out[1]=(saved[1]&255)|((rtl_u32)p->rate[1]<<8)|
           ((rtl_u32)p->rate[2]<<16)|((rtl_u32)p->rate[3]<<24);
    for(i=2;i<6;++i) out[i]=pack(p->rate+4+(i-2)*4);
    for(i=0;i<6;++i) {
        touched=i+1;
        if(b->write(b->cookie,addr[i],4,out[i]) ||
           b->read(b->cookie,addr[i],4,&v) || v!=out[i]) goto fail;
    }
    return RTL_OK;
fail:
    for(i=0;i<touched;++i) {
        if(b->write(b->cookie,addr[i],4,saved[i])) bad=1;
        if(b->read(b->cookie,addr[i],4,&v) || v!=saved[i]) bad=1;
    }
    return bad ? RTL_RESTORE : RTL_IO;
}
