#ifndef RTL8188CU_STATION_HW_H
#define RTL8188CU_STATION_HW_H
#include "identity.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Experimental 20 MHz legacy management TX, on the already tuned/approved
 * channel. Caller has exclusive ownership, no queued OUT, RX monitor active.
 * Only management queue opens. No data/security/automatic beacon generation.
 * Caller must quiesce/reset after ANY attempted begin, including failure;
 * this is not a rollback to a reusable RX session. */
int rtl8188cu_station_hw_begin(const struct rtl_bus *,const struct rtl_identity *,
                             const rtl_u8 bssid[6],unsigned int channel);
/* Best effort all queues paused and both TX DMA bits off, retaining RX.
 * Final owner shutdown must also drain USB and quiesce radio/firmware. */
int rtl8188cu_station_hw_block(const struct rtl_bus *);
/* Before authentication, in RX-only mode: erase the CAM while the AP has no
 * running handshake deadline. This can take seconds over Panther USB. */
int rtl8188cu_station_hw_wpa_prepare(const struct rtl_bus *);
/* Initial WPA2 probe only: set station mode, open MGNT+BE.
 * CAM must already have been cleared by wpa_prepare before authentication.
 * Caller has consumed all USB TX results. Security stays disabled and the
 * software policy must permit only EAPOL on BE. Never opens a network port. */
int rtl8188cu_station_hw_eapol(const struct rtl_bus *);
int rtl8188cu_station_hw_key(const struct rtl_bus *,int,unsigned int,
                            const rtl_u8 bssid[6],const rtl_u8 key[16]);
/* Erase all five session slots, attempt every slot, retain paused queues. */
int rtl8188cu_station_hw_erase(const struct rtl_bus *);
#ifdef __cplusplus
}
#endif
#endif
