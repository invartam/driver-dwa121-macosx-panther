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

/* Checked OpenBSD urtwn LLT/DMA port, restricted to RTL8188CU / 2 OUT pipes. */
#include "rx.h"
#define TRY(x) do { error=(x); if(error)goto fail; } while(0)
static int rd(const struct rtl_bus *b,rtl_u16 a,unsigned int w,rtl_u32 *v)
{ return b->read(b->cookie,a,w,v)?RTL_IO:RTL_OK; }
static int wr(const struct rtl_bus *b,rtl_u16 a,unsigned int w,rtl_u32 v)
{ return b->write(b->cookie,a,w,v)?RTL_IO:RTL_OK; }
static int change(const struct rtl_bus *b,rtl_u16 a,unsigned int w,rtl_u32 clear,rtl_u32 set)
{
    rtl_u32 v;int error=rd(b,a,w,&v);
    return error?error:wr(b,a,w,(v&~clear)|set);
}
static int llt(const struct rtl_bus *b,unsigned int addr,unsigned int data,int read,rtl_u32 *value)
{
    unsigned int n;int error;
    TRY(wr(b,0x1e0,4,(read?0x80000000U:0x40000000U)|(addr<<8)|data));
    for(n=0;n<20;++n) {
        TRY(rd(b,0x1e0,4,value));
        if(!(*value&0xc0000000U))return RTL_OK;
        b->delay_us(b->cookie,5);
    }
    return RTL_TIMEOUT;
fail:return error;
}
int rtl8188cu_rx_disable(const struct rtl_bus *b)
{
    rtl_u32 v;int error=RTL_OK;
    if(!b||!b->read||!b->write)return RTL_BAD_ARGUMENT;
    if(wr(b,0x100,2,0))error=RTL_IO;
    if(wr(b,0x522,1,0xff))error=RTL_IO;
    if(wr(b,0x608,4,0))error=RTL_IO;
    if(wr(b,0x6a0,2,0))error=RTL_IO;
    if(wr(b,0x6a4,2,0))error=RTL_IO;
    if(rd(b,0x100,2,&v)||v)error=RTL_IO;
    return error;
}
int rtl8188cu_dma_init(const struct rtl_bus *b,unsigned int ntx,struct rtl_dma_report *r)
{
    unsigned int i,next;rtl_u32 v;int error;
    if(!b||!b->read||!b->write||!b->delay_us||!r)return RTL_BAD_ARGUMENT;
    r->llt_written=r->llt_verified=0;r->control_at_rx=0;r->cleanup_error=0;
    if(ntx!=2)return RTL_UNSUPPORTED;
    TRY(rd(b,0x100,2,&v));if(v){error=RTL_UNSUPPORTED;goto fail;}
    for(i=0;i<256;++i) {
        next=i==248?255:(i==255?249:i+1);
        TRY(llt(b,i,next,0,&v));++r->llt_written;
    }
    for(i=0;i<256;++i) {
        next=i==248?255:(i==255?249:i+1);
        TRY(llt(b,i,0,1,&v));
        if((v&255)!=next){error=RTL_IO;goto fail;}++r->llt_verified;
    }
    TRY(wr(b,0x214,1,2));
    TRY(wr(b,0x200,4,0x80ea000cU)); /* Public 234, high 12, normal 2. */
    TRY(wr(b,0x424,1,249));TRY(wr(b,0x425,1,249));
    TRY(wr(b,0x45d,1,249));TRY(wr(b,0x114,1,249));TRY(wr(b,0x209,1,249));
    TRY(change(b,0x10c,2,0xfff4,0xfaf4)); /* 2 endpoints, bounded DMA aggregation. */
    TRY(wr(b,0x116,2,0x27ff));TRY(wr(b,0x104,1,0x11));
    TRY(change(b,0xfe55,1,8,0)); /* USB aggregation off for initial RX. */
    TRY(wr(b,0x280,1,48));
    TRY(wr(b,0xfe5b,1,4)); /* Flush partial DMA aggregates on timeout. */
    TRY(wr(b,0x60f,1,4)); /* 32-byte PHY info. */
    TRY(rd(b,0x116,2,&v));if(v!=0x27ff){error=RTL_IO;goto fail;}
    TRY(rd(b,0x104,1,&v));if(v!=0x11){error=RTL_IO;goto fail;}
    return RTL_OK;
fail:r->cleanup_error=rtl8188cu_rx_disable(b);return error;
}
int rtl8188cu_rx_control_enable(const struct rtl_bus *b)
{
    rtl_u32 v;int error;
    if(!b||!b->read||!b->write)return RTL_BAD_ARGUMENT;
    TRY(rd(b,0x100,2,&v));if(v!=0x2fa){error=RTL_UNSUPPORTED;goto fail;}
    TRY(rd(b,0x522,1,&v));if(v!=0xff){error=RTL_UNSUPPORTED;goto fail;}
    TRY(rd(b,0x680,2,&v));if(v){error=RTL_UNSUPPORTED;goto fail;}
    TRY(rd(b,0x608,4,&v));if(v!=0x9000600fU){error=RTL_UNSUPPORTED;goto fail;}
    /* OpenBSD monitor RX: admit data, retain PHY/FCS, reject control frames.
     * Only selected-peer unprotected EAPOL survives the software parser. */
    TRY(wr(b,0x6a4,2,0xffff));
    TRY(wr(b,0x608,4,0x9000680fU));
    TRY(rd(b,0x6a4,2,&v));if(v!=0xffff){error=RTL_IO;goto fail;}
    TRY(rd(b,0x608,4,&v));if(v!=0x9000680fU){error=RTL_IO;goto fail;}
    return RTL_OK;
fail:rtl8188cu_rx_disable(b);return error;
}
int rtl8188cu_rx_enable(const struct rtl_bus *b,struct rtl_dma_report *r)
{
    rtl_u32 v;int error;
    if(!b||!b->read||!b->write||!r)return RTL_BAD_ARGUMENT;
    if(r->llt_verified!=256)return RTL_BAD_ARGUMENT;
    TRY(wr(b,0x522,1,0xff));
    TRY(change(b,0x040,1,0x20,0)); /* Disable Bluetooth GPIO mux (rtwn_init). */
    /* Set MAC/BB bandwidth explicitly; calibration tables alone are not the
     * complete receive configuration. These are the 20 MHz OpenBSD settings. */
    TRY(change(b,0x603,1,3,4));
    TRY(change(b,0x800,4,1,0));TRY(change(b,0x900,4,1,0));
    TRY(change(b,0x884,4,0,0x400));
    TRY(change(b,0xc50,4,0x7f,0x20));
    TRY(change(b,0xc58,4,0x7f,0x20));
    TRY(change(b,0x102,1,3,0)); /* No-link/monitor network type. */
    TRY(wr(b,0x550,2,0x1010)); /* No beacon generation / TSF update. */
    TRY(wr(b,0x680,2,0)); /* No decryption or keys. */
    TRY(wr(b,0x6a0,2,0xffff)); /* Management frames; parser retains only beacon counts. */
    TRY(wr(b,0x6a2,2,0));TRY(wr(b,0x6a4,2,0));
    TRY(wr(b,0x608,4,0x9000600fU)); /* Broadcast mgmt, PHY info and FCS. */
    TRY(wr(b,0x124,4,0xffffffffU));
    TRY(wr(b,0x120,4,1)); /* RX completion interrupt, no TX interrupts. */
    TRY(wr(b,0x100,2,0x02fa)); /* RX DMA + both MAC blocks; TX DMA off and all TX queues paused. */
    TRY(rd(b,0x100,2,&v));r->control_at_rx=v;
    if(v!=0x2fa){error=RTL_IO;goto fail;}
    return RTL_OK;
fail:r->cleanup_error=rtl8188cu_rx_disable(b);return error;
}
/* Channel-only pause/resume of the already configured passive monitor.
 * Do not reinitialize BB/filter/security on every hop. All preconditions are
 * read back: this can never resume a station/TX/crypto configuration. */
