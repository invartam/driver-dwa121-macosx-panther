#ifndef DWA121_RX_LIFECYCLE_H
#define DWA121_RX_LIFECYCLE_H
/* Single outstanding read. All operations must be serialized by the backend.
 * pending remains set until the completion has been consumed. A callback may
 * arrive inline inside submit or after stop; neither permits premature reuse.
 * USB contract: accepted requests complete exactly once; rejected submissions
 * never complete. No callback generation guessing is used. */
struct rtl_rx_lifecycle {
    unsigned int running, pending, ready, abort_requested;
};
#ifdef __cplusplus
extern "C" {
#endif
int rtl_rx_life_start(struct rtl_rx_lifecycle *);
int rtl_rx_life_reserve(struct rtl_rx_lifecycle *);
int rtl_rx_life_complete(struct rtl_rx_lifecycle *);
int rtl_rx_life_consume(struct rtl_rx_lifecycle *, int *deliver);
int rtl_rx_life_stop(struct rtl_rx_lifecycle *);
int rtl_rx_life_reject(struct rtl_rx_lifecycle *);
#ifdef __cplusplus
}
#endif
#endif
