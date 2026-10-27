#ifndef RTL8188CU_IDENTITY_H
#define RTL8188CU_IDENTITY_H
#ifdef __cplusplus
extern "C" {
#endif
/* Fixed sizes on both GCC 3.3/PowerPC and a modern test host. */
typedef unsigned char rtl_u8;
typedef unsigned short rtl_u16;
typedef unsigned int rtl_u32;
typedef char rtl_require_32_bits[(sizeof(rtl_u32) == 4) ? 1 : -1];
struct rtl_bus {
    void *cookie;
    int (*read)(void *, rtl_u16, unsigned int, rtl_u32 *);
    int (*write)(void *, rtl_u16, unsigned int, rtl_u32);
    void (*delay_us)(void *, unsigned int);
};
enum rtl_result {
    RTL_OK = 0, RTL_IO = -1, RTL_TIMEOUT = -2, RTL_BAD_ROM = -3,
    RTL_UNSUPPORTED = -4, RTL_RESTORE = -5, RTL_BAD_ARGUMENT = -6
};
enum rtl_stage { RTL_STAGE_CHIP = 1, RTL_STAGE_POWER, RTL_STAGE_EFUSE,
                 RTL_STAGE_RESTORE, RTL_STAGE_VALIDATE, RTL_STAGE_DONE };
struct rtl_identity {
    rtl_u32 sys_cfg;
    rtl_u8 rom[128];
    rtl_u8 mac[6];
    rtl_u16 vid, pid, physical_bytes;
    rtl_u8 pa_setting, channel_plan, board_type, regulatory, thermal_meter;
    int stage, restore_error;
};
/* Register writes only enable temporary read clocks/power and request reads.
 * Snapshot registers are restored on success and all recoverable failures.
 * Caller serializes I/O and holds the USB interface open throughout the call.
 * On error, identity fields are partial and must not be used to initialize RF.
 */
int rtl8188cu_read_identity(const struct rtl_bus *, struct rtl_identity *);
/* Raw logical-ROM validation, also useful for saved eFuse fixtures. */
int rtl8188cu_parse_rom(struct rtl_identity *);
/* Name used by OpenBSD firmware distribution; does not load a firmware. */
const char *rtl8188cu_firmware_name(rtl_u32 sys_cfg);
#ifdef __cplusplus
}
#endif
#endif
