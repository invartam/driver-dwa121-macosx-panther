#ifndef RTL8188CU_RADIO_H
#define RTL8188CU_RADIO_H
#include "identity.h"
#ifdef __cplusplus
extern "C" {
#endif
enum rtl_radio_stage {
    RTL_RADIO_VALIDATE = 1, RTL_RADIO_MAC, RTL_RADIO_BB, RTL_RADIO_RF,
    RTL_RADIO_IQ, RTL_RADIO_LC, RTL_RADIO_PA, RTL_RADIO_VERIFY,
    RTL_RADIO_DONE
};
#define RTL_CALIBRATION_FAILED (-7)
struct rtl_iq_sample { rtl_u16 tx[2], rx[2]; int valid; };
struct rtl_radio_report {
    int stage, cleanup_error, calibration_restore_error;
    unsigned int mac_writes, bb_writes, agc_writes, rf_writes, iq_runs;
    struct rtl_iq_sample iq, last_iq;
    rtl_u32 rf_channel_before, rf_channel_after, bb_rfmod;
    int lc_verified;
};
/* TSMC 8188CU, board 0, 1T1R only. Firmware must already be running.
 * Caller owns/serializes USB for the entire call and calls radio_quiesce
 * before releasing it. MAC TX/RX and all host queues remain disabled.
 * This performs internal RF calibrations, not scanning or association.
 * Failure attempts to restore calibration state, then quiesces hardware.
 */
int rtl8188cu_radio_init(const struct rtl_bus *, const struct rtl_identity *,
                        struct rtl_radio_report *);
/* Passive receive only, channels 1..11, MAC/DMA must be stopped. */
int rtl8188cu_set_rx_channel(const struct rtl_bus *, unsigned int, rtl_u32 *);
int rtl8188cu_radio_quiesce(const struct rtl_bus *);
/* Signed 10-bit comparison, requiring every TX/RX component within tolerance. */
int rtl8188cu_iq_agrees(const struct rtl_iq_sample *, const struct rtl_iq_sample *);
#ifdef __cplusplus
}
#endif
#endif
