#include "DWA121Diagnostic.h"
#include <IOKit/IOMessage.h>
#include "PassiveRX.h"
#include "PersistentRX.h"
#include "ControlClient.h"
#include "TXProbe.h"
#include "../rtl8188cu/station_hw.h"
#include <kern/clock.h>
#include <IOKit/IOUserClient.h>
#include <kern/task.h>
#include "../usb/rtl_usb_profile.h"
#include "../usb/rtl_wire.h"
#include "../rtl8188cu/identity.h"
#include "../rtl8188cu/firmware.h"
#include "../rtl8188cu/radio.h"
#include "../rtl8188cu/cam.h"
#include "../rtl8188cu/txpower.h"
#include "../firmware/images.h"
#include <IOKit/IOLib.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSArray.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSBoolean.h>
#include <IOKit/usb/IOUSBDevice.h>
#include <IOKit/usb/IOUSBInterface.h>
#include <IOKit/usb/IOUSBPipe.h>

/* Panther owns USB lifetime; the portable core owns register sequencing. */
struct PantherBusContext {
    IOUSBDevice *device;
    IOReturn firstError;
};
static int pantherTransfer(PantherBusContext *context, UInt16 address,
                           unsigned int width, rtl_u32 *value, bool write)
{
    IOUSBDevRequest request;
    unsigned char bytes[4];
    unsigned int i;
    IOReturn result;
    if (width != 1 && width != 2 && width != 4) return RTL_BAD_ARGUMENT;
    bzero(&request, sizeof(request));
    bzero(bytes, sizeof(bytes));
    if (write) for (i = 0; i < width; ++i) bytes[i] = (unsigned char)(*value >> (8 * i));
    request.bmRequestType = USBmakebmRequestType(write ? kUSBOut : kUSBIn, kUSBVendor, kUSBDevice);
    request.bRequest = RTL_REQ_REGISTERS;
    request.wValue = address;
    request.wLength = width;
    request.pData = bytes;
    result = context->device->DeviceRequest(&request, 1000, 2000);
    if (result == kIOReturnSuccess && request.wLenDone != width) result = kIOReturnUnderrun;
    if (result != kIOReturnSuccess) {
        if (context->firstError == kIOReturnSuccess) context->firstError = result;
        return RTL_IO;
    }
    if (!write) *value = (rtl_u32)rtl_get_le32(bytes);
    return RTL_OK;
}
static int pantherRead(void *cookie, rtl_u16 address, unsigned int width, rtl_u32 *value)
{
    return pantherTransfer((PantherBusContext *)cookie, address, width, value, false);
}
static int pantherWrite(void *cookie, rtl_u16 address, unsigned int width, rtl_u32 value)
{
    return pantherTransfer((PantherBusContext *)cookie, address, width, &value, true);
}
static int pantherWriteBlock(void *cookie, rtl_u16 address, const rtl_u8 *bytes, unsigned int length)
{
    PantherBusContext *context = (PantherBusContext *)cookie;
    IOUSBDevRequest request;
    IOReturn result;
    /* Limit this backend to the firmware RAM window and the known CU chunks. */
    if (!bytes || (length != 196 && length != 4 && length != 1) ||
        address < 0x1000 || (unsigned int)address + length > 0x2000)
        return RTL_BAD_ARGUMENT;
    bzero(&request, sizeof(request));
    request.bmRequestType = USBmakebmRequestType(kUSBOut, kUSBVendor, kUSBDevice);
    request.bRequest = RTL_REQ_REGISTERS;
    request.wValue = address;
    request.wLength = length;
    request.pData = (void *)bytes;
    result = context->device->DeviceRequest(&request, 1000, 2000);
    if (result == kIOReturnSuccess && request.wLenDone != length) result = kIOReturnUnderrun;
    if (result != kIOReturnSuccess) {
        if (context->firstError == kIOReturnSuccess) context->firstError = result;
        return RTL_IO;
    }
    return RTL_OK;
}
static void pantherDelay(void *, unsigned int microseconds)
{
    if (microseconds >= 1000) IOSleep((microseconds + 999) / 1000);
    else IODelay(microseconds);
}

OSDefineMetaClassAndStructors(DWA121Diagnostic, IOService)

static bool bssNumber(OSDictionary *d, const char *key, unsigned int value)
{
    OSNumber *n = OSNumber::withNumber(value, 32);
    if (!n) return false;
    bool ok = d->setObject(key, n); n->release(); return ok;
}
static bool bssBytes(OSDictionary *d, const char *key, const void *bytes, unsigned int size)
{
    OSData *data = OSData::withBytes(bytes, size);
    if (!data) return false;
    bool ok = d->setObject(key, data); data->release(); return ok;
}
static bool publishBSS(IOService *owner, const struct rtl_bss_table *table)
{
    OSArray *list = OSArray::withCapacity(RTL_BSS_MAX);
    if (!list) return false;
    bool ok = true;
    for (unsigned int i = 0; i < table->count && ok; ++i) {
        const struct rtl_bss *b = &table->entries[i];
        OSDictionary *d = OSDictionary::withCapacity(16);
        if (!d) { ok = false; break; }
        ok = bssBytes(d, "BSSID", b->bssid, 6) &&
             bssBytes(d, "SSID", b->ssid, b->ssid_length) &&
             bssBytes(d, "RSNInformationElement", b->rsn_ie, b->rsn_length) &&
             bssNumber(d, "AdvertisedChannel", b->channel) &&
             bssNumber(d, "ReceivedChannel", b->received_channel) &&
             bssNumber(d, "BeaconIntervalTU", b->beacon_interval) &&
             bssNumber(d, "Capabilities", b->capabilities) &&
             bssNumber(d, "Observations", b->observations) &&
             bssNumber(d, "RSNStatus", b->rsn.status) &&
             bssNumber(d, "RSNCapabilities", b->rsn.capabilities) &&
             d->setObject("HiddenSSID", b->hidden ? kOSBooleanTrue : kOSBooleanFalse) &&
             d->setObject("WPA2PSKCCMPAdvertised", rtl_bss_advertises_psk_ccmp(b) ? kOSBooleanTrue : kOSBooleanFalse) &&
             d->setObject("PMFRequired", (b->rsn.capabilities & 0x40) ? kOSBooleanTrue : kOSBooleanFalse) &&
             d->setObject("PMFCapable", (b->rsn.capabilities & 0x80) ? kOSBooleanTrue : kOSBooleanFalse) &&
             list->setObject(d);
        d->release();
    }
    if (ok) ok = owner->setProperty("DiscoveredBSS", list) &&
        owner->setProperty("BSSCount", table->count, 32) &&
        owner->setProperty("BSSObservations", table->accepted, 32) &&
        owner->setProperty("BSSRejected", table->rejected, 32) &&
        owner->setProperty("BSSDropped", table->dropped, 32);
    list->release();
    return ok;
}

