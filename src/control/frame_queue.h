#ifndef DWA121_FRAME_QUEUE_H
#define DWA121_FRAME_QUEUE_H
#include "../rtl8188cu/identity.h"
#ifdef __cplusplus
extern "C" {
#endif
#define DWA_FRAME_MAX 2304U
#define DWA_FRAME_SLOTS 16U
#define DWA_FRAME_MAX_AGE_MS 5000U
#define DWA_FRAME_BEACON 1U
#define DWA_FRAME_MANAGEMENT 2U
#define DWA_FRAME_EAPOL 3U
#define DWA_FRAME_CCMP 4U
struct dwa_frame {
    rtl_u32 kind, length, channel, received_ms;
    rtl_u8 data[DWA_FRAME_MAX];
};
struct dwa_frame_queue {
    struct dwa_frame slots[DWA_FRAME_SLOTS];
    rtl_u32 head,count,dropped,expired,delivered;
};
/* Caller serializes operations. No allocations. Full queues preserve control
 * frames: a new control frame evicts the oldest beacon/CCMP, otherwise rejects the
 * new frame. Beacons never evict existing frames. A session reset wipes data. */
void dwa_frame_queue_reset(struct dwa_frame_queue *);
int dwa_frame_queue_push(struct dwa_frame_queue *,unsigned int kind,unsigned int channel,
                         rtl_u32 now,const rtl_u8 *,unsigned int length);
/* Output always initialized, also on empty/error. Expired frames are discarded.
 * Unsigned milliseconds support wrap; callers must reset after sleep/restart. */
int dwa_frame_queue_pop(struct dwa_frame_queue *,rtl_u32 now,struct dwa_frame *);
#ifdef __cplusplus
}
#endif
#endif
