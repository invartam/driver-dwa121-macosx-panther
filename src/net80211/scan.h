#ifndef DWA121_SCAN_H
#define DWA121_SCAN_H
#include "beacon.h"
#ifdef __cplusplus
extern "C" {
#endif
#define DWA_SCAN_TTL_MS 15000U
#define DWA_SCAN_NATIVE_BYTES 50U
/* Private in-kernel ABI, never exposed as an IOKit user structure. */
#define DWA_SCAN_START "DWA121PassiveScanStart"
#define DWA_SCAN_COPY "DWA121PassiveScanCopy"
void dwa_scan_merge(struct rtl_bss_table *, const struct rtl_bss_table *, rtl_u32);
void dwa_scan_expire(struct rtl_bss_table *, rtl_u32);
/* Returns 0 for SSIDs unsafe in Panther's C-string consumer (hidden, embedded
 * NUL or full 32 bytes). Internal scan data remains exact and length-delimited.
 * Noise/signal are unavailable (zero), NOT a measured RSSI. */
int dwa_scan_native(const struct rtl_bss *, rtl_u8 *);
#ifdef __cplusplus
}
#endif
#endif
