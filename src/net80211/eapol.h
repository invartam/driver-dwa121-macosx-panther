#ifndef DWA121_EAPOL_H
#define DWA121_EAPOL_H
#include "../rtl8188cu/identity.h"
struct rtl_eapol_view {
    const rtl_u8 *data; /* borrowed, valid only while the input frame lives */
    unsigned int length,key_info,key_data_length,is_m1;
};
#ifdef __cplusplus
extern "C" {
#endif
/* Unencrypted FromDS RSN EAPOL-Key, with or without a QoS header. Caller
 * must already have verified FCS and freshness. No allocation or key logging.
 * is_m1 describes syntax only: M1 has no MIC and does not authenticate the AP.
 * Output is cleared on failure. Protected/group data and A-MSDU are rejected. */
int rtl_eapol_from_80211(const rtl_u8 *,unsigned int,const rtl_u8 mac[6],
                       const rtl_u8 bssid[6],struct rtl_eapol_view *);
#ifdef __cplusplus
}
#endif
#endif
