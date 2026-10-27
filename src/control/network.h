#ifndef DWA121_NETWORK_H
#define DWA121_NETWORK_H
#include "../rtl8188cu/identity.h"
#define DWA_NET_VERSION 1U
#define DWA_NET_OPEN 1U
#define DWA_NET_PULSE 2U
#define DWA_NET_TX 3U
#define DWA_NET_RX 4U
#define DWA_NET_CLOSE 5U
#define DWA_NET_FRAME_MAX 1514U
#define DWA_NET_SLOTS 32U
#define DWA_NET_HEARTBEAT_MS 5000U
#define DWA_NET_FRAME_MS 2000U
/* Method 3 on the existing exclusive administrator control connection.
 * RX is plaintext authenticated by that trusted WPA2 owner, never by peers. */
struct dwa_net_request { rtl_u32 version,operation,length;rtl_u8 data[1516]; };
struct dwa_net_reply { rtl_u32 version,active,length,tx_dropped,rx_dropped;rtl_u8 data[1516]; };
struct dwa_net_frame { rtl_u32 length,time;rtl_u8 data[1516]; };
struct dwa_net_queue { struct dwa_net_frame frames[DWA_NET_SLOTS];rtl_u32 head,count,dropped; };
struct dwa_net_bridge { struct dwa_net_queue tx,rx;rtl_u8 mac[6];rtl_u32 pulse;int active; };
#ifdef __cplusplus
extern "C" {
#endif
int dwa_net_validate(const struct dwa_net_request *);
void dwa_net_reset(struct dwa_net_bridge *);
void dwa_net_open(struct dwa_net_bridge *,const rtl_u8 *,rtl_u32);
int dwa_net_active(const struct dwa_net_bridge *,rtl_u32);
int dwa_net_push(struct dwa_net_bridge *,int rx,const rtl_u8 *,unsigned int,rtl_u32);
int dwa_net_pop(struct dwa_net_bridge *,int rx,struct dwa_net_frame *,rtl_u32);
#ifdef __cplusplus
}
#endif
#endif
