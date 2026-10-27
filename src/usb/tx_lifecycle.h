#ifndef DWA121_TX_LIFECYCLE_H
#define DWA121_TX_LIFECYCLE_H
/* One logical packet, one USB request at a time. Serialize every call.
 * A full-size data transfer needs a separate ZLP; never report success before
 * that ZLP completes. No retry after failure: delivery may be ambiguous.
 * Accepted requests complete exactly once; rejected requests never callback.
 * pending includes a completed request until the workloop consumes it. */
#define RTL_TX_IDLE 0U
#define RTL_TX_DATA 1U
#define RTL_TX_ZLP 2U
#define RTL_TX_DONE 3U
#define RTL_TX_SUCCESS 1U
#define RTL_TX_FAILED 2U
#define RTL_TX_CANCELLED 3U
struct rtl_tx_lifecycle {
    unsigned int phase, pending, ready, needs_zlp, stopped, abort_requested;
    unsigned int result, stop_result;
};
#ifdef __cplusplus
extern "C" {
#endif
int rtl_tx_life_begin(struct rtl_tx_lifecycle *,int needs_zlp,int empty_probe);
int rtl_tx_life_reserve(struct rtl_tx_lifecycle *);
int rtl_tx_life_complete(struct rtl_tx_lifecycle *);
int rtl_tx_life_consume(struct rtl_tx_lifecycle *,int success);
int rtl_tx_life_reject(struct rtl_tx_lifecycle *);
/* Returns one only when backend must Abort its active pipe. */
int rtl_tx_life_stop(struct rtl_tx_lifecycle *,int failed);
/* Returns terminal result once, or zero. Never releases a pending buffer. */
unsigned int rtl_tx_life_take(struct rtl_tx_lifecycle *);
#ifdef __cplusplus
}
#endif
#endif
