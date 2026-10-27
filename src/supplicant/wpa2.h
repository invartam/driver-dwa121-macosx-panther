#ifndef DWA121_WPA2_H
#define DWA121_WPA2_H
#include <stddef.h>
/* Userland only. Serialized on the process's eloop thread. Backend callbacks
 * must not re-enter or destroy the supplicant. No production USB backend yet.
 * Keys and EAPOL buffers are borrowed only for the duration of each callback. */
struct dwa_wpa2;
struct dwa_wpa2_ops {
    void *cookie;
    int (*send_eapol)(void *,const unsigned char *,size_t);
    int (*install_ccmp)(void *,int pairwise,unsigned int index,
        const unsigned char key[16],const unsigned char *rsc,size_t rsc_length);
    /* Close controlled port immediately; delete all session keys on clear. */
    int (*port)(void *,int authorized);
    void (*clear_keys)(void *);
    void (*failed)(void *,unsigned int reason);
    /* Optional initial-handshake diagnostic: when provided, completion calls
     * this INSTEAD of port(1). Keys must still have been installed successfully.
     * authorized() remains false. The caller stops and erases after the probe. */
    int (*handshake_complete)(void *);
};
/* PMK is exactly 32 bytes. ap_rsn must be the verified advertised RSN IE;
 * association uses the separately negotiated PSK/CCMP IE from station.h. */
struct dwa_wpa2 *dwa_wpa2_create(const struct dwa_wpa2_ops *,
    const unsigned char mac[6],const unsigned char bssid[6],
    const unsigned char *ssid,size_t ssid_len,const unsigned char pmk[32],
    const unsigned char *ap_rsn,size_t ap_rsn_len);
/* Call only after successful association; random SNonce comes from urandom. */
int dwa_wpa2_associated(struct dwa_wpa2 *);
int dwa_wpa2_receive(struct dwa_wpa2 *,const unsigned char src[6],
    const unsigned char *eapol,size_t length);
void dwa_wpa2_disconnect(struct dwa_wpa2 *);
void dwa_wpa2_destroy(struct dwa_wpa2 *);
int dwa_wpa2_authorized(const struct dwa_wpa2 *);
int dwa_wpa2_handshake_verified(const struct dwa_wpa2 *);
int dwa_wpa2_pmk(const char *passphrase,const unsigned char *ssid,
    size_t ssid_len,unsigned char pmk[32]);
#endif
