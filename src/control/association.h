#ifndef DWA121_ASSOCIATION_CONTROL_H
#define DWA121_ASSOCIATION_CONTROL_H
#include "protocol.h"
#define DWA_ASSOC_VERSION 1U
#define DWA_ASSOC_BEGIN 1U
#define DWA_ASSOC_SEND 2U
#define DWA_ASSOC_POLL 3U
#define DWA_ASSOC_END 4U
#define DWA_ASSOC_ACTIVE 1U
#define DWA_ASSOC_DRAINED 2U
#define DWA_ASSOC_STOPPED 4U
#define DWA_ASSOC_MAX_MS 10000U
/* Separate method index 1. No arbitrary register or data/EAPOL access.
 * END consumes the hardware session: RX/radio also stop until kext reload. */
struct dwa_assoc_request {
    rtl_u32 version,operation,channel,length;
    rtl_u8 bssid[6],reserved[2],frame[512];
};
struct dwa_assoc_reply {
    rtl_u32 version,flags,result,submitted,completed,succeeded,failed,cancelled;
    rtl_u32 bytes,aborts,watchdogs,last_error;
};
struct dwa_assoc_budget { rtl_u32 start_ms,auth,assoc; };
#ifdef __cplusplus
extern "C" {
#endif
int dwa_assoc_validate(const struct dwa_assoc_request *,const rtl_u8 mac[6]);
/* Check before accepting a frame; update counts only after send succeeds. */
int dwa_assoc_admit(const struct dwa_assoc_budget *,rtl_u32 now,unsigned int subtype);
#ifdef __cplusplus
}
#endif
#endif
