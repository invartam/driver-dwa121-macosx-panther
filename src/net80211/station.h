#ifndef DWA121_STATION_H
#define DWA121_STATION_H
#include "beacon.h"
#define RTL_STA_FRAME_MAX 512U
#define RTL_STA_RSN_SIZE 22U
enum rtl_sta_state { RTL_STA_IDLE, RTL_STA_AUTH, RTL_STA_ASSOC,
    RTL_STA_KEYS, RTL_STA_AUTHORIZED, RTL_STA_FAILED };
struct rtl_station {
    unsigned int state, attempts, sequence, aid, status;
    rtl_u8 mac[6];
    struct rtl_bss target;
    rtl_u8 rates[12];
    unsigned int rate_count;
    rtl_u8 tx[RTL_STA_FRAME_MAX];
    unsigned int tx_length;
};
extern const rtl_u8 rtl_station_rsn[RTL_STA_RSN_SIZE];
/* Pure protocol core, no USB/TX or timers. Caller provides a fresh, verified
 * beacon, explicit SSID, approved channel, and runs serialized. Output tx is
 * borrowed until the next call. Consume each newly produced frame once.
 * Return 1: frame ready; 0: no frame; negative: rejected/failed.
 * Regulatory/power approval is a separate transport prerequisite. */
int rtl_station_begin(struct rtl_station *, const rtl_u8 mac[6],
    const rtl_u8 *beacon, unsigned int length, const rtl_u8 *ssid,
    unsigned int ssid_length, unsigned int channel, unsigned int age_ms);
int rtl_station_receive(struct rtl_station *, const rtl_u8 *, unsigned int);
/* Caller invokes once per 1000 ms response deadline, never on every poll. */
int rtl_station_timeout(struct rtl_station *);
void rtl_station_disconnect(struct rtl_station *);
/* Only after the supplicant AND driver confirm keys/controlled port. */
int rtl_station_keys_ready(struct rtl_station *, int installed);
#endif
