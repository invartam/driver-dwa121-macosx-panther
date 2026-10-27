#include "DWA121Ethernet.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/network/IONetworkInterface.h>
#include <IOKit/network/IONetworkMedium.h>
#include <IOKit/network/IOOutputQueue.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSBoolean.h>

OSDefineMetaClassAndStructors(DWA121Ethernet,IOEthernetController)

bool DWA121Ethernet::init(OSDictionary *properties)
{
    networkLoop=0;networkInterface=0;enableRefusals=0;bzero(&address,sizeof(address));
    return IOEthernetController::init(properties);
}
bool DWA121Ethernet::createWorkLoop()
{
    /* Do not inherit the USB provider's or diagnostic's workloop. */
    if(!networkLoop)networkLoop=IOWorkLoop::workLoop();
    return networkLoop!=0;
}
IOWorkLoop *DWA121Ethernet::getWorkLoop() const { return networkLoop; }

bool DWA121Ethernet::start(IOService *provider)
{
    OSData *mac;
    OSDictionary *media;
    IONetworkMedium *none;
    unsigned int i,nonzero=0;
    bool ok;
    if(!provider || !provider->metaCast("DWA121Diagnostic") ||
       getProperty("ExperimentalOfflineOnly")!=kOSBooleanTrue ||
       provider->getProperty("PersistentRXActive")!=kOSBooleanTrue)
        return false;
    mac=OSDynamicCast(OSData,provider->getProperty("MACAddress"));
    if(!mac || mac->getLength()!=sizeof(address.bytes))return false;
    bcopy(mac->getBytesNoCopy(),address.bytes,sizeof(address.bytes));
    for(i=0;i<sizeof(address.bytes);++i)nonzero|=address.bytes[i];
    if(!nonzero || (address.bytes[0]&1))return false;
    if(!IOEthernetController::start(provider))return false;
    media=OSDictionary::withCapacity(1);
    none=IONetworkMedium::medium(kIOMediumEthernetNone,0);
    ok=media && none && IONetworkMedium::addMedium(media,none) &&
       publishMediumDictionary(media) && setSelectedMedium(none) &&
       setLinkStatus(kIONetworkLinkValid);
    if(none)none->release();
    if(media)media->release();
    if(!ok || !setProperty("EthernetEnableRefusals",(UInt64)0,32) ||
       !setProperty("EthernetDataPathReady",false) ||
       !setProperty("EthernetPortAuthorized",false) ||
       !attachInterface(&networkInterface,false)) {
        IOEthernetController::stop(provider);return false;
    }
    networkInterface->registerService();
    registerService();
    IOLog("DWA121Ethernet: interface attached, link inactive, activation refused\n");
    return true;
}
void DWA121Ethernet::stop(IOService *provider)
{
    setLinkStatus(kIONetworkLinkValid);
    if(networkInterface) {
        /* Termination must not wait for BSD under a provider's workloop gate.
         * The networking family holds its own references until BSD detaches. */
        detachInterface(networkInterface,false);
        networkInterface->release();networkInterface=0;
    }
    IOLog("DWA121Ethernet: interface termination requested\n");
    IOEthernetController::stop(provider);
}
void DWA121Ethernet::free()
{
    /* The superclass also retained this workloop during start(). */
    if(networkLoop){networkLoop->release();networkLoop=0;}
    IOEthernetController::free();
}
IOReturn DWA121Ethernet::getHardwareAddress(IOEthernetAddress *out)
{
    if(!out)return kIOReturnBadArgument;
    *out=address;return kIOReturnSuccess;
}
IOReturn DWA121Ethernet::getPacketFilters(UInt32 *filters) const
{
    if(!filters)return kIOReturnBadArgument;
    *filters=kIOPacketFilterUnicast|kIOPacketFilterBroadcast;
    return kIOReturnSuccess;
}
IOReturn DWA121Ethernet::enable(IONetworkInterface *)
{
    /* A successful diagnostic handshake is not a persistent data session. */
    ++enableRefusals;
    setProperty("EthernetEnableRefusals",enableRefusals,32);
    return kIOReturnNotReady;
}
IOReturn DWA121Ethernet::disable(IONetworkInterface *)
{ return kIOReturnSuccess; }
UInt32 DWA121Ethernet::outputPacket(struct mbuf *packet,void *)
{
    /* Free exactly once even if a caller bypasses the disabled interface. */
    if(packet)freePacket(packet);
    return kIOReturnOutputDropped;
}
const OSString *DWA121Ethernet::newVendorString() const
{ return OSString::withCString("D-Link"); }
const OSString *DWA121Ethernet::newModelString() const
{ return OSString::withCString("DWA-121 A1 (experimental offline interface)"); }
