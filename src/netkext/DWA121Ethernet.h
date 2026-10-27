#ifndef DWA121_ETHERNET_H
#define DWA121_ETHERNET_H
#include <IOKit/network/IOEthernetController.h>

/* Optional companion to the USB diagnostic. The BSD attachment is real, but
 * activation is deliberately refused until the WPA/CCMP packet bridge exists.
 * No USB calls, keys, packet queues or provider lifecycle locks are borrowed. */
class DWA121Ethernet : public IOEthernetController {
    OSDeclareDefaultStructors(DWA121Ethernet)
    IOWorkLoop *networkLoop;
    IONetworkInterface *networkInterface;
    IOEthernetAddress address;
    UInt32 enableRefusals; /* only touched under the networking command gate */
public:
    virtual bool init(OSDictionary *properties = 0);
    virtual bool createWorkLoop();
    virtual IOWorkLoop *getWorkLoop() const;
    virtual bool start(IOService *provider);
    virtual void stop(IOService *provider);
    virtual void free();
    virtual IOReturn getHardwareAddress(IOEthernetAddress *);
    virtual IOReturn getPacketFilters(UInt32 *) const;
    virtual IOReturn enable(IONetworkInterface *);
    virtual IOReturn disable(IONetworkInterface *);
    virtual UInt32 outputPacket(struct mbuf *,void *);
    virtual const OSString *newVendorString() const;
    virtual const OSString *newModelString() const;
};
#endif
