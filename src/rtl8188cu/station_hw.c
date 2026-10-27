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

/* Checked subset of rtwn initialization. See NOTICE and station-hw.md. */
#include "station_hw.h"
#include "txpower.h"
#include "cam.h"
#define TRY(x) do { error=(x); if(error) goto fail; } while(0)
static int readreg(const struct rtl_bus *b,rtl_u16 a,unsigned int w,rtl_u32 *v)
{ return b->read(b->cookie,a,w,v) ? RTL_IO : RTL_OK; }
static int checked(const struct rtl_bus *b,rtl_u16 a,unsigned int w,rtl_u32 v)
{
    rtl_u32 actual;
    if(b->write(b->cookie,a,w,v) || b->read(b->cookie,a,w,&actual) || actual!=v) return RTL_IO;
    return RTL_OK;
}
static int change(const struct rtl_bus *b,rtl_u16 a,unsigned int w,rtl_u32 clear,rtl_u32 set)
{
    rtl_u32 v; int e=readreg(b,a,w,&v);
    return e ? e : checked(b,a,w,(v&~clear)|set);
}
int rtl8188cu_station_hw_block(const struct rtl_bus *b)
{
    int bad=0;
    if(!b || !b->read || !b->write) return RTL_BAD_ARGUMENT;
    /* Attempt both even after an error; readback must confirm both. */
    if(checked(b,0x522,1,255)) bad=1;
    if(checked(b,0x100,2,0x2fa)) bad=1;
    return bad ? RTL_RESTORE : RTL_OK;
}
int rtl8188cu_station_hw_begin(const struct rtl_bus *b,const struct rtl_identity *id,
                             const rtl_u8 ap[6],unsigned int ch)
{
    static const rtl_u16 sifs[]={0x428,0x63a,0x514,0x516,0x63c,0x63e};
    struct rtl_txpower power;
    rtl_u32 v; unsigned int i,any=0,mac=0,same=1; int error;
    if(!b || !b->read || !b->write || !id || !ap || ch<1 || ch>11) return RTL_BAD_ARGUMENT;
    for(i=0;i<6;++i) { any|=ap[i]; mac|=id->mac[i]; if(ap[i]!=id->mac[i]) same=0; }
    if(!any || !mac || same || (ap[0]&1) || (id->mac[0]&1)) return RTL_BAD_ARGUMENT;
    TRY(rtl8188cu_txpower_compute(id,ch,&power));
    TRY(readreg(b,0x100,2,&v)); if(v!=0x2fa) { error=RTL_UNSUPPORTED; goto fail; }
    TRY(readreg(b,0x522,1,&v)); if(v!=255) { error=RTL_UNSUPPORTED; goto fail; }
    TRY(readreg(b,0x680,2,&v)); if(v) { error=RTL_UNSUPPORTED; goto fail; }
    TRY(readreg(b,0x608,4,&v)); if(v!=0x9000680fU) { error=RTL_UNSUPPORTED; goto fail; }
    TRY(rtl8188cu_txpower_program(b,&power));
    for(i=0;i<6;++i) {
        TRY(checked(b,(rtl_u16)(0x610+i),1,id->mac[i]));
        TRY(checked(b,(rtl_u16)(0x618+i),1,ap[i]));
    }
    TRY(change(b,0x102,1,3,0)); /* no-link until response; no AP/beacon mode */
    TRY(checked(b,0x550,2,0x1010)); /* TSF updates and beacon generation off */
    TRY(change(b,0x440,4,0x00ffffffU,1)); /* CCK1 response, long preamble */
    TRY(checked(b,0x480,1,0)); /* RTS CCK1 */
    TRY(checked(b,0x42a,2,0x0606)); /* short/long retries capped at six */
    for(i=0;i<sizeof(sifs)/sizeof(sifs[0]);++i) TRY(checked(b,sifs[i],2,0x100a));
    TRY(checked(b,0x51b,1,20)); /* conservative long slot before association */
    TRY(checked(b,0x640,1,0x40));
    /* Legacy non-QoS contention, no TXOP; AC queues remain paused. */
    for(i=0;i<4;++i) TRY(checked(b,(rtl_u16)(0x500+4*i),4,0x0000a42a));
    TRY(change(b,0xc50,4,0x7f,0x32));
    TRY(change(b,0xc58,4,0x7f,0x32));
    /* Address-match rather than promiscuous reception, retain beacon/data
     * admission for software-selected peer validation; no crypto or data TX. */
    TRY(checked(b,0x608,4,0x9000680eU));
    TRY(checked(b,0x100,2,0x2ff));
    /* RX-only initialization loaded RQPN with TX DMA clocks stopped. Reload
     * with clocks active, matching upstream power-on -> DMA-init ordering.
     * No OUT is pending and all queues remain paused. LD self-clears. */
    TRY(checked(b,0x214,1,2));
    if(b->write(b->cookie,0x200,4,0x80ea000cU)) { error=RTL_IO; goto fail; }
    TRY(readreg(b,0x200,4,&v));
    if(v!=0x00ea000cU) { error=RTL_IO; goto fail; }
    /* Clear MGNT and the aggregate BCN_HIGH_MGNT pause. Dedicated beacon,
     * high and all AC pause bits remain set. */
    TRY(checked(b,0x522,1,0x6f)); /* management only, unpause LAST */
    return RTL_OK;
fail:
    return rtl8188cu_station_hw_block(b)==RTL_OK ? error : RTL_RESTORE;
}
int rtl8188cu_station_hw_wpa_prepare(const struct rtl_bus *b)
{
    rtl_u32 v;int error;
    if(!b||!b->read||!b->write||!b->delay_us)return RTL_BAD_ARGUMENT;
    TRY(readreg(b,0x100,2,&v));if(v!=0x2fa){error=RTL_UNSUPPORTED;goto fail;}
    TRY(rtl8188cu_cam_clear_all(b));
    return RTL_OK;
fail:
    return rtl8188cu_station_hw_block(b)==RTL_OK?error:RTL_RESTORE;
}
int rtl8188cu_station_hw_eapol(const struct rtl_bus *b)
{
    rtl_u32 v;int error;
    if(!b||!b->read||!b->write||!b->delay_us)return RTL_BAD_ARGUMENT;
    TRY(readreg(b,0x100,2,&v));if(v!=0x2ff){error=RTL_UNSUPPORTED;goto fail;}
    TRY(readreg(b,0x522,1,&v));if(v!=0x6f){error=RTL_UNSUPPORTED;goto fail;}
    TRY(readreg(b,0x680,2,&v));if(v){error=RTL_UNSUPPORTED;goto fail;}
    TRY(checked(b,0x522,1,255));
    TRY(change(b,0x102,1,3,2)); /* MSR infrastructure, TSF/beacon still disabled */
    TRY(checked(b,0x522,1,0x6b)); /* only MGNT and BE, software EAPOL-only gate */
    return RTL_OK;
fail:
    return rtl8188cu_station_hw_block(b)==RTL_OK?error:RTL_RESTORE;
}
int rtl8188cu_station_hw_key(const struct rtl_bus *b,int pair,unsigned int index,
                            const rtl_u8 ap[6],const rtl_u8 key[16])
{
    rtl_u32 v;int error;
    if(!b||!b->read||!b->write||!b->delay_us)return RTL_BAD_ARGUMENT;
    TRY(readreg(b,0x522,1,&v));if(v!=0x6b){error=RTL_UNSUPPORTED;goto fail;}
    TRY(checked(b,0x522,1,255));
    TRY(rtl8188cu_cam_install(b,pair,index,ap,key));
    TRY(checked(b,0x522,1,0x6b));
    return RTL_OK;
fail:
    return rtl8188cu_station_hw_block(b)==RTL_OK?error:RTL_RESTORE;
}
int rtl8188cu_station_hw_erase(const struct rtl_bus *b)
{
    unsigned int i;int bad=0;
    if(!b||!b->read||!b->write||!b->delay_us)return RTL_BAD_ARGUMENT;
    if(checked(b,0x522,1,255))bad=1;
    /* The probe never enables SECCFG. Clear only with the invariant verified. */
    for(i=0;i<5;++i)if(rtl8188cu_cam_clear(b,i))bad=1;
    return bad?RTL_RESTORE:RTL_OK;
}
