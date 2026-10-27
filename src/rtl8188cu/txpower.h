#ifndef RTL8188CU_TXPOWER_H
#define RTL8188CU_TXPOWER_H
#include "identity.h"
#ifdef __cplusplus
extern "C" {
#endif
struct rtl_txpower { rtl_u8 rate[20]; }; /* CCK4, OFDM8, MCS0..7 */
/* Board 0, TSMC 1T1R, 20 MHz, channels 1..11 only. This computes calibrated
 * register indices, NOT dBm nor regulatory permission. The caller separately
 * approves the channel/country and any local power reduction before TX. */
int rtl8188cu_txpower_compute(const struct rtl_identity *,unsigned int channel,
                            struct rtl_txpower *);
/* Requires all TX queues paused and TXDMA disabled. Write/readback six
 * chain-A registers; preserve unrelated bits. On failure restores snapshots.
 * Never unpauses TX; RTL_RESTORE requires device reset/quiescence. */
int rtl8188cu_txpower_program(const struct rtl_bus *,const struct rtl_txpower *);
#ifdef __cplusplus
}
#endif
#endif
