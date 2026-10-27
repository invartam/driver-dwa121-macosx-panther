#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/rtl8188cu/firmware.h"
#include "../src/firmware/images.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
struct mock {
    rtl_u8 regs[65536], written[32768];
    const rtl_u8 *expected;
    unsigned int size, received, calls, fail_at, blocks, delays;
    int fail_after_write, autoload_timeout, mac_timeout, checksum_timeout, ready_timeout;
    int reset_timeout, ready_seen;
};
static rtl_u32 get(struct mock *m, unsigned int address, unsigned int width)
{
    rtl_u32 value = 0; unsigned int i;
    CHECK(address + width <= sizeof(m->regs));
    for (i = 0; i < width; ++i) value |= (rtl_u32)m->regs[address+i] << (8*i);
    return value;
}
static void put(struct mock *m, unsigned int address, unsigned int width, rtl_u32 value)
{
    unsigned int i;
    CHECK(address + width <= sizeof(m->regs));
    for (i = 0; i < width; ++i) m->regs[address+i] = (rtl_u8)(value >> (8*i));
}
static int read_mock(void *cookie, rtl_u16 address, unsigned int width, rtl_u32 *value)
{
    struct mock *m = cookie;
    CHECK(width == 1 || width == 2 || width == 4);
    if (++m->calls == m->fail_at) return -1;
    if (address == 4) {
        if (m->autoload_timeout) m->regs[4] &= ~2;
        if (!m->mac_timeout) m->regs[5] &= ~1;
    }
    if (address == 0x80 && m->received == m->size && m->size && !m->checksum_timeout)
        m->regs[0x80] |= 4;
    *value = get(m, address, width);
    return 0;
}
static int write_mock(void *cookie, rtl_u16 address, unsigned int width, rtl_u32 value)
{
    struct mock *m = cookie;
    int failure = (++m->calls == m->fail_at);
    rtl_u32 previous = get(m, address, width);
    CHECK(width == 1 || width == 2 || width == 4);
    if (address == 0x100) CHECK(!(value & 0xc0)); /* No MAC TX/RX. */
    if (address == 0x1f) CHECK(value == 0);       /* RF remains disabled. */
    if (failure && !m->fail_after_write) return -1;
    put(m, address, width, value);
    if (address == 0x80 && (value & 4) && (value & 1) && m->received < m->size)
        m->regs[0x80] &= ~4; /* write-one-to-clear download checksum */
    if (address == 0x1cf && value == 0x20 && !m->reset_timeout)
        m->regs[3] &= ~4;
    if (address == 2 && (value & 0x400) && !(previous & 0x400) &&
        (m->regs[0x80] & 2) && m->received == m->size && !m->ready_timeout) {
        CHECK(memcmp(m->expected, m->written, m->size) == 0);
        m->regs[0x80] |= 0xc0;
        m->ready_seen = 1;
    }
    return failure ? -1 : 0;
}
static int block_mock(void *cookie, rtl_u16 address, const rtl_u8 *bytes, unsigned int size)
{
    struct mock *m = cookie;
    unsigned int page = (get(m, 0x80, 4) >> 16) & 7;
    unsigned int offset = page * 4096 + address - 0x1000;
    int failure = (++m->calls == m->fail_at);
    CHECK(m->regs[0x80] & 1);
    CHECK(address >= 0x1000 && address + size <= 0x2000);
    CHECK(size == 196 || size == 4 || size == 1);
    CHECK(offset == m->received && offset + size <= m->size);
    CHECK(memcmp(bytes, m->expected + offset, size) == 0);
    if (failure && !m->fail_after_write) return -1;
    memcpy(m->written + offset, bytes, size);
    m->received += size; ++m->blocks;
    return failure ? -1 : 0;
}
static void delay_mock(void *cookie, unsigned int us)
{
    struct mock *m = cookie; CHECK(us <= 100); ++m->delays;
}
static void setup(struct mock *m, const rtl_u8 *firmware, unsigned int length, struct rtl_fw_bus *bus)
{
    memset(m, 0, sizeof(*m));
    m->expected = firmware + 32; m->size = length - 32;
    m->regs[4] = 2;
    put(m, 0xf0, 4, 0x04400735U);
    bus->registers.cookie = m; bus->registers.read = read_mock;
    bus->registers.write = write_mock; bus->registers.delay_us = delay_mock;
    bus->write_block = block_mock;
}
static void stopped(struct mock *m)
{
    CHECK(!(get(m, 2, 2) & 0x400));
    CHECK(!(m->regs[0x80] & 3));
    CHECK(!get(m, 0x100, 2) && !m->regs[0x1f]);
    CHECK(m->regs[0x522] == 0xff);
}
int main(void)
{
    struct mock m;
    struct rtl_fw_bus bus;
    struct rtl_fw_report report;
    struct rtl_fw_image image;
    rtl_u8 bad[33], *synthetic;
    unsigned int calls, fault, mode, lengths[] = {1,4,196,197,4096,4097,8193,32768};
    unsigned int i, j;
    int result;
    CHECK(rtl8188cu_validate_firmware(rtl8192cT, sizeof(rtl8192cT), &image) == RTL_OK);
    CHECK(image.size == 16084 && image.version == 80 && image.subversion == 0);
    CHECK(rtl8188cu_validate_firmware(rtl8192cU, sizeof(rtl8192cU), &image) == RTL_OK);
    setup(&m, rtl8192cT, sizeof(rtl8192cT), &bus);
    CHECK(rtl8188cu_boot_firmware(&bus, rtl8192cT, sizeof(rtl8192cT), &report) == RTL_OK);
    CHECK(report.stage == RTL_FW_READY && report.cleanup_error == 0);
    CHECK(report.bytes_written == 16084 && report.pages_written == 4 && m.ready_seen);
    CHECK(report.ready_register & 0x40);
    calls = m.calls;
    CHECK(rtl8188cu_quiesce(&bus.registers) == RTL_OK); stopped(&m);
    for (mode = 0; mode < 2; ++mode) for (fault = 1; fault <= calls; ++fault) {
        setup(&m, rtl8192cT, sizeof(rtl8192cT), &bus);
        m.fail_at = fault; m.fail_after_write = mode;
        result = rtl8188cu_boot_firmware(&bus, rtl8192cT, sizeof(rtl8192cT), &report);
        CHECK(result != RTL_OK && report.cleanup_error == RTL_OK);
        stopped(&m);
    }
    /* Explicitly fail each cleanup transaction. */
    for (fault = 1; fault <= 12; ++fault) {
        setup(&m, rtl8192cT, sizeof(rtl8192cT), &bus);
        put(&m, 2, 2, 0x400); m.regs[0x80] = 3;
        m.fail_at = fault;
        CHECK(rtl8188cu_quiesce(&bus.registers) != RTL_OK);
    }
    for (mode = 0; mode < 4; ++mode) {
        setup(&m, rtl8192cT, sizeof(rtl8192cT), &bus);
        m.autoload_timeout = mode == 0; m.mac_timeout = mode == 1;
        m.checksum_timeout = mode == 2; m.ready_timeout = mode == 3;
        CHECK(rtl8188cu_boot_firmware(&bus, rtl8192cT, sizeof(rtl8192cT), &report) == RTL_TIMEOUT);
        CHECK(report.stage == (mode == 0 ? RTL_FW_AUTOLOAD : mode == 1 ? RTL_FW_MAC_ON : mode == 2 ? RTL_FW_CHECKSUM : RTL_FW_START));
        stopped(&m);
    }
    /* Warm reload, including the forced 8051 reset fallback. */
    for (mode = 0; mode < 2; ++mode) {
        setup(&m, rtl8192cT, sizeof(rtl8192cT), &bus);
        m.regs[0x80] = 0x80; put(&m, 2, 2, 0x400); m.reset_timeout = mode;
        CHECK(rtl8188cu_boot_firmware(&bus, rtl8192cT, sizeof(rtl8192cT), &report) == RTL_OK);
    }
    memcpy(bad, rtl8192cT, sizeof(bad));
    setup(&m, rtl8192cT, sizeof(rtl8192cT), &bus);
    CHECK(rtl8188cu_boot_firmware(&bus, bad, 32, &report) != RTL_OK && m.calls == 0);
    CHECK(rtl8188cu_boot_firmware(&bus, bad, 33, &report) != RTL_OK && m.calls == 0);
    bad[0] = 0; bad[1] = 0;
    CHECK(rtl8188cu_boot_firmware(&bus, bad, 33, &report) == RTL_UNSUPPORTED && m.calls == 0);
    synthetic = malloc(32 + 32768); CHECK(synthetic != 0);
    memcpy(synthetic, rtl8192cT, 32);
    for (i = 0; i < 32768; ++i) synthetic[32 + i] = (rtl_u8)(i ^ (i >> 8));
    for (j = 0; j < sizeof(lengths)/sizeof(lengths[0]); ++j) {
        unsigned int length = lengths[j];
        synthetic[12] = (rtl_u8)length; synthetic[13] = (rtl_u8)(length >> 8);
        setup(&m, synthetic, length + 32, &bus);
        CHECK(rtl8188cu_boot_firmware(&bus, synthetic, length + 32, &report) == RTL_OK);
        CHECK(report.bytes_written == length && report.pages_written == (length + 4095)/4096);
    }
    free(synthetic);
    printf("PASS: firmware header, exact payload/page boundaries, timeouts, warm reset, quiesce; %u transfer faults and 12 cleanup faults\n", calls * 2);
    return 0;
}
