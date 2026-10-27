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

/* Checked 1T1R port of OpenBSD rtwn/urtwn; see NOTICE. */
#include "radio.h"
#include "firmware.h"
#include "radio_tables.h"
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define TRY(x) do { error = (x); if (error) goto fail; } while (0)
#define READ(a,w,v) rd(b,a,w,&(v))
#define WRITE(a,w,v) wr(b,a,w,v)
#define CHANGE(a,w,c,s) change(b,a,w,c,s)

typedef char rtl_bb_table_sizes[(COUNT(radio_bb_regs) == COUNT(radio_bb_vals)) ? 1 : -1];
typedef char rtl_rf_table_sizes[(COUNT(radio_rf_regs) == COUNT(radio_rf_vals)) ? 1 : -1];
static int rd(const struct rtl_bus *b, rtl_u16 a, unsigned int w, rtl_u32 *v)
{ return b->read(b->cookie,a,w,v) ? RTL_IO : RTL_OK; }
static int wr(const struct rtl_bus *b, rtl_u16 a, unsigned int w, rtl_u32 v)
{ return b->write(b->cookie,a,w,v) ? RTL_IO : RTL_OK; }
static int change(const struct rtl_bus *b, rtl_u16 a, unsigned int w, rtl_u32 clear, rtl_u32 set)
{
    rtl_u32 v;
    int error = rd(b,a,w,&v);
    return error ? error : wr(b,a,w,(v & ~clear) | set);
}
static int rf_write(const struct rtl_bus *b, unsigned int address, rtl_u32 value)
{
    int error;
    if (address > 0x3f || value > 0xfffff) return RTL_BAD_ARGUMENT;
    error = WRITE(0x840,4,((rtl_u32)address << 20) | value);
    b->delay_us(b->cookie,1);
    return error;
}
static int rf_read(const struct rtl_bus *b, unsigned int address, rtl_u32 *value)
{
    rtl_u32 param, mode;
    int error;
    if (address > 0x3f) return RTL_BAD_ARGUMENT;
    TRY(READ(0x824,4,param));
    TRY(WRITE(0x824,4,param & ~0x80000000U));
    b->delay_us(b->cookie,1000);
    TRY(WRITE(0x824,4,(param & ~0x7f800000U) | ((rtl_u32)address << 23) | 0x80000000U));
    b->delay_us(b->cookie,1000);
    TRY(WRITE(0x824,4,param | 0x80000000U));
    b->delay_us(b->cookie,1000);
    TRY(READ(0x820,4,mode));
    TRY(READ((mode & 0x100) ? 0x8b8 : 0x8a0,4,*value));
    *value &= 0xfffff;
    return RTL_OK;
fail:
    return error;
}
int rtl8188cu_radio_quiesce(const struct rtl_bus *b)
{
    int error = RTL_OK;
    rtl_u32 v;
    if (!b || !b->read || !b->write) return RTL_BAD_ARGUMENT;
    /* End IQ mode before removing BB clocks; always attempt the later steps. */
    if (WRITE(0xe28,4,0)) error = RTL_IO;
    if (CHANGE(0x800,4,0x03000000U,0)) error = RTL_IO;
    if (CHANGE(0x002,2,3,0)) error = RTL_IO;
    if (rtl8188cu_quiesce(b)) error = RTL_IO;
    if (READ(0x002,2,v) || (v & 3)) error = RTL_IO;
    return error;
}
static int signed10(unsigned int x) { return (x & 0x200) ? (int)x - 1024 : (int)x; }
int rtl8188cu_iq_agrees(const struct rtl_iq_sample *a, const struct rtl_iq_sample *b)
{
    unsigned int i;
    int delta;
    if (!a || !b || !a->valid || !b->valid) return 0;
    for (i = 0; i < 2; ++i) {
        if (a->tx[i] > 1023 || b->tx[i] > 1023 || a->rx[i] > 1023 || b->rx[i] > 1023) return 0;
        delta = signed10(a->tx[i]) - signed10(b->tx[i]);
        if (delta < -5 || delta > 5) return 0;
        delta = signed10(a->rx[i]) - signed10(b->rx[i]);
        if (delta < -5 || delta > 5) return 0;
    }
    return 1;
}
/* Every temporary IQ register is saved before the first write, then restored
 * even on error. Strobe registers and RF writes are handled explicitly. */
