#ifndef DWA121_TX_PROBE_H
#define DWA121_TX_PROBE_H
#include "AsyncTX.h"
/* Opt-in USB-only exercise: always reqCount=0, never sends a descriptor or
 * frame. Requires CR=0, TXPAUSE=ff, SECCFG=0, before RX is enabled. Enables
 * only HCI_TXDMA (CR=1) for USB OUT, restores CR=0. Radio TXDMA/MAC stay off. */
bool pantherTXProbe(IOService *,IOUSBPipe *,IOUSBPipe *,const rtl_bus *);
#endif
