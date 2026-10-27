#ifndef DWA121_PASSIVE_RX_H
#define DWA121_PASSIVE_RX_H
#include <IOKit/IOService.h>
#include <IOKit/usb/IOUSBPipe.h>
#include "../rtl8188cu/rx.h"
#include "../net80211/beacon.h"
struct PantherRXReport {
    struct rtl_rx_stats frames;
    unsigned int submitted, completed, timeouts, aborted, usbErrors;
    bool drained;
    IOReturn lastError;
};
/* Bounded diagnostic, one outstanding request at a time. Completion owns an
 * independent reference to its DMA buffer, pipe, and driver object until done.
 * A late callback after Abort cannot touch freed stack or buffer memory.
 */
int pantherReceiveProbe(IOService *, IOUSBPipe *, PantherRXReport *,
                        struct rtl_bss_table *, unsigned int, unsigned int);
int pantherAbortProbe(IOService *, IOUSBPipe *, PantherRXReport *);
#endif
