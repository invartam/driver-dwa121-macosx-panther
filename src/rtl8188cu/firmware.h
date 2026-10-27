#ifndef RTL8188CU_FIRMWARE_H
#define RTL8188CU_FIRMWARE_H
#include "identity.h"
#ifdef __cplusplus
extern "C" {
#endif
struct rtl_fw_bus {
    struct rtl_bus registers;
    int (*write_block)(void *, rtl_u16, const rtl_u8 *, unsigned int);
};
struct rtl_fw_image { const rtl_u8 *payload; unsigned int size, version, subversion; };
enum rtl_fw_stage { RTL_FW_VALIDATE = 1, RTL_FW_AUTOLOAD, RTL_FW_POWER,
    RTL_FW_MAC_ON, RTL_FW_RESET, RTL_FW_TRANSFER, RTL_FW_CHECKSUM,
    RTL_FW_START, RTL_FW_READY };
struct rtl_fw_report {
    int stage, cleanup_error;
    unsigned int bytes_written, pages_written, version, subversion;
    rtl_u32 ready_register;
};
/* No bus access: reject wrong family/size/truncated header before power changes. */
int rtl8188cu_validate_firmware(const rtl_u8 *, unsigned int, struct rtl_fw_image *);
/* Powered MCU only: MAC TX/RX, RF and host data paths stay disabled.
 * Caller supplies the silicon-specific, hash-verified T or U image.
 * Caller serializes the bus and must quiesce before releasing the interface.
 */
int rtl8188cu_boot_firmware(const struct rtl_fw_bus *, const rtl_u8 *, unsigned int,
                          struct rtl_fw_report *);
/* Best effort: disable traffic, RF, interrupts, MCU and firmware download.
 * This is a quiescent powered state, not a restoration of pre-boot power rails.
 */
int rtl8188cu_quiesce(const struct rtl_bus *);
#ifdef __cplusplus
}
#endif
#endif
