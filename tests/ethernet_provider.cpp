/* VM-only kernel fixture. No USB access: start deliberately does not call the
 * hardware diagnostic's start(). Never include this class in a release kext. */
#include "../src/kext/DWA121Diagnostic.h"
class DWA121EthernetTestProvider : public DWA121Diagnostic {
    OSDeclareDefaultStructors(DWA121EthernetTestProvider)
public:
    virtual bool start(IOService *provider);
};
OSDefineMetaClassAndStructors(DWA121EthernetTestProvider,DWA121Diagnostic)
bool DWA121EthernetTestProvider::start(IOService *provider)
{
    unsigned char mac[]={0x02,0x44,0x57,0x41,0x12,0x01};
    if(!IOService::start(provider))return false;
    setProperty("MACAddress",mac,sizeof(mac));
    setProperty("PersistentRXActive",true);
    setProperty("SyntheticEthernetProvider",true);
    registerService();return true;
}
