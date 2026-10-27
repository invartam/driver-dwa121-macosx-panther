#ifndef RTL8188CU_TX_H
#define RTL8188CU_TX_H
#include "identity.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RTL_TX_FRAME_MAX 2080U
#define RTL_TX_USB_MAX (32U+RTL_TX_FRAME_MAX)
struct rtl_tx_packet {
    rtl_u8 bytes[RTL_TX_USB_MAX];
    unsigned int length, endpoint, needs_zlp;
};
/* DWA-121 two-OUT profile (02 high / 03 normal); 64 or 512 byte USB packets.
 * Accepts only unencrypted auth, association request, or LLC EAPOL to the
 * selected AP from our MAC. Not a general data-TX API/controlled-port bypass.
 * Requires the selected BSS to support CCK1 (rate 2 in 500 kb/s units).
 * Input and output buffers must not overlap.
 * FCS omitted. Sequence/retry bits are owned by software, HW sequencing OFF.
 * A full-size final USB packet MUST be followed by a ZLP by the backend.
 * Preparation only: does not submit anything or authorize radio TX. */
int rtl8188cu_tx_prepare(struct rtl_tx_packet *,const rtl_u8 *frame,unsigned int,
                        const rtl_u8 mac[6],const rtl_u8 bssid[6],unsigned int bulk_packet);
/* Separate caller-gated software-CCMP path. Hardware cipher bits stay zero;
 * complete CCMP header/ciphertext/MIC already present, FCS appended by radio. */
int rtl8188cu_tx_prepare_ccmp(struct rtl_tx_packet *,const rtl_u8 *,unsigned int,
    const rtl_u8 mac[6],const rtl_u8 bssid[6],unsigned int bulk_packet);
/* Build a three-address, non-QoS ToDS EAPOL frame. No encryption/fragmentation.
 * EAPOL key frames only, exact bounded length. Caller owns sequence modulo4096.
 * Group rekey messages needing link-layer protection require future CCMP TX. */
int rtl8188cu_eapol_frame(rtl_u8 *out,unsigned int capacity,unsigned int *length,
                        const rtl_u8 mac[6],const rtl_u8 bssid[6],unsigned int seq,
                        const rtl_u8 *eapol,unsigned int eapol_length);
#ifdef __cplusplus
}
#endif
#endif