static void publishLive(IOService *owner, const PantherLiveReport *r, const rtl_bss_table *table)
{
    const PantherRXReport *u=&r->usb;
    owner->setProperty("RXRunning",r->running);
    owner->setProperty("PersistentRXActive",r->running);
    owner->setProperty("RXStopping",r->stopping);
    owner->setProperty("RXRequestsDrained",u->drained);
    owner->setProperty("RXUSBSubmitted",u->submitted,32);
    owner->setProperty("RXUSBCompleted",u->completed,32);
    owner->setProperty("RXUSBTimeouts",u->timeouts,32);
    owner->setProperty("RXUSBAborted",u->aborted,32);
    owner->setProperty("RXUSBErrors",u->usbErrors,32);
    owner->setProperty("RXLastError",(UInt32)u->lastError,32);
    owner->setProperty("RXWatchdogs",r->watchdogs,32);
    owner->setProperty("RXAbortRequests",r->abortRequests,32);
    owner->setProperty("RXSubmissionsRejected",r->submissionsRejected,32);
    owner->setProperty("RXDescriptors",u->frames.descriptors,32);
    owner->setProperty("RXBeacons",u->frames.beacons,32);
    owner->setProperty("RXManagement",u->frames.management,32);
    owner->setProperty("RXEAPOL",u->frames.eapol,32);
    owner->setProperty("RXCCMP",u->frames.encrypted,32);
    owner->setProperty("RXAuthSeen",u->frames.auth_seen,32);
    owner->setProperty("RXAuthToUs",u->frames.auth_to_us,32);
    owner->setProperty("RXAuthFromAP",u->frames.auth_from_ap,32);
    owner->setProperty("RXAuthLastLength",u->frames.auth_last_length,32);
    owner->setProperty("RXBytes",u->frames.bytes,32);
    owner->setProperty("RXHardwareCRCErrors",u->frames.crc_errors,32);
    owner->setProperty("RXICVErrors",u->frames.icv_errors,32);
    owner->setProperty("RXFCSErrors",u->frames.fcs_errors,32);
    owner->setProperty("RXMalformed",u->frames.malformed,32);
    owner->setProperty("RXFiltered",u->frames.filtered,32);
    owner->setProperty("RXFramesVerified",u->frames.beacons!=0);
    owner->setProperty("BSSSnapshotValid",publishBSS(owner,table));
}
bool DWA121Diagnostic::init(OSDictionary *dictionary)
{
    liveRX=0; liveInterface=0; liveContext=0; lifecycleLock=0; serviceLoop=0;networkBridge=0;
    usbTerminating=false;
    controlOwner=0; liveTX=0; liveHighPipe=liveNormalPipe=0; liveChannel=0;
    associationTimer=0; associationTimerAdded=false;
    bzero(&liveIdentity,sizeof(liveIdentity)); bzero(livePeer,sizeof(livePeer));
    bzero(&txBudget,sizeof(txBudget));
    wpaActive=false;wpaPrepared=false;wpaKeyMask=0;wpaSends=0;bzero(wpaRSC,sizeof(wpaRSC));
    wpaVerified=dataProbeActive=false;bzero(&dataBudget,sizeof(dataBudget));
    bzero(&liveBus,sizeof(liveBus));
    if (!IOService::init(dictionary)) return false;
    lifecycleLock=IOLockAlloc();
    networkBridge=(dwa_net_bridge *)IOMalloc(sizeof(*networkBridge));
    if(!networkBridge)return false;
    dwa_net_reset(networkBridge);
    serviceLoop=IOWorkLoop::workLoop();
    if(!lifecycleLock || !serviceLoop) return false;
    associationTimer=IOTimerEventSource::timerEventSource(this,associationExpired);
    if(!associationTimer || serviceLoop->addEventSource(associationTimer)!=kIOReturnSuccess) return false;
    associationTimerAdded=true;
    return true;
}
IOWorkLoop *DWA121Diagnostic::getWorkLoop() const
{
    /* Panther finalizes a service under getWorkLoop()->runAction. Inheriting
     * the USB controller's loop deadlocks synchronous shutdown requests.
     * Keep this lifecycle loop distinct from both USB and the RX event loop. */
    return serviceLoop;
}
IOReturn DWA121Diagnostic::message(UInt32 type,IOService *provider,void *argument)
{
    if(provider==getProvider()&&(type==kIOMessageServiceIsTerminated||type==kIOMessageServiceIsRequestingClose)) {
        /* didTerminate cannot advance while the USB provider is open.
         * Never drain USB under its caller's gate; close it on our own loop. */
        usbTerminating=true;
        return associationTimer?associationTimer->setTimeoutMS(0):kIOReturnNotReady;
    }
    return IOService::message(type,provider,argument);
}
bool DWA121Diagnostic::finalize(IOOptionBits options)
{
    /* Panther later calls stop() under the PROVIDER's workloop gate. Quiesce
     * here, under our lifecycle loop, before IOService schedules that phase.
     * stop() then only repeats the already completed, idempotent cleanup. */
    stopLive();
    return IOService::finalize(options);
}
bool DWA121Diagnostic::stopLive()
{
    if (!lifecycleLock) return true;
    IOLockLock(lifecycleLock);
    bool ok=stopLiveLocked();
    IOLockUnlock(lifecycleLock); return ok;
}
bool DWA121Diagnostic::stopLiveLocked()
{
    if(networkBridge)dwa_net_reset(networkBridge);
    setProperty("NetworkSessionActive",false);
    if(associationTimer) associationTimer->cancelTimeout();
    controlOwner=0;
    if(liveRX)liveRX->setEncryptedCapture(false);
    dataProbeActive=false;wpaVerified=false;bzero(&dataBudget,sizeof(dataBudget));
    setProperty("CCMPDataProbeActive",false);
    bool absent=usbTerminating&&liveContext&&liveContext->device->isInactive();
    if(absent){setProperty("USBRemoved",true);setProperty("WPA2HardwareKeyEraseUnverified",true);}
    bool ok=true;
    if(liveTX) {
        const rtl_u16 addresses[]={0x100,0x10c,0x200,0x204,0x208,0x210,0x214,0x420,0x522,0xfe65};
        const unsigned int widths[]={2,2,4,4,4,4,1,4,1,2};
        for(unsigned int i=0;!absent&&i<sizeof(addresses)/sizeof(addresses[0]);++i) {
            rtl_u32 value=0; int error=liveBus.read(liveBus.cookie,addresses[i],widths[i],&value);
            IOLog("DWA121Diagnostic: association TX register %04x=%08lx error=%d\n",
                  addresses[i],(unsigned long)value,error);
        }
        /* Block radio queues before cancelling DMA. Never reuse this session:
         * an OUT completion does not prove the hardware TX FIFO is empty. */
        int blocked=absent?RTL_OK:rtl8188cu_station_hw_block(&liveBus);
        bool drained=liveTX->shutdown();
        PantherTXReport tx; liveTX->snapshot(&tx);
        setProperty("AssociationTXBlocked",!absent&&blocked==RTL_OK);
        setProperty("AssociationTXDrained",drained);
        setProperty("AssociationTXSubmitted",tx.submitted,32);
        setProperty("AssociationTXCompleted",tx.completed,32);
        setProperty("AssociationTXBytes",tx.bytesSubmitted,32);
        setProperty("AssociationTXErrors",tx.failed,32);
        setProperty("AssociationTXAborts",tx.aborts,32);
        setProperty("AssociationTXWatchdogs",tx.watchdogs,32);
        setProperty("AssociationTXLastError",(UInt32)tx.lastError,32);
        IOLog("DWA121Diagnostic: association TX stop USB=%u/%u bytes=%u errors=%u blocked=%d drained=%d\n",
              tx.completed,tx.submitted,tx.bytesSubmitted,tx.failed,blocked==RTL_OK,drained);
        ok=blocked==RTL_OK && drained;
        if(wpaActive||wpaPrepared) {
            int erased=absent?RTL_OK:rtl8188cu_station_hw_erase(&liveBus);
            setProperty("WPA2KeysErased",!absent&&erased==RTL_OK);
            setProperty("WPA2ProbeActive",false);setProperty("WPA2DataPortAuthorized",false);
            ok=ok&&erased==RTL_OK;
            wpaActive=false;wpaPrepared=false;wpaKeyMask=0;bzero(wpaRSC,sizeof(wpaRSC));
        }
        liveTX->release(); liveTX=0;
    }
    if (liveRX) {
        bool drained=liveRX->shutdown();
        int rx=absent?RTL_OK:rtl8188cu_rx_disable(&liveBus);
        int radio=absent?RTL_OK:rtl8188cu_radio_quiesce(&liveBus);
        ok=ok && drained && rx==RTL_OK && radio==RTL_OK;
        setProperty("RXRunning",false);
        setProperty("RadioRunning",radio!=RTL_OK);
        setProperty("FirmwareRunning",radio!=RTL_OK);
        setProperty("RadioQuiesced",!absent&&radio==RTL_OK);
        setProperty("FirmwareQuiesced",!absent&&radio==RTL_OK);
        setProperty("RXHardwareStopVerified",!absent&&rx==RTL_OK);
        setProperty("PersistentStopVerified",!absent&&ok);
        setProperty("USBRemovalCleanupVerified",absent&&ok);
        setProperty("PortStage",absent?"usb-removed":(ok?"persistent-rx-stopped":"persistent-rx-stop-failed"));
        liveInterface->close(this);
        liveRX->release(); liveRX=0;
        liveContext->device->release();
        IOFree(liveContext,sizeof(*liveContext)); liveContext=0;
        liveInterface->release(); liveInterface=0;
        liveHighPipe=liveNormalPipe=0; bzero(livePeer,sizeof(livePeer));
        IOLog("DWA121Diagnostic: persistent cleanup drained=%d RX=%d radio=%d\n",drained,rx,radio);
    } else {
        OSBoolean *verified=OSDynamicCast(OSBoolean,getProperty("PersistentStopVerified"));
        if (verified) ok=verified->isTrue();
    }
    return ok;
}
IOReturn DWA121Diagnostic::claimControl(DWA121ControlClient *client)
{
    IOLockLock(lifecycleLock);
    IOReturn result=kIOReturnNotReady;
    if (liveRX && !isInactive()) {
        if (controlOwner) result=kIOReturnExclusiveAccess;
        else {
            result=liveRX->setCapture(true);
            if (result==kIOReturnSuccess) controlOwner=client;
        }
    }
    IOLockUnlock(lifecycleLock); return result;
}
void DWA121Diagnostic::releaseControl(DWA121ControlClient *client)
{
    IOLockLock(lifecycleLock);
    if (controlOwner==client) {
        if(liveTX) stopLiveLocked();
        else { if (liveRX) liveRX->setCapture(false); controlOwner=0; }
    }
    IOLockUnlock(lifecycleLock);
}
IOReturn DWA121Diagnostic::controlCommand(DWA121ControlClient *client,const dwa_control_request *r,dwa_control_reply *out)
{
    IOLockLock(lifecycleLock);
    IOReturn result=kIOReturnNotReady;
    if (controlOwner==client && liveRX && !isInactive()) {
        if(liveTX && (r->operation==DWA_CONTROL_SELECT || r->operation==DWA_CONTROL_RESET))
            result=kIOReturnBusy;
        else result=liveRX->control(r,out);
    }
    IOLockUnlock(lifecycleLock); return result;
}
IOReturn DWA121Diagnostic::newUserClient(task_t task,void *security,UInt32 type,IOUserClient **handler)
{
    if (!handler) return kIOReturnBadArgument;
    *handler=0;
    if (type!=DWA_CONTROL_TYPE) return kIOReturnUnsupported;
    IOReturn privilege=IOUserClient::clientHasPrivilege(task,kIOClientPrivilegeAdministrator);
    if (privilege!=kIOReturnSuccess) return privilege;
    DWA121ControlClient *client=new DWA121ControlClient;
    if (!client) return kIOReturnNoMemory;
    if (!client->initWithTask(task,security,type)) { client->release(); return kIOReturnNoMemory; }
    if (!client->attach(this)) { client->release(); return kIOReturnNotAttached; }
    if (!client->start(this)) { client->detach(this); client->release(); return kIOReturnNotReady; }
    *handler=client; return kIOReturnSuccess;
}
IOReturn DWA121Diagnostic::setProperties(OSObject *properties)
{
    OSDictionary *d=OSDynamicCast(OSDictionary,properties);
    if (!d || d->getCount()!=1 || d->getObject("RXStop")!=kOSBooleanTrue)
        return kIOReturnUnsupported;
    IOReturn privilege=IOUserClient::clientHasPrivilege(current_task(),kIOClientPrivilegeAdministrator);
    if (privilege!=kIOReturnSuccess) return privilege;
    return stopLive()?kIOReturnSuccess:kIOReturnIOError;
}
void DWA121Diagnostic::stop(IOService *provider)
{
    stopLive();
    IOService::stop(provider);
}
void DWA121Diagnostic::free()
{
    if(networkBridge){dwa_net_reset(networkBridge);IOFree(networkBridge,sizeof(*networkBridge));networkBridge=0;}
    if(associationTimer) {
        associationTimer->cancelTimeout();
        if(associationTimerAdded) serviceLoop->removeEventSource(associationTimer);
        associationTimer->release();
    }
    if (lifecycleLock) IOLockFree(lifecycleLock);
    if (serviceLoop) serviceLoop->release();
    IOService::free();
}

