#ifndef DWA121_BEACON_H
#define DWA121_BEACON_H
#include "../rtl8188cu/identity.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RTL_BSS_MAX 16U
#define RTL_RSN_ABSENT 0U
#define RTL_RSN_VALID 1U
#define RTL_RSN_MALFORMED 2U
#define RTL_RSN_UNSUPPORTED 3U
struct rtl_rsn {
    unsigned int status, group_ccmp, pairwise_ccmp, psk;
    unsigned int capabilities;
};
struct rtl_bss {
    rtl_u8 bssid[6], ssid[32], rsn_ie[257], wpa_ie[257];
    unsigned int ssid_length, hidden, channel, received_channel;
    unsigned int beacon_interval, capabilities, observations, rsn_length;
    unsigned int wpa_status,wpa_length; /* same status values as RSN */
    rtl_u32 last_seen_ms; /* monotonic, supplied by the RX owner */
    struct rtl_rsn rsn;
};
struct rtl_bss_table {
    struct rtl_bss entries[RTL_BSS_MAX];
    unsigned int count, accepted, rejected, dropped;
};
/* Original C89 parser. IE includes ID and length; all reads are bounded.
 * RSN defaults are CCMP / CCMP / 802.1X; PSK must be explicitly advertised.
 * Unknown selectors are tolerated but never counted as CCMP or PSK.
 */
unsigned int rtl_wpa_status(const rtl_u8 *, unsigned int);
const char *rtl_bss_security(const struct rtl_bss *);
int rtl_rsn_parse(const rtl_u8 *, unsigned int, struct rtl_rsn *);
/* Input is a complete beacon WITHOUT FCS, already integrity-checked by RX.
 * Snapshot table, not an authenticated association/candidate-selection cache.
 * Full table drops new BSSIDs; existing BSSIDs are updated atomically. */
int rtl_bss_observe(struct rtl_bss_table *, const rtl_u8 *, unsigned int,
                    unsigned int received_channel);
int rtl_bss_observe_at(struct rtl_bss_table *, const rtl_u8 *, unsigned int,
                       unsigned int, rtl_u32);
int rtl_bss_advertises_psk_ccmp(const struct rtl_bss *);
#ifdef __cplusplus
}
#endif
#endif
