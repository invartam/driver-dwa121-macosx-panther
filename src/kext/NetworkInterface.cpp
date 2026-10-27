#include "NetworkInterface.h"
#include "DWA121Diagnostic.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/network/IONetworkInterface.h>
#include <IOKit/network/IONetworkMedium.h>
#include <IOKit/network/IOOutputQueue.h>
#include <IOKit/network/IONetworkData.h>
#include <IOKit/network/IOGatedOutputQueue.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSBoolean.h>
extern "C" {
#include <sys/param.h>
#include <sys/mbuf.h>
}
OSDefineMetaClassAndStructors(DWA121NetworkInterface,IOEthernetController)
bool DWA121NetworkInterface::init(OSDictionary *p)
{
    radio=0;loop=0;interface=0;timer=0;stats=0;timerAdded=enabled=linked=false;
    bzero(&mac,sizeof(mac));return IOEthernetController::init(p);
}
bool DWA121NetworkInterface::createWorkLoop()
{ if(!loop)loop=IOWorkLoop::workLoop();return loop!=0; }
IOWorkLoop *DWA121NetworkInterface::getWorkLoop() const { return loop; }
IOOutputQueue *DWA121NetworkInterface::createOutputQueue()
{ return IOGatedOutputQueue::withTarget(this,getWorkLoop(),32); }
bool DWA121NetworkInterface::configureInterface(IONetworkInterface *iface)
{
    if(!IOEthernetController::configureInterface(iface))return false;
    IONetworkData *data=iface->getNetworkData(kIONetworkStatsKey);
    stats=data?(IONetworkStats *)data->getBuffer():0;
    return stats!=0;
}
bool DWA121NetworkInterface::start(IOService *provider)
{
    radio=OSDynamicCast(DWA121Diagnostic,provider);
    if(!radio||radio->getProperty("NetworkSessionEnabled")!=kOSBooleanTrue)return false;
    OSData *address=OSDynamicCast(OSData,radio->getProperty("MACAddress"));
    if(!address||address->getLength()!=6)return false;
    bcopy(address->getBytesNoCopy(),mac.bytes,6);
    if(!IOEthernetController::start(provider))return false;
    OSDictionary *media=OSDictionary::withCapacity(1);
    IONetworkMedium *automatic=IONetworkMedium::medium(kIOMediumEthernetAuto,1000000);
    bool ok=media&&automatic&&IONetworkMedium::addMedium(media,automatic)&&
        publishMediumDictionary(media)&&setSelectedMedium(automatic)&&setLinkStatus(kIONetworkLinkValid);
    if(media)media->release();if(automatic)automatic->release();
    timer=IOTimerEventSource::timerEventSource(this,tick);
    if(!ok||!timer||loop->addEventSource(timer)!=kIOReturnSuccess)goto fail;
    timerAdded=true;
    if(!attachInterface(&interface,false))goto fail;
    if(timer->setTimeoutMS(20)!=kIOReturnSuccess)goto fail;
    setProperty("WPA2LinkActive",false);
    interface->registerService();registerService();return true;
fail:
    stop(provider);return false;
}
void DWA121NetworkInterface::stop(IOService *provider)
{
    if(getOutputQueue()){getOutputQueue()->stop();getOutputQueue()->flush();}
    if(timer){timer->cancelTimeout();if(timerAdded){loop->removeEventSource(timer);timerAdded=false;}}
    enabled=linked=false;setLinkStatus(kIONetworkLinkValid);
    stats=0;
    if(interface){detachInterface(interface,false);interface->release();interface=0;}
    radio=0;IOEthernetController::stop(provider);
}
void DWA121NetworkInterface::free()
{
    if(timer)timer->release();if(loop)loop->release();
    IOEthernetController::free();
}
void DWA121NetworkInterface::tick(OSObject *owner,IOTimerEventSource *sender)
{
    DWA121NetworkInterface *d=(DWA121NetworkInterface *)owner;
    if(!d->radio||d->isInactive())return;
    bool active=d->enabled&&d->radio->networkStatus();
    if(active!=d->linked){
        d->linked=active;d->setProperty("WPA2LinkActive",active);
        d->setLinkStatus(kIONetworkLinkValid|(active?kIONetworkLinkActive:0),active?d->getSelectedMedium():0);
    }
    dwa_net_frame frame;
    for(unsigned int i=0;active&&i<32&&d->radio->networkReceive(&frame);++i){
        struct mbuf *m=d->allocatePacket(frame.length);
        if(m){
            unsigned int copied=0;struct mbuf *part;
            for(part=m;part&&copied<frame.length;part=part->m_next){
                unsigned int n=part->m_len;
                if(n>frame.length-copied)n=frame.length-copied;
                bcopy(frame.data+copied,part->m_data,n);copied+=n;
            }
            if(copied==frame.length){if(d->stats)++d->stats->inputPackets;d->interface->inputPacket(m,frame.length);}
            else {if(d->stats)++d->stats->inputErrors;d->freePacket(m);}
        } else if(d->stats)++d->stats->inputErrors;
    }
    bzero(&frame,sizeof(frame));sender->setTimeoutMS(10);
}
IOReturn DWA121NetworkInterface::getHardwareAddress(IOEthernetAddress *out)
{ if(!out)return kIOReturnBadArgument;*out=mac;return kIOReturnSuccess; }
IOReturn DWA121NetworkInterface::getPacketFilters(UInt32 *out) const
{ if(!out)return kIOReturnBadArgument;*out=kIOPacketFilterUnicast|kIOPacketFilterBroadcast|kIOPacketFilterMulticast;return kIOReturnSuccess; }
IOReturn DWA121NetworkInterface::setMulticastMode(bool) { return kIOReturnSuccess; }
IOReturn DWA121NetworkInterface::setMulticastList(IOEthernetAddress *,UInt32) { return kIOReturnSuccess; }
IOReturn DWA121NetworkInterface::enable(IONetworkInterface *)
{ enabled=true;getOutputQueue()->start();return kIOReturnSuccess; }
IOReturn DWA121NetworkInterface::disable(IONetworkInterface *)
{ getOutputQueue()->stop();getOutputQueue()->flush();enabled=linked=false;setLinkStatus(kIONetworkLinkValid);return kIOReturnSuccess; }
UInt32 DWA121NetworkInterface::outputPacket(struct mbuf *m,void *)
{
    rtl_u8 frame[DWA_NET_FRAME_MAX];bool accepted=false;
    if(m&&(m->m_flags&M_PKTHDR)&&m->m_pkthdr.len>=14&&(unsigned int)m->m_pkthdr.len<=DWA_NET_FRAME_MAX&&radio){
        unsigned int n=m->m_pkthdr.len,total=0;struct mbuf *part;
        for(part=m;part;part=part->m_next){if(part->m_len<0||(unsigned int)part->m_len>n-total){total=0;break;}total+=part->m_len;}
        if(total==n){total=0;for(part=m;part;part=part->m_next){bcopy(part->m_data,frame+total,part->m_len);total+=part->m_len;}accepted=radio->networkTransmit(frame,n);}
    }
    if(stats){if(accepted)++stats->outputPackets;else ++stats->outputErrors;}
    bzero(frame,sizeof(frame));if(m)freePacket(m);
    return accepted?kIOReturnOutputSuccess:kIOReturnOutputDropped;
}
const OSString *DWA121NetworkInterface::newVendorString() const { return OSString::withCString("D-Link"); }
const OSString *DWA121NetworkInterface::newModelString() const { return OSString::withCString("DWA-121 A1 WPA2 USB"); }