static const struct { rtl_u16 address; unsigned int width; } iq_saved[] = {
    {0x85c,4},{0xe6c,4},{0xe70,4},{0xe74,4},{0xe78,4},{0xe7c,4},
    {0xe80,4},{0xe84,4},{0xe88,4},{0xe8c,4},{0xed0,4},{0xed4,4},
    {0xed8,4},{0xedc,4},{0xee0,4},{0xeec,4},
    {0x522,1},{0x550,1},{0x551,1},{0x040,4},
    {0xc04,4},{0xc08,4},{0x870,4},{0x874,4},{0x860,4},{0x864,4},
    {0xb68,4},{0xb6c,4},{0xa04,4},{0x820,4},{0x828,4},
    {0xe30,4},{0xe34,4},{0xe38,4},{0xe3c,4},{0xe40,4},{0xe44,4},
    {0xe4c,4},{0xe54,4}
};
static int iq_run(const struct rtl_bus *b, struct rtl_iq_sample *sample, int *restore_error)
{
    rtl_u32 saved[COUNT(iq_saved)], v, status;
    unsigned int i, attempt;
    int error = RTL_OK, cleanup = RTL_OK;
    sample->valid = 0;
    sample->tx[0] = sample->tx[1] = sample->rx[0] = sample->rx[1] = 0;
    /* No mutation until the whole snapshot has succeeded. */
    for (i = 0; i < COUNT(iq_saved); ++i)
        if (rd(b,iq_saved[i].address,iq_saved[i].width,&saved[i])) return RTL_IO;
    TRY(WRITE(0x85c,4,0x0b1b25a0));
    for (i = 1; i < 16; ++i) TRY(WRITE(iq_saved[i].address,4,0x0bdb25a0));
    TRY(CHANGE(0x820,4,0,0x100));
    TRY(CHANGE(0x828,4,0,0x100));
    TRY(WRITE(0xc04,4,0x03a05600));
    TRY(WRITE(0xc08,4,0x000800e4));
    TRY(WRITE(0x874,4,0x22204000));
    /* Keep ALL queues paused, including beacon; MAC TX/RX are also disabled. */
    TRY(WRITE(0x522,1,0xff));
    TRY(CHANGE(0x550,1,8,0));
    TRY(CHANGE(0x551,1,8,0));
    TRY(CHANGE(0x040,1,0x20,0));
    TRY(WRITE(0xb68,4,0x00080000));
    TRY(WRITE(0xe28,4,0x80800000));
    TRY(WRITE(0xe40,4,0x01007c00));
    TRY(WRITE(0xe44,4,0x01004800));
    for (attempt = 0; attempt < 2; ++attempt) {
        TRY(WRITE(0xe30,4,0x10008c1f));
        TRY(WRITE(0xe54,4,0x10008c1f));
        TRY(WRITE(0xe38,4,0x82140102));
        TRY(WRITE(0xe3c,4,0x28160502));
        TRY(WRITE(0xe4c,4,0x001028d1));
        TRY(WRITE(0xe48,4,0xf9000000));
        TRY(WRITE(0xe48,4,0xf8000000));
        b->delay_us(b->cookie,1000);
        TRY(READ(0xeac,4,status));
        TRY(READ(0xe94,4,v)); sample->tx[0] = (v >> 16) & 0x3ff;
        TRY(READ(0xe9c,4,v)); sample->tx[1] = (v >> 16) & 0x3ff;
        TRY(READ(0xea4,4,v)); sample->rx[0] = (v >> 16) & 0x3ff;
        sample->rx[1] = (status >> 16) & 0x3ff;
        sample->valid = !(status & 0x18000000U) &&
            sample->tx[0] != 0x142 && sample->tx[1] != 0x042 &&
            sample->rx[0] != 0x132 && sample->rx[1] != 0x036 &&
            sample->tx[0] != 0 && sample->rx[0] != 0;
        if (sample->valid) break;
    }
fail:
    if (WRITE(0xe28,4,0)) cleanup = RTL_IO;
    if (WRITE(0x840,4,0x00032ed3)) cleanup = RTL_IO;
    for (i = COUNT(iq_saved); i > 0; --i)
        if (wr(b,iq_saved[i-1].address,iq_saved[i-1].width,saved[i-1])) cleanup = RTL_IO;
    /* Compare the complete saved state, not just successful USB completions. */
    for (i = 0; i < COUNT(iq_saved); ++i)
        if (rd(b,iq_saved[i].address,iq_saved[i].width,&v) || v != saved[i]) cleanup = RTL_IO;
    if (cleanup) { *restore_error = cleanup; sample->valid = 0; }
    return error ? error : cleanup;
}
/* Portable arithmetic right shift: no reliance on signed-shift or long width. */
static int floor_div(int value, int divisor)
{ return value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor); }
static int iq_apply(const struct rtl_bus *b, const struct rtl_iq_sample *s)
{
    rtl_u32 reg, scale, x, y;
    int error;
    TRY(READ(0xc80,4,reg)); scale = (reg >> 22) & 0x3ff;
    x = (rtl_u32)floor_div(signed10(s->tx[0]) * (int)scale,256);
    y = (rtl_u32)floor_div(signed10(s->tx[1]) * (int)scale,256);
    TRY(CHANGE(0xc80,4,0x003f03ff,(x & 0x3ff) | ((y & 0x3f) << 16)));
    TRY(CHANGE(0xc94,4,0xf0000000U,(y & 0x3c0) << 22));
    x = (rtl_u32)floor_div(signed10(s->tx[0]) * (int)scale,128);
    y = (rtl_u32)floor_div(signed10(s->tx[1]) * (int)scale,128);
    TRY(CHANGE(0xc4c,4,0xa0000000U,((x & 1) << 31) | ((y & 1) << 29)));
    TRY(CHANGE(0xc14,4,0x0000ffff,s->rx[0] | ((rtl_u32)(s->rx[1] & 0x3f) << 10)));
    TRY(CHANGE(0xca0,4,0xf0000000U,(rtl_u32)(s->rx[1] & 0x3c0) << 22));
    return RTL_OK;
fail:
    return error;
}
static int lc_calibrate(const struct rtl_bus *b, struct rtl_radio_report *r)
{
    rtl_u32 channel, txmode;
    unsigned int n;
    int error;
    TRY(READ(0xd03,1,txmode));
    /* Diagnostic should never be in continuous TX; fail rather than resume it. */
    if (txmode & 0x70) return RTL_UNSUPPORTED;
    TRY(WRITE(0x522,1,0xff));
    TRY(rf_read(b,0x18,&channel));
    TRY(rf_write(b,0x18,channel | 0x8000));
    for (n = 0; n < 100; ++n) {
        b->delay_us(b->cookie,1000);
        TRY(rf_read(b,0x18,&r->rf_channel_after));
        if (!(r->rf_channel_after & 0x8000)) {
            if ((r->rf_channel_after & ~0x8000U) != (channel & ~0x8000U)) return RTL_CALIBRATION_FAILED;
            r->lc_verified = 1;
            return RTL_OK;
        }
    }
    return RTL_TIMEOUT;
fail:
    return error;
}
int rtl8188cu_radio_init(const struct rtl_bus *b, const struct rtl_identity *id,
                        struct rtl_radio_report *r)
{
    unsigned int i, run;
    rtl_u32 value, env;
    int error;
    struct rtl_iq_sample previous, current;
    if (!b || !b->read || !b->write || !b->delay_us || !id || !r) return RTL_BAD_ARGUMENT;
    r->stage = RTL_RADIO_VALIDATE; r->cleanup_error = r->calibration_restore_error = 0;
    r->mac_writes = r->bb_writes = r->agc_writes = r->rf_writes = r->iq_runs = 0;
    r->rf_channel_before = r->rf_channel_after = r->bb_rfmod = 0;
    r->lc_verified = r->iq.valid = 0;
    r->iq.tx[0] = r->iq.tx[1] = r->iq.rx[0] = r->iq.rx[1] = 0;
    r->last_iq = r->iq;
    previous.valid = 0;
    if (id->vid != 0x2001 || id->pid != 0x3308 || id->board_type != 0 ||
        (id->sys_cfg & ~0x3fU) != (0x04400735U & ~0x3fU)) return RTL_UNSUPPORTED;
    TRY(READ(0x80,4,value));
    if ((value & 0xc6) != 0xc6) { error = RTL_UNSUPPORTED; goto fail; }
    TRY(READ(0x100,2,value));
    if (value) { error = RTL_UNSUPPORTED; goto fail; }
    TRY(WRITE(0x522,1,0xff));
    r->stage = RTL_RADIO_MAC;
    for (i = 0; i < COUNT(radio_mac); ++i) {
        TRY(WRITE(radio_mac[i].reg,1,radio_mac[i].val)); ++r->mac_writes;
    }
    /* No automatic responses, RX filters or security until a station exists. */
    TRY(WRITE(0x608,4,0));
    TRY(WRITE(0x680,2,0));
    for (i = 0; i < 6; ++i) TRY(WRITE(0x610+i,1,id->mac[i]));
    r->stage = RTL_RADIO_BB;
    TRY(CHANGE(0x002,2,0,0x2003));
    TRY(WRITE(0x028,2,0xdb83));
    TRY(WRITE(0x01f,1,7));
    TRY(WRITE(0x002,1,0x17));
    TRY(WRITE(0x022,1,0x0f));
    TRY(WRITE(0x015,1,0xe9));
    TRY(WRITE(0x025,1,0x80));
    for (i = 0; i < COUNT(radio_bb_regs); ++i) {
        TRY(WRITE(radio_bb_regs[i],4,radio_bb_vals[i]));
        b->delay_us(b->cookie,1); ++r->bb_writes;
    }
    for (i = 0; i < COUNT(radio_agc); ++i) {
        TRY(WRITE(0xc78,4,radio_agc[i]));
        b->delay_us(b->cookie,1); ++r->agc_writes;
    }
    r->stage = RTL_RADIO_RF;
    TRY(READ(0x870,4,env)); env &= 0x10;
    TRY(CHANGE(0x860,4,0,0x100000)); b->delay_us(b->cookie,50);
    TRY(CHANGE(0x860,4,0,0x10)); b->delay_us(b->cookie,50);
    TRY(CHANGE(0x824,4,0x400,0)); b->delay_us(b->cookie,50);
    TRY(CHANGE(0x824,4,0x800,0)); b->delay_us(b->cookie,50);
    for (i = 0; i < COUNT(radio_rf_regs); ++i) {
        if (radio_rf_regs[i] == 0xfe) { b->delay_us(b->cookie,50000); continue; }
        TRY(rf_write(b,radio_rf_regs[i],radio_rf_vals[i]));
        b->delay_us(b->cookie,5); ++r->rf_writes;
    }
    TRY(CHANGE(0x870,4,0x10,env));
    TRY(rf_read(b,0x18,&r->rf_channel_before));
    /* This table sets channel 1, 20 MHz. Never continue on a floating RF bus. */
    if ((r->rf_channel_before & ~0x8000U) != 0x07401) { error = RTL_CALIBRATION_FAILED; goto fail; }
    TRY(CHANGE(0x800,4,0,0x03000000));
    r->stage = RTL_RADIO_IQ;
    for (run = 0; run < 3; ++run) {
        TRY(iq_run(b,&current,&r->calibration_restore_error));
        ++r->iq_runs; r->last_iq = current;
        if (rtl8188cu_iq_agrees(&previous,&current)) { r->iq = current; break; }
        previous = current;
    }
    if (!r->iq.valid) { error = RTL_CALIBRATION_FAILED; goto fail; }
    TRY(iq_apply(b,&r->iq));
    r->stage = RTL_RADIO_LC;
    TRY(lc_calibrate(b,r));
    r->stage = RTL_RADIO_PA;
    TRY(WRITE(0xfe40,1,0xe0)); TRY(WRITE(0xfe41,1,0x8d)); TRY(WRITE(0xfe42,1,0x80));
    if (!(id->pa_setting & 1)) {
        TRY(rf_write(b,0x15,0x0f406)); TRY(rf_write(b,0x15,0x4f406));
        TRY(rf_write(b,0x15,0x8f406)); TRY(rf_write(b,0x15,0xcf406));
    }
    if (!(id->pa_setting & 0x10)) TRY(CHANGE(0x016,1,0xf0,0x90));
    r->stage = RTL_RADIO_VERIFY;
    TRY(READ(0x800,4,r->bb_rfmod));
    if (r->bb_rfmod != 0x83040000U) { error = RTL_IO; goto fail; }
    TRY(READ(0x100,2,value)); if (value) { error = RTL_IO; goto fail; }
    TRY(READ(0x522,1,value)); if (value != 0xff) { error = RTL_IO; goto fail; }
    r->stage = RTL_RADIO_DONE;
    return RTL_OK;
fail:
    r->cleanup_error = rtl8188cu_radio_quiesce(b);
    return error;
}

/* Channel portion of OpenBSD rtwn_set_chan (20 MHz), without TX power setup.
 * This API is explicitly RX-only; callers must not enable TX afterward. */
int rtl8188cu_set_rx_channel(const struct rtl_bus *b, unsigned int channel, rtl_u32 *actual)
{
    rtl_u32 value, wanted;
    int error;
    if (!b || !b->read || !b->write || !b->delay_us || !actual) return RTL_BAD_ARGUMENT;
    if (channel < 1 || channel > 11) return RTL_UNSUPPORTED;
    TRY(READ(0x100,2,value));
    if (value) return RTL_UNSUPPORTED;
    TRY(rf_read(b,0x18,&value));
    wanted = (value & ~0x8fffU) | 0x400U | channel;
    TRY(rf_write(b,0x18,wanted));
    b->delay_us(b->cookie,1000);
    TRY(rf_read(b,0x18,actual));
    return *actual == wanted ? RTL_OK : RTL_IO;
fail:
    return error;
}
