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

/* RTL8188CU USB power/firmware port from OpenBSD; see NOTICE. */
#include "firmware.h"
#define MCUFWDL 0x80
#define SYS_FUNC 0x02
#define FW_EN 0x01U
#define FW_RDY 0x02U
#define FW_CHECKSUM 0x04U
#define FW_INIT 0x40U
#define FW_RAM 0x80U
#define CPU_EN 0x0400U
#define TRY(expression) do { error = (expression); if (error) goto fail; } while (0)

static unsigned int le16(const rtl_u8 *p) { return p[0] | ((unsigned int)p[1] << 8); }
int rtl8188cu_validate_firmware(const rtl_u8 *data, unsigned int length, struct rtl_fw_image *image)
{
    unsigned int size;
    if (!data || !image || length < 33) return RTL_BAD_ARGUMENT;
    image->payload = 0; image->size = 0;
    if ((le16(data) >> 4) != 0x88c) return RTL_UNSUPPORTED;
    size = le16(data + 12);
    if (!size || size > 8U * 4096U || length - 32 != size) return RTL_BAD_ROM;
    image->payload = data + 32;
    image->size = size;
    image->version = le16(data + 4);
    image->subversion = le16(data + 6);
    return RTL_OK;
}
static int readreg(const struct rtl_bus *b, rtl_u16 reg, unsigned int width, rtl_u32 *value)
{ return b->read(b->cookie, reg, width, value) ? RTL_IO : RTL_OK; }
static int writereg(const struct rtl_bus *b, rtl_u16 reg, unsigned int width, rtl_u32 value)
{ return b->write(b->cookie, reg, width, value) ? RTL_IO : RTL_OK; }
static int modify(const struct rtl_bus *b, rtl_u16 reg, unsigned int width, rtl_u32 clear, rtl_u32 set)
{
    rtl_u32 value;
    int error = readreg(b, reg, width, &value);
    if (error) return error;
    return writereg(b, reg, width, (value & ~clear) | set);
}
static int poll(const struct rtl_bus *b, rtl_u16 reg, unsigned int width,
                rtl_u32 mask, rtl_u32 expected, unsigned int delay, unsigned int count)
{
    unsigned int n;
    rtl_u32 value;
    for (n = 0; n < count; ++n) {
        if (readreg(b, reg, width, &value)) return RTL_IO;
        if ((value & mask) == expected) return RTL_OK;
        b->delay_us(b->cookie, delay);
    }
    return RTL_TIMEOUT;
}
int rtl8188cu_quiesce(const struct rtl_bus *b)
{
    int error = RTL_OK;
    rtl_u32 value;
    if (!b || !b->read || !b->write) return RTL_BAD_ARGUMENT;
    /* Continue cleanup after errors, including ambiguous failed writes. */
    if (writereg(b, 0x522, 1, 0xff)) error = RTL_IO;
    if (writereg(b, 0x100, 2, 0)) error = RTL_IO;
    if (writereg(b, 0x01f, 1, 0)) error = RTL_IO;
    if (writereg(b, 0x120, 4, 0)) error = RTL_IO;
    if (writereg(b, 0x128, 4, 0)) error = RTL_IO;
    if (modify(b, SYS_FUNC, 2, CPU_EN, 0)) error = RTL_IO;
    if (writereg(b, MCUFWDL, 1, 0)) error = RTL_IO;
    if (readreg(b, SYS_FUNC, 2, &value) || (value & CPU_EN)) error = RTL_IO;
    if (readreg(b, MCUFWDL, 1, &value) || (value & (FW_EN | FW_RDY))) error = RTL_IO;
    if (readreg(b, 0x100, 2, &value) || value) error = RTL_IO;
    if (readreg(b, 0x01f, 1, &value) || value) error = RTL_IO;
    return error;
}
int rtl8188cu_boot_firmware(const struct rtl_fw_bus *fb, const rtl_u8 *data,
                          unsigned int length, struct rtl_fw_report *report)
{
    const struct rtl_bus *b;
    struct rtl_fw_image image;
    rtl_u32 value;
    unsigned int offset, page, within, chunk, remaining;
    int error;
    if (!fb || !report || !fb->registers.read || !fb->registers.write ||
        !fb->registers.delay_us || !fb->write_block) return RTL_BAD_ARGUMENT;
    b = &fb->registers;
    report->stage = RTL_FW_VALIDATE; report->cleanup_error = RTL_OK;
    report->bytes_written = report->pages_written = 0;
    report->ready_register = report->version = report->subversion = 0;
    error = rtl8188cu_validate_firmware(data, length, &image);
    if (error) return error; /* Invalid images never touch hardware. */
    report->version = image.version; report->subversion = image.subversion;
    TRY(readreg(b, 0xf0, 4, &value));
    if (!rtl8188cu_firmware_name(value)) return RTL_UNSUPPORTED;
    report->stage = RTL_FW_AUTOLOAD;
    TRY(poll(b, 0x04, 1, 2, 2, 5, 1000));
    report->stage = RTL_FW_POWER;
    TRY(writereg(b, 0x1c, 1, 0));
    TRY(writereg(b, 0x11, 1, 0x2b));
    b->delay_us(b->cookie, 100);
    TRY(readreg(b, 0x21, 1, &value));
    if (!(value & 1)) {
        TRY(writereg(b, 0x21, 1, value | 1));
        b->delay_us(b->cookie, 100);
        TRY(modify(b, 0x00, 1, 1, 0));
    }
    report->stage = RTL_FW_MAC_ON;
    TRY(modify(b, 0x04, 2, 0, 0x100));
    TRY(poll(b, 0x04, 2, 0x100, 0, 5, 1000));
    TRY(writereg(b, 0x04, 2, 0x0812));
    TRY(modify(b, 0x00, 2, 0x0200, 0));
    /* Deviates from full OpenBSD power_on: no TX/RX or RF yet. */
    TRY(writereg(b, 0x522, 1, 0xff));
    TRY(writereg(b, 0x100, 2, 0));
    TRY(writereg(b, 0x01f, 1, 0));
    TRY(writereg(b, 0x120, 4, 0));
    TRY(writereg(b, 0x128, 4, 0));
    TRY(writereg(b, 0xfe10, 1, 0x19));
    report->stage = RTL_FW_RESET;
    TRY(readreg(b, MCUFWDL, 1, &value));
    if (value & FW_RAM) {
        TRY(writereg(b, MCUFWDL, 1, 0));
        TRY(writereg(b, 0x1cf, 1, 0x20));
        error = poll(b, SYS_FUNC, 2, CPU_EN, 0, 50, 100);
        if (error != RTL_OK && error != RTL_TIMEOUT) goto fail;
        if (error == RTL_TIMEOUT) TRY(modify(b, SYS_FUNC, 2, CPU_EN, 0));
    }
    TRY(modify(b, MCUFWDL, 1, 0, FW_EN));
    TRY(modify(b, MCUFWDL, 4, 0x00080000U, 0));
    TRY(modify(b, MCUFWDL, 1, 0, FW_CHECKSUM));
    b->delay_us(b->cookie, 50);
    report->stage = RTL_FW_TRANSFER;
    offset = 0;
    for (page = 0; offset < image.size; ++page) {
        TRY(modify(b, MCUFWDL, 4, 0x00070000U, (rtl_u32)page << 16));
        remaining = image.size - offset;
        if (remaining > 4096) remaining = 4096;
        within = 0;
        while (remaining) {
            /* RTL8192CU USB block sizes, including the 4/1-byte tail. */
            chunk = remaining > 196 ? 196 : (remaining > 4 ? 4 : 1);
            if (fb->write_block(b->cookie, (rtl_u16)(0x1000 + within), image.payload + offset, chunk)) {
                error = RTL_IO; goto fail;
            }
            remaining -= chunk; within += chunk; offset += chunk;
            report->bytes_written += chunk;
        }
        ++report->pages_written;
    }
    report->stage = RTL_FW_CHECKSUM;
    TRY(poll(b, MCUFWDL, 4, FW_CHECKSUM, FW_CHECKSUM, 5, 1000));
    TRY(modify(b, MCUFWDL, 1, FW_EN, 0));
    TRY(writereg(b, MCUFWDL + 1, 1, 0));
    report->stage = RTL_FW_START;
    TRY(modify(b, MCUFWDL, 4, FW_INIT, FW_RDY));
    TRY(readreg(b, SYS_FUNC, 2, &value));
    TRY(writereg(b, SYS_FUNC, 2, value & ~CPU_EN));
    TRY(writereg(b, SYS_FUNC, 2, value | CPU_EN));
    TRY(poll(b, MCUFWDL, 4, FW_INIT, FW_INIT, 10, 1000));
    TRY(readreg(b, MCUFWDL, 4, &report->ready_register));
    /* Acknowledge only firmware readiness; this is not an association. */
    if (!(report->ready_register & FW_INIT)) { error = RTL_IO; goto fail; }
    report->stage = RTL_FW_READY;
    return RTL_OK;
fail:
    report->cleanup_error = rtl8188cu_quiesce(b);
    return error;
}
