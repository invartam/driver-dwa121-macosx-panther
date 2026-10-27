#ifndef DWA121_SESSION_H
#define DWA121_SESSION_H
#include "wpa2.h"
#include "../net80211/station.h"
struct dwa_session;
struct dwa_session_ops {
    struct dwa_wpa2_ops security;
    /* Synchronous success means hardware/readback completed, not enqueued.
     * prepare must check channel/regulatory/TX power and keep the data port
     * closed. TX callbacks must copy buffers, be bounded and non-reentrant.
     * Actual USB/CAM implementations are NOT supplied by this library. */
    int (*prepare)(void *,const struct rtl_station *);
    int (*send_management)(void *,const unsigned char *,size_t);
    int (*associated)(void *,const struct rtl_station *);
    void (*stop)(void *);
};
/* Call eloop_init once in the owning process and run eloop_run to service
 * management retries and the WPA deadline. One object is one association;
 * reconnect requires a new object, a fresh beacon and a new SNonce. */
struct dwa_session *dwa_session_create(const struct dwa_session_ops *,
    const unsigned char mac[6],const unsigned char *beacon,size_t length,
    const unsigned char *ssid,size_t ssid_length,unsigned int channel,
    unsigned int beacon_age_ms,const unsigned char pmk[32]);
int dwa_session_receive_management(struct dwa_session *,const unsigned char *,size_t);
int dwa_session_receive_eapol(struct dwa_session *,const unsigned char src[6],
    const unsigned char *,size_t);
/* Bridge from the driver's FCS-verified queue. Reject stale/raw protected data;
 * the caller supplies age_ms from the same queue reply, never a new timestamp.
 * Does not copy or retain frame memory beyond this synchronous call. */
int dwa_session_receive_80211(struct dwa_session *,const unsigned char *,size_t,
    unsigned int age_ms);
unsigned int dwa_session_state(const struct dwa_session *);
int dwa_session_authorized(const struct dwa_session *);
void dwa_session_destroy(struct dwa_session *);
#endif
