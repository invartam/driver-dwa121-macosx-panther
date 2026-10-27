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

/* Panther port: see NOTICE. No OTP programming operation is implemented. */
#include "identity.h"
#define SYS_CFG 0x00f0
#define EFUSE_CTRL 0x0030
#define EFUSE_VALID 0x80000000U
#define EFUSE_ADDR_MASK 0x0003ff00U
#define CHIP_TEST 0x00800000U
#define CHIP_92C 0x08000000U
#define CHIP_UMC 0x00080000U

static int read_efuse(const struct rtl_bus *bus, unsigned int address, rtl_u8 *byte)
{
    rtl_u32 value;
    unsigned int attempt;
    if (address >= 512) return RTL_BAD_ROM;
    if (bus->read(bus->cookie, EFUSE_CTRL, 4, &value)) return RTL_IO;
    value = (value & ~(EFUSE_ADDR_MASK | EFUSE_VALID)) | ((rtl_u32)address << 8);
    /* Clearing bit 31 requests a READ. Never set bit 31 to initiate a write. */
    if (bus->write(bus->cookie, EFUSE_CTRL, 4, value)) return RTL_IO;
    for (attempt = 0; attempt < 100; ++attempt) {
        if (bus->read(bus->cookie, EFUSE_CTRL, 4, &value)) return RTL_IO;
        if (value & EFUSE_VALID) {
            *byte = (rtl_u8)value;
            return RTL_OK;
        }
        bus->delay_us(bus->cookie, 5);
    }
    return RTL_TIMEOUT;
}

static int read_rom(const struct rtl_bus *bus, struct rtl_identity *id)
{
    unsigned int address = 0, i, offset;
    rtl_u8 header;
    int error;
    for (i = 0; i < sizeof(id->rom); ++i) id->rom[i] = 0xff;
    /* 88C uses ordinary 4-bit block headers, not 88E extended headers. */
    while (address < 512) {
        error = read_efuse(bus, address++, &header);
        if (error) return error;
        if (header == 0xff) {
            id->physical_bytes = (rtl_u16)address;
            return read_efuse(bus, 0x1fa, &id->pa_setting);
        }
        offset = (header >> 4) * 8;
        for (i = 0; i < 4; ++i) {
            if (header & (1 << i)) continue;
            if (address + 2 > 512 || offset + 2 * i + 1 >= sizeof(id->rom))
                return RTL_BAD_ROM;
            error = read_efuse(bus, address++, &id->rom[offset + 2 * i]);
            if (error) return error;
            error = read_efuse(bus, address++, &id->rom[offset + 2 * i + 1]);
            if (error) return error;
        }
    }
    return RTL_BAD_ROM; /* No end marker within the physical eFuse. */
}

static rtl_u16 get16(const rtl_u8 *p)
{
    return (rtl_u16)(p[0] | ((rtl_u16)p[1] << 8));
}

int rtl8188cu_parse_rom(struct rtl_identity *id)
{
    unsigned int i, any = 0;
    if (!id) return RTL_BAD_ARGUMENT;
    if (get16(id->rom) != 0x8129) return RTL_BAD_ROM;
    for (i = 0; i < 6; ++i) {
        id->mac[i] = id->rom[0x16 + i];
        any |= id->mac[i];
    }
    if (!any || (id->mac[0] & 1)) return RTL_BAD_ROM;
    id->vid = get16(id->rom + 0x0a);
    id->pid = get16(id->rom + 0x0c);
    id->channel_plan = id->rom[0x75];
    id->thermal_meter = id->rom[0x78];
    id->regulatory = id->rom[0x79] & 7;
    id->board_type = id->rom[0x79] >> 5;
    /* Raw calibration bytes remain in rom; no guessed RF defaults here. */
    return RTL_OK;
}

const char *rtl8188cu_firmware_name(rtl_u32 sys_cfg)
{
    if (sys_cfg == 0 || sys_cfg == 0xffffffffU || (sys_cfg & (CHIP_TEST | CHIP_92C)))
        return 0;
    if ((sys_cfg & CHIP_UMC) && !(sys_cfg & 0x0000f000U))
        return "urtwn-rtl8192cU";
    return "urtwn-rtl8192cT";
}

int rtl8188cu_read_identity(const struct rtl_bus *bus, struct rtl_identity *id)
{
    static const rtl_u16 regs[3] = {0x0000, 0x0002, 0x0008};
    static const rtl_u16 enables[3] = {0x8000, 0x1000, 0x0022};
    rtl_u32 saved[3], value;
    unsigned int i, touched = 0;
    int error = RTL_OK, cleanup = RTL_OK;
    if (!bus || !id || !bus->read || !bus->write || !bus->delay_us)
        return RTL_BAD_ARGUMENT;
    id->stage = RTL_STAGE_CHIP;
    id->restore_error = 0;
    id->physical_bytes = 0;
    if (bus->read(bus->cookie, SYS_CFG, 4, &id->sys_cfg)) return RTL_IO;
    if (!rtl8188cu_firmware_name(id->sys_cfg)) return RTL_UNSUPPORTED;
    id->stage = RTL_STAGE_POWER;
    /* Read every snapshot before changing any register. */
    for (i = 0; i < 3; ++i)
        if (bus->read(bus->cookie, regs[i], 2, &saved[i])) return RTL_IO;
    for (i = 0; i < 3; ++i) {
        if ((saved[i] & enables[i]) == enables[i]) continue;
        /* Even a failing transfer may have reached hardware: restore it too. */
        touched |= 1U << i;
        if (bus->write(bus->cookie, regs[i], 2, saved[i] | enables[i])) {
            error = RTL_IO;
            goto restore;
        }
    }
    id->stage = RTL_STAGE_EFUSE;
    error = read_rom(bus, id);
restore:
    /* Reverse order; attempt every restoration even if one fails. */
    for (i = 3; i > 0; --i) {
        if (!(touched & (1U << (i - 1)))) continue;
        if (bus->write(bus->cookie, regs[i - 1], 2, saved[i - 1]))
            cleanup = RTL_RESTORE;
    }
    for (i = 0; i < 3; ++i) {
        if (bus->read(bus->cookie, regs[i], 2, &value) || value != saved[i])
            cleanup = RTL_RESTORE;
    }
    id->restore_error = cleanup;
    if (cleanup) {
        if (!error) id->stage = RTL_STAGE_RESTORE;
        return cleanup;
    }
    if (error) return error;
    id->stage = RTL_STAGE_VALIDATE;
    error = rtl8188cu_parse_rom(id);
    if (!error) id->stage = RTL_STAGE_DONE;
    return error;
}
