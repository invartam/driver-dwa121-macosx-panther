#ifndef DWA121_RTL_WIRE_H
#define DWA121_RTL_WIRE_H
/* Byte-wise access: USB/chip registers are little endian; PowerPC is big endian. */
static unsigned long rtl_get_le32(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}
#define DWA121_VENDOR 0x2001
#define DWA121_PRODUCT 0x3308
#define RTL_REQ_REGISTERS 0x05
#define RTL_SYS_CFG 0x00f0
#define RTL_SYS_CFG_TEST_CHIP 0x00800000UL
#define RTL_SYS_CFG_TYPE_92C 0x08000000UL
#define RTL_SYS_CFG_VENDOR_UMC 0x00080000UL
#endif
