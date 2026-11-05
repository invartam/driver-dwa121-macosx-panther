#ifndef DWA121_AIRPORT_JOIN_H
#define DWA121_AIRPORT_JOIN_H
#include "../net80211/scan.h"
#define DWA_JOIN_NATIVE_BYTES 56U
#define DWA_JOIN_NONE 0U
#define DWA_JOIN_SELECT 1U
#define DWA_JOIN_DISCONNECT 2U
struct dwa_join_selection {
    unsigned int operation,ssid_length,legacy_prompt; /* 2 = native 26/17 transport */
    rtl_u8 ssid[32];
};
#ifdef __cplusplus
extern "C" {
#endif
/* Only nonsecret selection metadata survives parsing. The legacy prompt is
 * NOT a WEP implementation and supplies no credentials to the WPA2 owner. */
int dwa_join_parse(const rtl_u8 *,unsigned int,struct dwa_join_selection *);
/* Prefer the configured channel, then lexicographic BSSID, never fake RSSI.
 * An exact SSID match is mandatory; credentials are checked independently. */
int dwa_join_choose(const struct rtl_bss_table *,const rtl_u8 *,unsigned int,
                    unsigned int,rtl_u32,struct rtl_bss *);
#ifdef __cplusplus
}
#endif
#endif
