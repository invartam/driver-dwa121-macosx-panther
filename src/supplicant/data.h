#ifndef DWA121_DATA_H
#define DWA121_DATA_H
#include <stddef.h>
#define DWA_DATA_ETH_MAX 1514
#define DWA_DATA_WIFI_MAX 1550
struct dwa_data;
/* Userland only, serialized by one owner. One object per fresh association/TK.
 * Input/output buffers may be unaligned but must not overlap. No FCS in frames.
 * Output length is zero on failure; output bytes are untouched on failure.
 * No USB, interface, timer, WPA verification or hardware crypto in this module. */
struct dwa_data *dwa_data_create(const unsigned char mac[6],
    const unsigned char bssid[6]);
/* Same signature as the supplicant install callback. RX RSC is little endian,
 * zero to six bytes. Duplicate key installation preserves every counter.
 * Slot replacement and reuse of a TK in another slot are rejected: reconnect
 * with fresh key material is required once the four GTK slots are exhausted. */
int dwa_data_install_ccmp(void *,int pairwise,unsigned int index,
    const unsigned char key[16],const unsigned char *rsc,size_t rsc_length);
/* Trusted WPA owner calls port(1) only after verified M3 and successful M4.
 * port(0)/clear wipe keys and are terminal after the first key installation;
 * initial empty closes from WPA create/start are harmless. */
int dwa_data_port(void *,int enabled);
void dwa_data_clear(void *);
int dwa_data_authorized(const struct dwa_data *);
void dwa_data_destroy(struct dwa_data *);
/* Ethernet II, MTU 1500, RFC1042; EAPOL and legacy AARP/IPX are separate paths.
 * TX is non-QoS ToDS/PTK even for multicast Ethernet destinations. Each
 * successful encoding consumes PN before return, including abandoned USB TX.
 * Do not re-encode an old frame with a saved PN or retain frames after close. */
int dwa_data_encrypt(struct dwa_data *,const unsigned char *ethernet,size_t,
    unsigned char *wifi,size_t capacity,size_t *length);
/* RX: protected FromDS data/QoS, PTK unicast or GTK group; no fragmentation,
 * A-MSDU, WDS, HT-control or hardware-decrypted input. Caller must validate FCS
 * and queue freshness. Per-key/per-TID replay state advances only after MIC
 * and supported LLC validation. Unordered/out-of-order packets are dropped. */
int dwa_data_decrypt(struct dwa_data *,const unsigned char *wifi,size_t,
    unsigned char *ethernet,size_t capacity,size_t *length);
/* Network owner only: also authenticates encrypted EAPOL. Caller MUST route
 * EtherType 888e to WPA control, never to the BSD Ethernet data interface. */
int dwa_data_decrypt_network(struct dwa_data *,const unsigned char *,size_t,
    unsigned char *,size_t,size_t *);
#endif
