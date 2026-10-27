#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../src/rtl8188cu/identity.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
struct mock {
    rtl_u8 physical[512], expected[128];
    rtl_u32 regs[3], original[3], cfg, control;
    unsigned int calls, fail_at, delays, polls, requests, max_address;
    int timeout, apply_failed_write;
};
static int reg_index(rtl_u16 address)
{
    if (address == 0) return 0;
    if (address == 2) return 1;
    if (address == 8) return 2;
    return -1;
}
static int read_mock(void *cookie, rtl_u16 address, unsigned int width, rtl_u32 *value)
{
    struct mock *m = cookie;
    int index = reg_index(address);
    if (++m->calls == m->fail_at) return -1;
    if (index >= 0) { CHECK(width == 2); *value = m->regs[index]; }
    else if (address == 0xf0) { CHECK(width == 4); *value = m->cfg; }
    else {
        unsigned int offset = (m->control >> 8) & 0x3ff;
        CHECK(address == 0x30 && width == 4 && offset < 512);
        ++m->polls;
        *value = (m->control & ~0xffU) | m->physical[offset];
        if (!m->timeout) *value |= 0x80000000U;
    }
    return 0;
}
static int write_mock(void *cookie, rtl_u16 address, unsigned int width, rtl_u32 value)
{
    struct mock *m = cookie;
    int index = reg_index(address), fail = (++m->calls == m->fail_at);
    if (fail && !m->apply_failed_write) return -1;
    if (index >= 0) { CHECK(width == 2 && value <= 0xffff); m->regs[index] = value; }
    else {
        unsigned int offset = (value >> 8) & 0x3ff;
        CHECK(address == 0x30 && width == 4);
        CHECK(!(value & 0x80000000U)); /* Never initiate OTP programming. */
        CHECK(offset < 512);
        if (offset > m->max_address) m->max_address = offset;
        ++m->requests;
        m->control = value;
    }
    return fail ? -1 : 0;
}
static void delay_mock(void *cookie, unsigned int us)
{
    struct mock *m = cookie;
    CHECK(us == 5); ++m->delays;
}
static void setup(struct mock *m)
{
    unsigned int block, address = 0, i;
    memset(m, 0, sizeof(*m));
    memset(m->physical, 0xff, sizeof(m->physical));
    for (i = 0; i < 128; ++i) m->expected[i] = (rtl_u8)i;
    m->expected[0] = 0x29; m->expected[1] = 0x81;
    m->expected[0xa] = 1; m->expected[0xb] = 0x20;
    m->expected[0xc] = 8; m->expected[0xd] = 0x33;
    m->expected[0x16] = 2;
    for (block = 0; block < 16; ++block) {
        m->physical[address++] = (rtl_u8)(block << 4);
        for (i = 0; i < 8; ++i) m->physical[address++] = m->expected[block * 8 + i];
    }
    /* A later record updates word 0 of block 11; inactive words are preserved. */
    m->physical[address++] = 0xbe;
    m->physical[address++] = m->expected[88] = 0xab;
    m->physical[address++] = m->expected[89] = 0xcd;
    m->physical[0x1fa] = 0x5a;
    m->regs[0] = m->original[0] = 0x1234;
    m->regs[1] = m->original[1] = 0x0040;
    m->regs[2] = m->original[2] = 0x0100;
    m->cfg = 0x04400735U;
}
static int run(struct mock *m, struct rtl_identity *id)
{
    struct rtl_bus bus;
    memset(id, 0, sizeof(*id));
    bus.cookie = m; bus.read = read_mock; bus.write = write_mock; bus.delay_us = delay_mock;
    return rtl8188cu_read_identity(&bus, id);
}
static void restored(struct mock *m) { CHECK(memcmp(m->regs, m->original, sizeof(m->regs)) == 0); }
int main(void)
{
    struct mock m;
    struct rtl_identity id;
    unsigned int calls, n, mode;
    int result;
    setup(&m); CHECK(run(&m, &id) == RTL_OK); restored(&m);
    CHECK(id.stage == RTL_STAGE_DONE && id.restore_error == 0);
    CHECK(id.physical_bytes == 148 && id.pa_setting == 0x5a);
    CHECK(id.vid == 0x2001 && id.pid == 0x3308);
    CHECK(memcmp(id.rom, m.expected, 128) == 0);
    CHECK(memcmp(id.mac, m.expected + 0x16, 6) == 0);
    CHECK(id.channel_plan == 0x75 && id.board_type == 3 && id.regulatory == 1);
    CHECK(m.max_address == 0x1fa);
    calls = m.calls;
    /* Every transaction can fail, including ambiguous partially applied writes. */
    for (mode = 0; mode < 2; ++mode) for (n = 1; n <= calls; ++n) {
        setup(&m); m.fail_at = n; m.apply_failed_write = mode;
        result = run(&m, &id);
        CHECK(result != RTL_OK);
        if (result != RTL_RESTORE) restored(&m);
    }
    /* Clocks already enabled: no power-register change is necessary. */
    setup(&m);
    m.original[0] = m.regs[0] |= 0x8000;
    m.original[1] = m.regs[1] |= 0x1000;
    m.original[2] = m.regs[2] |= 0x22;
    CHECK(run(&m, &id) == RTL_OK); restored(&m);
    /* Sparse image: omitted words keep 0xff, not zero or stale data. */
    setup(&m); memset(m.physical, 0xff, 512);
    m.physical[0] = 0x0e; m.physical[1] = 0x29; m.physical[2] = 0x81;
    m.physical[3] = 0x27; m.physical[4] = 0x02; m.physical[5] = 0x11;
    m.physical[6] = 0x3c; m.physical[7] = 0x22; m.physical[8] = 0x33;
    m.physical[9] = 0x44; m.physical[10] = 0x55;
    CHECK(run(&m, &id) == RTL_OK && id.physical_bytes == 12);
    CHECK(id.rom[0x5a] == 0xff && id.mac[5] == 0x55); restored(&m);
    setup(&m); m.timeout = 1;
    CHECK(run(&m, &id) == RTL_TIMEOUT); CHECK(m.delays == 100); restored(&m);
    setup(&m); memset(m.physical, 0x0f, 512);
    CHECK(run(&m, &id) == RTL_BAD_ROM); restored(&m);
    setup(&m); memset(m.physical, 0x0f, 512); m.physical[511] = 0x0e;
    CHECK(run(&m, &id) == RTL_BAD_ROM); restored(&m);
    setup(&m); m.physical[0] = 0xff;
    CHECK(run(&m, &id) == RTL_BAD_ROM); restored(&m);
    setup(&m); m.cfg |= 0x08000000U;
    CHECK(run(&m, &id) == RTL_UNSUPPORTED && m.calls == 1 && m.requests == 0);
    setup(&m); CHECK(run(&m, &id) == RTL_OK);
    id.rom[0] = 0; CHECK(rtl8188cu_parse_rom(&id) == RTL_BAD_ROM);
    id.rom[0] = 0x29; memset(id.rom + 0x16, 0, 6);
    CHECK(rtl8188cu_parse_rom(&id) == RTL_BAD_ROM);
    id.rom[0x16] = 1; CHECK(rtl8188cu_parse_rom(&id) == RTL_BAD_ROM);
    CHECK(strcmp(rtl8188cu_firmware_name(0x04400735U), "urtwn-rtl8192cT") == 0);
    CHECK(strcmp(rtl8188cu_firmware_name(0x04480735U), "urtwn-rtl8192cU") == 0);
    CHECK(strcmp(rtl8188cu_firmware_name(0x04481735U), "urtwn-rtl8192cT") == 0);
    CHECK(rtl8188cu_firmware_name(0) == 0);
    CHECK(rtl8188cu_firmware_name(0xffffffffU) == 0);
    CHECK(rtl8188cu_read_identity(0, &id) == RTL_BAD_ARGUMENT);
    printf("PASS: eFuse decoding, ROM/MAC validation, firmware selection, timeout, bounds, rollback; %u injected transaction faults\n", 2 * calls);
    return 0;
}
