#ifndef DWA121_AIRPORT_PROTOCOL_H
#define DWA121_AIRPORT_PROTOCOL_H
#include "airport_join.h"
#define DWA_AIRPORT_BROKER_TYPE 0x50303332U
#define DWA_AIRPORT_OWNER_TYPE 0x50333243U
#define DWA_AIRPORT_VERSION 1U
#define DWA_AIRPORT_RESERVE "DWA121AirPortReserve"
#define DWA_AIRPORT_PREPARE "DWA121AirPortPrepare"
#define DWA_AIRPORT_CANCEL "DWA121AirPortCancel"
#define DWA_AIRPORT_STATUS "DWA121AirPortStatus"
#define DWA_AIRPORT_RECOVER "DWA121AirPortRecover"
#define DWA_AP_IDLE 0U
#define DWA_AP_REQUESTED 1U
#define DWA_AP_PREPARED 2U
#define DWA_AP_RUNNING 3U
#define DWA_AP_CONNECTED 4U
#define DWA_AP_FAILED 5U
#define DWA_AP_DISCONNECTED 6U
/* Private PPC32 native-endian ABI. No pointers. Exactly 60 bytes. */
struct dwa_airport_target {
    rtl_u32 version,generation,phase,channel,ssid_length;
    rtl_u8 ssid[32],bssid[6],reserved[2];
};
#define DWA_AP_POLL 1U
#define DWA_AP_TAKE 2U
#define DWA_AP_CANCEL 3U
#define DWA_AP_RECOVER 4U
struct dwa_airport_request { rtl_u32 version,operation,generation,reserved; };
/* Root-only, one-shot TAKE. Other operations return an all-zero secret. */
struct dwa_airport_reply {
    struct dwa_airport_target target;
    rtl_u32 secret_length;
    rtl_u8 secret[64];
};
#endif