bool DWA121Diagnostic::start(IOService *provider)
{
    /* kextunload may terminate the provider while start is still calibrating.
     * Serialize with finalize/stop: never publish an orphaned live RX engine. */
    IOLockLock(lifecycleLock);
    bool ok=!isInactive()&&startHardwareLocked(provider);
    IOLockUnlock(lifecycleLock);
    return ok;
}
bool DWA121Diagnostic::startHardwareLocked(IOService *provider)
{
    IOUSBInterface *interface = OSDynamicCast(IOUSBInterface, provider);
    IOUSBDevice *device;
    IOUSBPipe *pipe = 0;
    IOUSBPipe *rxPipe = 0;
    IOUSBPipe *highPipe = 0, *normalPipe = 0;
    IOUSBFindEndpointRequest endpoint;
    struct rtl_identity identity;
    struct rtl_bus bus;
    struct rtl_fw_bus firmwareBus;
    struct rtl_fw_report firmwareReport;
    struct rtl_radio_report radioReport;
    struct rtl_dma_report dmaReport;
    PantherRXReport rxReport;
    const rtl_u8 *firmware;
    unsigned int firmwareLength;
    PantherBusContext context;
    UInt32 config;
    UInt32 rxChannel = 1;
    UInt32 rxReads = 32;
    bool persistent = getProperty("PersistentRX") == kOSBooleanTrue;
    rtl_u32 tunedRFChannel = 0;
    OSNumber *requestedChannel = OSDynamicCast(OSNumber, getProperty("PassiveRXChannel"));
    if (requestedChannel) rxChannel = requestedChannel->unsigned32BitValue();
    if (rxChannel < 1 || rxChannel > 11) return false;
    OSNumber *requestedReads = OSDynamicCast(OSNumber, getProperty("PassiveRXReadLimit"));
    if (requestedReads) rxReads = requestedReads->unsigned32BitValue();
    if (!rxReads || rxReads > 64) return false;
    int result;
    unsigned int rxCount = 0, txCount = 0;
    unsigned int minBulkPacket = 0xffff, maxBulkPacket = 0, usbSpeed = 0;
    struct rtl_usb_profile usbProfile;
    UInt8 rxAddress = 0, txMask = 0;

    if (!interface || !(device = interface->GetDevice()) ||
        device->GetVendorID() != DWA121_VENDOR ||
        device->GetProductID() != DWA121_PRODUCT ||
        interface->GetInterfaceNumber() != 0)
        return false;
    if (!IOService::start(provider)) return false;
    if (!interface->open(this)) {
        IOLog("DWA121Diagnostic: USB interface busy\n");
        IOService::stop(provider);
        return false;
    }

    /* FindNextPipe modifies the request; reset it before every iteration. */
    for (;;) {
        endpoint.type = kUSBAnyType;
        endpoint.direction = kUSBAnyDirn;
        endpoint.maxPacketSize = 0;
        endpoint.interval = 0;
        pipe = interface->FindNextPipe(pipe, &endpoint);
        if (!pipe) break;
        if (pipe->GetType() == kUSBBulk) {
            unsigned int packet = pipe->GetMaxPacketSize();
            if (packet < minBulkPacket) minBulkPacket = packet;
            if (packet > maxBulkPacket) maxBulkPacket = packet;
            if (pipe->GetDirection() == kUSBIn) {
                ++rxCount;
                rxPipe = pipe;
                rxAddress = 0x80 | pipe->GetEndpointNumber();
            } else {
                ++txCount;
                if (pipe->GetEndpointNumber()==2) highPipe=pipe;
                if (pipe->GetEndpointNumber()==3) normalPipe=pipe;
                if (pipe->GetEndpointNumber() < 8)
                    txMask |= 1 << pipe->GetEndpointNumber();
            }
        }
        IOLog("DWA121Diagnostic: endpoint=%u direction=%u type=%u maxPacket=%u\n",
              pipe->GetEndpointNumber(), pipe->GetDirection(),
              pipe->GetType(), pipe->GetMaxPacketSize());
    }

    if (device->GetSpeed() == kUSBDeviceSpeedFull) usbSpeed = RTL_USB_FULL_SPEED;
    else if (device->GetSpeed() == kUSBDeviceSpeedHigh) usbSpeed = RTL_USB_HIGH_SPEED;
    if (rxCount != 1 || txCount != 2 ||
        !rtl_usb_select_profile(usbSpeed, minBulkPacket, maxBulkPacket, &usbProfile)) {
        IOLog("DWA121Diagnostic: unsupported/inconsistent USB transport speed=%u bulkPackets=%u..%u RX=%u TX=%u; expected Full/64 or High/512\n",
              device->GetSpeed(), minBulkPacket, maxBulkPacket, rxCount, txCount);
        interface->close(this);
        IOService::stop(provider);
        return false;
    }
    IOLog("DWA121Diagnostic: USB link=%u Mb/s bulkPacket=%u\n",
          usbProfile.megabits, usbProfile.bulk_packet);
    bzero(&identity, sizeof(identity));
    context.device = device;
    context.firstError = kIOReturnSuccess;
    bus.cookie = &context;
    bus.read = pantherRead;
    bus.write = pantherWrite;
    bus.delay_us = pantherDelay;
    result = rtl8188cu_read_identity(&bus, &identity);
    if (result != RTL_OK) {
        interface->close(this);
        IOLog("DWA121Diagnostic: identity failed result=%d stage=%d restore=%d USB=0x%08lx\n",
              result, identity.stage, identity.restore_error, (unsigned long)context.firstError);
        IOService::stop(provider);
        return false;
    }
    config = identity.sys_cfg;
    firmwareBus.registers = bus;
    firmwareBus.write_block = pantherWriteBlock;
    if (!strcmp(rtl8188cu_firmware_name(config), "urtwn-rtl8192cU")) {
        firmware = rtl8192cU; firmwareLength = sizeof(rtl8192cU);
    } else {
        firmware = rtl8192cT; firmwareLength = sizeof(rtl8192cT);
    }
    result = rtl8188cu_boot_firmware(&firmwareBus, firmware, firmwareLength, &firmwareReport);
    if (result != RTL_OK) {
        interface->close(this);
        IOLog("DWA121Diagnostic: firmware failed result=%d stage=%d cleanup=%d bytes=%u USB=0x%08lx\n",
              result, firmwareReport.stage, firmwareReport.cleanup_error,
              firmwareReport.bytes_written, (unsigned long)context.firstError);
        IOService::stop(provider);
        return false;
    }
    IOLog("DWA121Diagnostic: firmware ready version=%u.%u bytes=%u pages=%u MCUFWDL=0x%08lx\n",
          firmwareReport.version, firmwareReport.subversion, firmwareReport.bytes_written,
          firmwareReport.pages_written, (unsigned long)firmwareReport.ready_register);
    result = rtl8188cu_radio_init(&bus, &identity, &radioReport);
    if (result != RTL_OK) {
        /* Unsupported variants can return before any radio I/O. */
        int cleanup = rtl8188cu_radio_quiesce(&bus);
        interface->close(this);
        IOLog("DWA121Diagnostic: radio failed result=%d stage=%d cleanup=%d/%d restore=%d USB=0x%08lx\n",
              result, radioReport.stage, radioReport.cleanup_error, cleanup,
              radioReport.calibration_restore_error, (unsigned long)context.firstError);
        IOLog("DWA121Diagnostic: radio channel=0x%lx/0x%lx IQ runs=%u valid=%d TX=%x,%x RX=%x,%x\n",
              (unsigned long)radioReport.rf_channel_before, (unsigned long)radioReport.rf_channel_after,
              radioReport.iq_runs, radioReport.last_iq.valid, radioReport.last_iq.tx[0],
              radioReport.last_iq.tx[1], radioReport.last_iq.rx[0], radioReport.last_iq.rx[1]);
        IOService::stop(provider);
        return false;
    }
    IOLog("DWA121Diagnostic: radio calibrated IQ runs=%u TX=%x,%x RX=%x,%x LC=0x%lx BB=0x%lx\n",
          radioReport.iq_runs, radioReport.iq.tx[0], radioReport.iq.tx[1],
          radioReport.iq.rx[0], radioReport.iq.rx[1], (unsigned long)radioReport.rf_channel_after,
          (unsigned long)radioReport.bb_rfmod);
    bzero(&dmaReport, sizeof(dmaReport));
    result = rtl8188cu_set_rx_channel(&bus, rxChannel, &tunedRFChannel);
    if (result == RTL_OK) result = rtl8188cu_dma_init(&bus, txCount, &dmaReport);
    if (result == RTL_OK && getProperty("TXPreparationProbe") == kOSBooleanTrue) {
        struct rtl_txpower power;
        struct rtl_cam_report camReport;
        bzero(&camReport, sizeof(camReport));
        /* Public synthetic key/address, never supplied by the user. No OUT
         * transfer, TXDMA disabled and TXPAUSE=ff throughout this probe. */
        static const rtl_u8 testKey[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
        static const rtl_u8 testAP[6] = {2,0x12,0x34,0x56,0x78,0x9a};
        result = rtl8188cu_txpower_compute(&identity, rxChannel, &power);
        if (result == RTL_OK) result = rtl8188cu_txpower_program(&bus, &power);
        setProperty("TXPowerReadbackVerified", result == RTL_OK);
        IOLog("DWA121Diagnostic: TX power readback result=%d\n", result);
        if (result == RTL_OK) result = pantherWrite(&context, 0x680, 2, 0);
        /* Supply protocol/scheduler/MAC/security clocks as in normal init,
         * but leave ALL four DMA enables off and every TX queue paused. */
        if (result == RTL_OK) result = pantherWrite(&context, 0x100, 2, 0x2f0);
        if (result == RTL_OK) result = rtl8188cu_cam_clear_all_report(&bus, &camReport);
        rtl_u32 camCommand=0;
        pantherRead(&context, 0x670, 4, &camCommand);
        IOLog("DWA121Diagnostic: CAM initial erase result=%d command=0x%lx\n",
              result, (unsigned long)camCommand);
        IOLog("DWA121Diagnostic: CAM detail word=%u step=%u error=%d mismatches=%u\n",
              camReport.first_word, camReport.step, camReport.error, camReport.mismatches);
        IOLog("DWA121Diagnostic: CAM masks words=0x%x entries=0x%lx control-diff=0x%x\n",
              camReport.word_mask, (unsigned long)camReport.entry_mask, camReport.control_difference);
        if (result == RTL_OK) result = rtl8188cu_cam_install_report(&bus, 1, 0, testAP, testKey, &camReport);
        IOLog("DWA121Diagnostic: CAM pairwise result=%d word=%u step=%u control-diff=0x%x\n",
              result, camReport.first_word, camReport.step, camReport.control_difference);
        for (unsigned int keyid=0; keyid<4 && result==RTL_OK; ++keyid) {
            result = rtl8188cu_cam_install_report(&bus, 0, keyid, 0, testKey, &camReport);
            IOLog("DWA121Diagnostic: CAM group %u result=%d word=%u step=%u control-diff=0x%x\n",
                  keyid, result, camReport.first_word, camReport.step, camReport.control_difference);
        }
        setProperty("CAMCCMPReadbackVerified", result == RTL_OK);
        /* Even a failed command may have reached the device. Always erase. */
        int erased = rtl8188cu_cam_clear_all(&bus);
        setProperty("CAMProbeErasureVerified", erased == RTL_OK);
        if (erased != RTL_OK) result = erased;
        if (pantherWrite(&context, 0x100, 2, 0) != RTL_OK) result = RTL_IO;
        rtl_u32 paused=0, control=0, security=0;
        int locked = pantherRead(&context, 0x522, 1, &paused) |
                     pantherRead(&context, 0x100, 2, &control) |
                     pantherRead(&context, 0x680, 2, &security);
        bool disabled = locked==0 && paused==255 && control==0 && security==0;
        setProperty("TXPreparationNoTXVerified", disabled);
        if (!disabled) result = RTL_IO;
        setProperty("TXPreparationVerified", result == RTL_OK);
        IOLog("DWA121Diagnostic: TX preparation (no transmission) result=%d erased=%d disabled=%d\n",
              result, erased, disabled);
    }
    if (result==RTL_OK && getProperty("TXUSBProbe")==kOSBooleanTrue) {
        if (!pantherTXProbe(this,highPipe,normalPipe,&bus)) result=RTL_IO;
    }
    bzero(&rxReport, sizeof(rxReport)); rxReport.drained = true;
    if (result == RTL_OK) result = rtl8188cu_rx_enable(&bus, &dmaReport);
    if (result == RTL_OK && persistent) result = rtl8188cu_rx_control_enable(&bus);
    setProperty("ControlRXFilterVerified",result==RTL_OK && persistent);
    if (persistent) {
        PantherBusContext *saved=0;
        PantherRXEngine *engine=0;
        if (result==RTL_OK) {
            saved=(PantherBusContext *)IOMalloc(sizeof(*saved));
            if (saved) engine=PantherRXEngine::create(this,rxPipe,rxChannel,identity.mac,publishLive);
            if (!saved || !engine) result=RTL_IO;
        }
        if (result!=RTL_OK) {
            if (saved) IOFree(saved,sizeof(*saved));
            rtl8188cu_rx_disable(&bus); rtl8188cu_radio_quiesce(&bus);
            interface->close(this); IOService::stop(provider); return false;
        }
        *saved=context; saved->device->retain();
        liveContext=saved; liveBus=bus; liveBus.cookie=saved;
        liveInterface=interface; liveInterface->retain(); liveRX=engine;
        liveHighPipe=highPipe; liveNormalPipe=normalPipe;
        liveIdentity=identity; liveChannel=rxChannel;
    } else {
    if (result == RTL_OK) {
        /* Keep the bounded BSS table off Panther's small kernel stack. */
        struct rtl_bss_table *table = (struct rtl_bss_table *)IOMalloc(sizeof(*table));
        if (!table) result = RTL_IO;
        else {
            result = pantherReceiveProbe(this, rxPipe, &rxReport, table, rxChannel, rxReads);
            if (result == RTL_OK && !publishBSS(this, table)) result = RTL_IO;
            IOFree(table, sizeof(*table));
        }
    }
    rtl_u32 rxPacketCount = 0, rxDMAStatus = 0, rxFIFO = 0;
    int rxStatus = pantherRead(&context, 0x284, 4, &rxPacketCount);
    rxStatus |= pantherRead(&context, 0x288, 4, &rxDMAStatus);
    rxStatus |= pantherRead(&context, 0x11c, 4, &rxFIFO);
    IOLog("DWA121Diagnostic: RX counters result=%d packets=0x%lx DMA=0x%lx FIFO=0x%lx\n",
          rxStatus, (unsigned long)rxPacketCount, (unsigned long)rxDMAStatus, (unsigned long)rxFIFO);
    const unsigned int rxCounterTypes[] = {0,2,4,6,8,11,15};
    const char *rxCounterNames[] = {"RXOFDMPhysicalCount", "RXOFDMGoodMPDUCount",
        "RXCCKPhysicalCount", "RXCCKGoodMPDUCount", "RXHTPhysicalCount",
        "RXHTGoodMPDUCount", "RXFullDropCount"};
    bool rxCountersReadable = true;
    for (unsigned int ci = 0; ci < 7; ++ci) {
        rtl_u32 count = 0;
        int cr = pantherWrite(&context, 0x664, 4, rxCounterTypes[ci] << 28);
        if (!cr) cr = pantherRead(&context, 0x664, 4, &count);
        if (cr) rxCountersReadable = false;
        else setProperty(rxCounterNames[ci], count & 0xfffff, 32);
        IOLog("DWA121Diagnostic: RX hardware counter %u = %lu status=%d\n",
              rxCounterTypes[ci], (unsigned long)(count & 0xfffff), cr);
    }
    setProperty("RXHardwareCountersReadable", rxCountersReadable);
    setProperty("RXFIFORegisterAtProbe", rxFIFO, 32);
    const UInt16 rxRegisters[] = {0x00,0x02,0x08,0x1f,0x100,0x600,0x608,0x800,0xc04,0x820,0x824};
    for (unsigned int ri = 0; ri < sizeof(rxRegisters)/sizeof(rxRegisters[0]); ++ri) {
        rtl_u32 reg = 0;
        int rr = pantherRead(&context, rxRegisters[ri], 4, &reg);
        IOLog("DWA121Diagnostic: RX register 0x%x = 0x%lx status=%d\n",
              rxRegisters[ri], (unsigned long)reg, rr);
    }
    int rxStop = rtl8188cu_rx_disable(&bus);
    if (result == RTL_OK && rxStop == RTL_OK)
        result = pantherAbortProbe(this, rxPipe, &rxReport);
    IOLog("DWA121Diagnostic: RX result=%d stop=%d LLT=%u/%u CR=0x%lx USB=%u/%u timeouts=%u aborted=%u drained=%d error=0x%08lx\n",
          result, rxStop, dmaReport.llt_written, dmaReport.llt_verified,
          (unsigned long)dmaReport.control_at_rx, rxReport.completed, rxReport.submitted,
          rxReport.timeouts, rxReport.aborted, rxReport.drained, (unsigned long)rxReport.lastError);
    IOLog("DWA121Diagnostic: RX descriptors=%u beacons=%u bytes=%u CRC=%u ICV=%u FCS=%u malformed=%u filtered=%u\n",
          rxReport.frames.descriptors, rxReport.frames.beacons, rxReport.frames.bytes,
          rxReport.frames.crc_errors, rxReport.frames.icv_errors, rxReport.frames.fcs_errors,
          rxReport.frames.malformed, rxReport.frames.filtered);
    if (result != RTL_OK || rxStop != RTL_OK) {
        rtl8188cu_radio_quiesce(&bus);
        interface->close(this);
        IOService::stop(provider);
        return false;
    }
    /* Diagnostic only: stop RF, BB and MCU before releasing USB ownership. */
    result = rtl8188cu_radio_quiesce(&bus);
    interface->close(this);
    if (result != RTL_OK) {
        IOLog("DWA121Diagnostic: firmware quiesce failed USB=0x%08lx\n", (unsigned long)context.firstError);
        IOService::stop(provider);
        return false;
    }
    }
    setProperty("PortStage", persistent?"persistent-rx-running":"passive-rx-probe-complete");
    setProperty("PersistentRXImplemented",true);
    setProperty("PersistentRXActive",persistent);
    setProperty("LLTEntriesWritten", dmaReport.llt_written, 32);
    setProperty("LLTEntriesVerified", dmaReport.llt_verified, 32);
    setProperty("RXControlAtProbe", dmaReport.control_at_rx, 32);
    setProperty("RXChannel", rxChannel, 32);
    setProperty("RXReadLimit", rxReads, 32);
    setProperty("BeaconDiscoveryImplemented", true);
    setProperty("RFChannelAtRX", tunedRFChannel, 32);
    setProperty("PassiveRXImplemented", true);
    setProperty("RXFramesVerified", rxReport.frames.beacons != 0);
    setProperty("RXRunning", persistent);
    setProperty("RXRequestsDrained", rxReport.drained);
    setProperty("RXUSBSubmitted", rxReport.submitted, 32);
    setProperty("RXUSBCompleted", rxReport.completed, 32);
    setProperty("RXUSBTimeouts", rxReport.timeouts, 32);
    setProperty("RXUSBAborted", rxReport.aborted, 32);
    setProperty("RXUSBErrors", rxReport.usbErrors, 32);
    setProperty("RXDescriptors", rxReport.frames.descriptors, 32);
    setProperty("RXBeacons", rxReport.frames.beacons, 32);
    setProperty("RXBytes", rxReport.frames.bytes, 32);
    setProperty("RXHardwareCRCErrors", rxReport.frames.crc_errors, 32);
    setProperty("RXICVErrors", rxReport.frames.icv_errors, 32);
    setProperty("RXFCSErrors", rxReport.frames.fcs_errors, 32);
    setProperty("RXMalformed", rxReport.frames.malformed, 32);
    setProperty("RXFiltered", rxReport.frames.filtered, 32);
    setProperty("RadioCalibrationVerified", true);
    setProperty("RadioRunning", persistent);
    setProperty("RadioQuiesced", !persistent);
    setProperty("IQCalibrationRuns", radioReport.iq_runs, 32);
    setProperty("IQTXBefore", radioReport.iq.tx[0], 16);
    setProperty("IQTXAfter", radioReport.iq.tx[1], 16);
    setProperty("IQRXBefore", radioReport.iq.rx[0], 16);
    setProperty("IQRXAfter", radioReport.iq.rx[1], 16);
    setProperty("LCCalibrationVerified", radioReport.lc_verified != 0);
    setProperty("RFChannelAtCalibration", radioReport.rf_channel_after, 32);
    setProperty("BasebandRFModeAtCalibration", radioReport.bb_rfmod, 32);
    setProperty("MACInitWrites", radioReport.mac_writes, 32);
    setProperty("BasebandInitWrites", radioReport.bb_writes, 32);
    setProperty("AGCInitWrites", radioReport.agc_writes, 32);
    setProperty("RFInitWrites", radioReport.rf_writes, 32);
    setProperty("FirmwareBootVerified", true);
    setProperty("FirmwareRunning", persistent);
    setProperty("FirmwareQuiesced", !persistent);
    setProperty("FirmwareReadyRegisterAtBoot", firmwareReport.ready_register, 32);
    setProperty("FirmwareVersion", firmwareReport.version, 16);
    setProperty("FirmwareSubversion", firmwareReport.subversion, 16);
    setProperty("FirmwareBytesWritten", firmwareReport.bytes_written, 32);
    setProperty("FirmwarePagesWritten", firmwareReport.pages_written, 32);
    setProperty("EfuseROM", identity.rom, sizeof(identity.rom));
    setProperty("MACAddress", identity.mac, sizeof(identity.mac));
    setProperty("ROMVendorID", identity.vid, 16);
    setProperty("ROMProductID", identity.pid, 16);
    setProperty("EfusePhysicalBytes", identity.physical_bytes, 16);
    setProperty("PASetting", identity.pa_setting, 8);
    setProperty("ChannelPlan", identity.channel_plan, 8);
    setProperty("BoardType", identity.board_type, 8);
    setProperty("Regulatory", identity.regulatory, 8);
    setProperty("ThermalMeter", identity.thermal_meter, 8);
    setProperty("EfusePowerRestored", true);
    setProperty("FirmwareRequired", rtl8188cu_firmware_name(config));
    setProperty("FirmwareLoaded", true);
    bool networking=getProperty("NetworkSessionEnabled")==kOSBooleanTrue;
    setProperty("DiagnosticOnly", !networking);
    setProperty("WiFiImplemented", networking);
    setProperty("WPA2Implemented", networking);
    setProperty("NetworkBridgeImplemented",true);
    setProperty("SYS_CFG", config, 32);
    setProperty("BulkInEndpoint", rxAddress, 8);
    setProperty("BulkOutEndpointMask", txMask, 8);
    setProperty("BulkOutCount", txCount, 32);
    setProperty("USBLinkSpeedMbps", usbProfile.megabits, 32);
    setProperty("USBBulkMaxPacket", usbProfile.bulk_packet, 32);
    setProperty("USBTransportConsistent", true);
    setProperty("ChipFamily", "RTL8188CU");
    setProperty("SiliconVendor", (config & RTL_SYS_CFG_VENDOR_UMC) ? "UMC" : "TSMC");
    IOLog("DWA121Diagnostic: RTL8188CU SYS_CFG=0x%08lx; persistent=%d; WPA2 network opt-in\n",
          (unsigned long)config,persistent);
    if (usbTerminating || isInactive() || (persistent && !liveRX->begin())) {
        stopLiveLocked(); IOService::stop(provider); return false;
    }
    registerService();
    return true;
}
