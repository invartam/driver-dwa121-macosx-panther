#ifndef RTL8188CU_RX_H
#define RTL8188CU_RX_H
#include "identity.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RTL_RX_BUFFER_SIZE 16384U
#define RTL_RX_MALFORMED (-8)
struct rtl_rx_stats {
    unsigned int transfers, descriptors, accepted, beacons, crc_errors;
    unsigned int icv_errors, fcs_errors, filtered, malformed, bytes;
    unsigned int management, eapol, encrypted;
    unsigned int auth_seen, auth_to_us, auth_from_ap, auth_last_length;
};
struct rtl_dma_report {
    unsigned int llt_written, llt_verified;
    rtl_u32 control_at_rx;
    int cleanup_error;
};
/* Board-0 8188CU, exactly two bulk OUT endpoints. Call with MAC/DMA stopped.
 * Writes and verifies all 256 LLT links. Does not enable data paths.
 */
int rtl8188cu_dma_init(const struct rtl_bus *, unsigned int, struct rtl_dma_report *);
/* Management RX probe; parser retains only beacons. Select an RX-only channel.
 * Both MAC clocks are required for reception on the tested chip. TX DMA
 * remains off, all TX queues stay paused, and no OUT frame is submitted.
 */
int rtl8188cu_rx_enable(const struct rtl_bus *, struct rtl_dma_report *);
/* Extend active RX-only monitor to data frames for software EAPOL filtering.
 * TX DMA/queues and security remain disabled. Failure quiesces RX. */
int rtl8188cu_rx_control_enable(const struct rtl_bus *);
int rtl8188cu_rx_disable(const struct rtl_bus *);
/* Parses bounded USB aggregates with 24-byte LE descriptors, info/shift fields
 * and 128-byte alignment. APPFCS must be enabled. No frame data retained.
 * Counts only valid management beacons with hardware and software CRC checks.
 */
int rtl8188cu_rx_parse(const rtl_u8 *, unsigned int, struct rtl_rx_stats *);
/* Synchronous callback receives only verified beacons, without FCS. The input
 * is borrowed for the call only; a valid aggregate prefix can be delivered
 * before a later malformed record is detected. */
typedef void (*rtl_beacon_sink)(void *, const rtl_u8 *, unsigned int);
int rtl8188cu_rx_parse_beacons(const rtl_u8 *, unsigned int, struct rtl_rx_stats *,
                              rtl_beacon_sink, void *);
#define RTL_RX_BEACON 1U
#define RTL_RX_MANAGEMENT 2U
#define RTL_RX_EAPOL 3U
#define RTL_RX_CCMP 4U
struct rtl_rx_filter { rtl_u8 mac[6],bssid[6]; unsigned int selected,encrypted; };
typedef void (*rtl_control_sink)(void *,unsigned int kind,const rtl_u8 *,unsigned int);
/* Beacons plus unfragmented, unprotected control traffic from the selected AP
 * to our MAC. FCS required; exact LLC/RSN EAPOL length, no A-MSDU. The caller
 * checks station state and performs MIC/replay/RSN policy validation. */
/* With encrypted=1 additionally accepts bounded FromDS CCMP to us/groups,
 * only if RX descriptor SWDEC=1 (still encrypted). MIC/PN remain userland. */
int rtl8188cu_rx_parse_control(const rtl_u8 *,unsigned int,struct rtl_rx_stats *,
                              const struct rtl_rx_filter *,rtl_control_sink,void *);
#ifdef __cplusplus
}
#endif
#endif
