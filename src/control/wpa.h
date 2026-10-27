#ifndef DWA121_WPA_CONTROL_H
#define DWA121_WPA_CONTROL_H
#include "association.h"
#define DWA_WPA_VERSION 1U
#define DWA_WPA_ENTER 1U
#define DWA_WPA_SEND 2U
#define DWA_WPA_KEY 3U
#define DWA_WPA_VERIFIED 4U
#define DWA_WPA_DATA_OPEN 5U
#define DWA_WPA_DATA_SEND 6U
#define DWA_WPA_DATA_OFFER 7U
#define DWA_WPA_DATA_MAX_SEND 3U
#define DWA_WPA_MAX_MS 30000U
#define DWA_WPA_MAX_SEND 8U
/* Method 2, separate opt-in WPA2ProbeEnabled. Initial handshake only. Never
 * enables the network-interface data port, hardware crypto or GTK rekey. Reply
 * is dwa_assoc_reply; POLL/END remain method 1. All unused bytes must be zero.
 * ENTER: aid. SEND: length and complete ToDS EAPOL M2/M4. KEY: length=16,
 * pairwise/index, six-byte RSC (upper two bytes zero), data[0..15]=CCMP key.
 * VERIFIED is a trusted supplicant report, not kernel MIC verification.
 * DATA_OPEN/SEND/OFFER additionally require CCMPDataProbeEnabled, VERIFIED,
 * installed keys, and exclusive session ownership. SEND is a full software
 * encrypted non-QoS ToDS/PTK frame. OFFER is a trusted userland report. */
struct dwa_wpa_request {
    rtl_u32 version,operation,length,index,pairwise,aid;
    rtl_u8 rsc[8],data[2080];
};
struct dwa_data_budget { rtl_u32 sent; rtl_u8 pn[6]; };
#ifdef __cplusplus
extern "C" {
#endif
int dwa_wpa_validate(const struct dwa_wpa_request *);
/* Reserve an increasing nonzero PN before submitting to USB. No rollback.
 * Caller separately enforces opt-in, handshake, timer and USB idle state. */
int dwa_wpa_data_reserve(struct dwa_data_budget *,const struct dwa_wpa_request *,
    const rtl_u8 mac[6],const rtl_u8 ap[6]);
int dwa_wpa_data_reserve_limit(struct dwa_data_budget *,const struct dwa_wpa_request *,
    const rtl_u8 mac[6],const rtl_u8 ap[6],rtl_u32 limit);
#ifdef __cplusplus
}
#endif
#endif
