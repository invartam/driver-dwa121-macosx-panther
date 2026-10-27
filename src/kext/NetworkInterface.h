#ifndef DWA121_NETWORK_INTERFACE_H
#define DWA121_NETWORK_INTERFACE_H
#include <IOKit/network/IOEthernetController.h>
#include <IOKit/network/IONetworkStats.h>
class DWA121Diagnostic;
class IOTimerEventSource;
class DWA121NetworkInterface : public IOEthernetController {
    OSDeclareDefaultStructors(DWA121NetworkInterface)
    DWA121Diagnostic *radio;
    IOWorkLoop *loop;
    IONetworkInterface *interface;
    IOTimerEventSource *timer;
    IOEthernetAddress mac;
    IONetworkStats *stats;
    bool timerAdded,enabled,linked;
    static void tick(OSObject *,IOTimerEventSource *);
public:
    virtual bool init(OSDictionary * = 0);
    virtual bool createWorkLoop();
    virtual IOWorkLoop *getWorkLoop() const;
    virtual IOOutputQueue *createOutputQueue();
    virtual bool start(IOService *);
    virtual bool configureInterface(IONetworkInterface *);
    virtual void stop(IOService *);
    virtual void free();
    virtual IOReturn getHardwareAddress(IOEthernetAddress *);
    virtual IOReturn getPacketFilters(UInt32 *) const;
    virtual IOReturn setMulticastMode(bool);
    virtual IOReturn setMulticastList(IOEthernetAddress *,UInt32);
    virtual IOReturn enable(IONetworkInterface *);
    virtual IOReturn disable(IONetworkInterface *);
    virtual UInt32 outputPacket(struct mbuf *,void *);
    virtual const OSString *newVendorString() const;
    virtual const OSString *newModelString() const;
};
#endif
