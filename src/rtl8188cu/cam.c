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

/* Checked RTL8192C CAM protocol port from OpenBSD rtwn.c; see NOTICE. */
#include "cam.h"
#define CMD 0x670
#define DATA 0x674
#define READBACK 0x678
#define BUSY 0x80000000U
static int valid_bus(const struct rtl_bus *b)
{ return b && b->read && b->write && b->delay_us; }
static int idle(const struct rtl_bus *b)
{
    unsigned int i; rtl_u32 v;
    for (i=0;i<100;++i) {
        if (b->read(b->cookie,CMD,4,&v)) return RTL_IO;
        if (!(v&BUSY)) return RTL_OK;
        b->delay_us(b->cookie,10);
    }
    return RTL_TIMEOUT;
}
static int locked(const struct rtl_bus *b)
{
    rtl_u32 v;
    if (b->read(b->cookie,0x522,1,&v)) return RTL_IO;
    if (v!=0xff) return RTL_BAD_ARGUMENT;
    if (b->read(b->cookie,0x680,2,&v)) return RTL_IO;
    if(v) return RTL_BAD_ARGUMENT;
    if (b->read(b->cookie,0x100,2,&v)) return RTL_IO;
    return (v&0x2f0)==0x2f0 ? RTL_OK : RTL_BAD_ARGUMENT;
}
static int word_report(const struct rtl_bus *b,unsigned int a,rtl_u32 v,struct rtl_cam_report *r)
{
    int e; rtl_u32 got,mask; unsigned int step=1;
    /* CTL0 bits 5..14 are reserved, not readable key storage. On the tested
     * CU, entries 0..3 read bits 8..14 as ones after a verified invalidation.
     * Compare every defined CTL0 bit and all bits of MAC/key words. */
    mask=(a%8==0) ? 0xffff801fU : 0xffffffffU;
    e=idle(b); if(e) goto fail;
    step=2; e=RTL_IO; if(b->write(b->cookie,DATA,4,v)) goto fail;
    step=3; if(b->write(b->cookie,CMD,4,BUSY|0x10000U|a)) goto fail;
    step=4; e=idle(b); if(e) goto fail;
    step=5; e=RTL_IO; if(b->write(b->cookie,CMD,4,BUSY|a)) goto fail;
    step=6; e=idle(b); if(e) goto fail;
    step=7; e=RTL_IO; if(b->read(b->cookie,READBACK,4,&got)) goto fail;
    step=8; if((got&mask)!=(v&mask)) {
        if(r) {
            ++r->mismatches; r->word_mask|=1U<<(a%8);
            r->entry_mask|=(rtl_u32)1<<(a/8);
            if(a%8==0) r->control_difference|=(rtl_u16)((got^v)&mask);
        }
        goto fail;
    }
    return RTL_OK;
fail:
    if(r && !r->error) { r->first_word=a; r->step=step; r->error=e; }
    return e;
}
static void report_init(struct rtl_cam_report *r)
{
    if(r) {
        r->first_word=0; r->step=0; r->mismatches=0; r->error=0;
        r->word_mask=0; r->entry_mask=0; r->control_difference=0;
    }
}
static int erase_report(const struct rtl_bus *b,unsigned int entry,struct rtl_cam_report *r)
{
    unsigned int i; int error=RTL_OK;
    /* Invalidate first, then erase every word even after an I/O failure. */
    /* Eight-word address stride, but only words 0..5 are implemented key
     * storage. Reserved words 6/7 cannot be treated as writable RAM. */
    for(i=0;i<6;++i) if(word_report(b,entry*8+i,0,r)) error=RTL_RESTORE;
    return error;
}
static int erase(const struct rtl_bus *b,unsigned int entry)
{ return erase_report(b,entry,0); }
int rtl8188cu_cam_clear(const struct rtl_bus *b,unsigned int entry)
{
    int e;
    if(!valid_bus(b) || entry>=32) return RTL_BAD_ARGUMENT;
    e=locked(b); return e ? e : erase(b,entry);
}
int rtl8188cu_cam_clear_all(const struct rtl_bus *b)
{ return rtl8188cu_cam_clear_all_report(b,0); }
int rtl8188cu_cam_clear_all_report(const struct rtl_bus *b,struct rtl_cam_report *r)
{
    unsigned int i; int e,failed=0;
    report_init(r);
    if(!valid_bus(b)) return RTL_BAD_ARGUMENT;
    e=locked(b); if(e) return e;
    /* Normal hardware initialization invalidates the whole CAM first. This
     * does not replace explicit key erasure/readback below. */
    e=idle(b);
    if(!e && b->write(b->cookie,CMD,4,BUSY|0x40000000U)) e=RTL_IO;
    if(!e) e=idle(b);
    if(e) {
        failed=1;
        if(r) { r->step=9; r->error=e; }
    }
    for(i=0;i<32;++i) if(erase_report(b,i,r)) failed=1;
    return failed ? RTL_RESTORE : RTL_OK;
}
static rtl_u32 le32(const rtl_u8 *p)
{ return (rtl_u32)p[0]|((rtl_u32)p[1]<<8)|((rtl_u32)p[2]<<16)|((rtl_u32)p[3]<<24); }
int rtl8188cu_cam_install(const struct rtl_bus *b,int pairwise,unsigned int keyid,
                         const rtl_u8 mac[6],const rtl_u8 key[16])
{ return rtl8188cu_cam_install_report(b,pairwise,keyid,mac,key,0); }
int rtl8188cu_cam_install_report(const struct rtl_bus *b,int pairwise,unsigned int keyid,
                         const rtl_u8 mac[6],const rtl_u8 key[16],struct rtl_cam_report *r)
{
    unsigned int i,entry,any=0; int e;
    rtl_u32 lo=0,hi=0;
    report_init(r);
    if(!valid_bus(b) || !key || (pairwise!=0 && pairwise!=1) || keyid>3 ||
       (pairwise && (!mac || keyid))) return RTL_BAD_ARGUMENT;
    if(pairwise) {
        for(i=0;i<6;++i) any|=mac[i];
        if(!any || (mac[0]&1)) return RTL_BAD_ARGUMENT;
        lo=(rtl_u32)mac[0]|((rtl_u32)mac[1]<<8); hi=le32(mac+2);
    }
    e=locked(b); if(e) return e;
    entry=pairwise ? 4 : keyid;
    e=word_report(b,entry*8,0,r); if(e) goto fail;
    for(i=0;i<4;++i) { e=word_report(b,entry*8+2+i,le32(key+4*i),r); if(e) goto fail; }
    e=word_report(b,entry*8+1,hi,r); if(e) goto fail;
    /* AES=4, valid set last, after all key bytes were read back. Entries 0..3
     * are default group slots: the index selects KeyID, CTL0 KeyID reads zero
     * on this CU. The eventual RX backend must enable RXBCKEY_DEF. Pairwise
     * entry 4 is MAC-matched and this API only accepts pairwise KeyID zero. */
    e=word_report(b,entry*8,(lo<<16)|0x8010U,r); if(e) goto fail;
    return RTL_OK;
fail:
    return erase(b,entry) ? RTL_RESTORE : e;
}
