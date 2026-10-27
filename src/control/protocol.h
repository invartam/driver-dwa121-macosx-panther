#ifndef DWA121_CONTROL_PROTOCOL_H
#define DWA121_CONTROL_PROTOCOL_H
#include "frame_queue.h"
/* Private native-endian ABI, shared by Panther PPC kernel and user space.
 * No pointers, packed fields, size_t, or architecture-dependent integers. */
#define DWA_CONTROL_TYPE 0x44574131U
#define DWA_CONTROL_VERSION 1U
#define DWA_CONTROL_STATUS 1U
#define DWA_CONTROL_SELECT 2U
#define DWA_CONTROL_RESET 3U
#define DWA_CONTROL_NEXT 4U
#define DWA_CONTROL_RUNNING 1U
#define DWA_CONTROL_SELECTED 2U
#define DWA_CONTROL_FRAME 4U
struct dwa_control_request {
    rtl_u32 version, operation;
    rtl_u8 bssid[6], reserved[2];
};
struct dwa_control_reply {
    rtl_u32 version, flags, channel, queued, dropped, expired, delivered, age_ms;
    rtl_u8 mac[6], bssid[6];
    struct dwa_frame frame;
};
#ifdef __cplusplus
extern "C" {
#endif
int dwa_control_validate(const struct dwa_control_request *,const rtl_u8 mac[6]);
#ifdef __cplusplus
}
#endif
#endif