int rtl8188cu_rx_scan_gate(const struct rtl_bus *b,int running)
{
    rtl_u32 v;int error;
    if(!b||!b->read||!b->write||(running!=0&&running!=1))return RTL_BAD_ARGUMENT;
    TRY(rd(b,0x100,2,&v));if(v!=(running?0U:0x2faU)){error=RTL_UNSUPPORTED;goto fail;}
    TRY(rd(b,0x522,1,&v));if(v!=0xff){error=RTL_UNSUPPORTED;goto fail;}
    TRY(rd(b,0x680,2,&v));if(v){error=RTL_UNSUPPORTED;goto fail;}
    TRY(rd(b,0x608,4,&v));if(v!=0x9000680fU){error=RTL_UNSUPPORTED;goto fail;}
    TRY(rd(b,0x6a0,2,&v));if(v!=0xffff){error=RTL_UNSUPPORTED;goto fail;}
    TRY(rd(b,0x6a2,2,&v));if(v){error=RTL_UNSUPPORTED;goto fail;}
    TRY(rd(b,0x6a4,2,&v));if(v!=0xffff){error=RTL_UNSUPPORTED;goto fail;}
    TRY(wr(b,0x100,2,running?0x2fa:0));
    TRY(rd(b,0x100,2,&v));if(v!=(running?0x2faU:0U)){error=RTL_IO;goto fail;}
    return RTL_OK;
fail:rtl8188cu_rx_disable(b);return error;
}
