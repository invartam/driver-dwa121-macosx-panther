#ifndef RTL8188CU_CAM_H
#define RTL8188CU_CAM_H
#include "identity.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Serialized register I/O, radio awake, CR protocol/scheduler/MAC/security
 * bits 0x2f0 enabled, SECCFG=0 and TXPAUSE=ff required.
 * Caller must drain TX before replacing any key and keep its port closed.
 * No PN/RSC state lives here: the transport must enforce replay protection.
 * Group entries 0..3 use a zero MAC; pairwise entry 4 uses the AP MAC.
 * Group KeyID is the default-slot index, not the CTL0 KeyID field (reads 0).
 * The eventual RX configuration must enable SECCFG.RXBCKEY_DEF for groups.
 * RTL_RESTORE means erasure could not be verified: reset/quiesce the device.
 * No operation enables security or transmission. */
int rtl8188cu_cam_install(const struct rtl_bus *, int pairwise, unsigned int keyid,
                         const rtl_u8 mac[6], const rtl_u8 key[16]);
int rtl8188cu_cam_clear(const struct rtl_bus *, unsigned int entry);
/* Clear all 32 entries (six implemented words each, eight-word address stride).
 * Reserved words 6/7 are not RAM. Attempt every entry on error. */
int rtl8188cu_cam_clear_all(const struct rtl_bus *);
/* Error diagnostics deliberately contain no key bytes. step: 1 idle,
 * 2 write-data, 3 write-command, 4 write-wait, 5 read-command, 6 read-wait,
 * 7 read-data, 8 compare. first_word is the CAM word address, not a register. */
struct rtl_cam_report {
    unsigned int first_word, step, mismatches, word_mask;
    rtl_u32 entry_mask;
    rtl_u16 control_difference; /* flags/cipher/keyid only, no MAC/key bytes */
    int error;
};
int rtl8188cu_cam_clear_all_report(const struct rtl_bus *,struct rtl_cam_report *);
int rtl8188cu_cam_install_report(const struct rtl_bus *,int,unsigned int,
                                const rtl_u8 mac[6],const rtl_u8 key[16],struct rtl_cam_report *);
#ifdef __cplusplus
}
#endif
#endif
