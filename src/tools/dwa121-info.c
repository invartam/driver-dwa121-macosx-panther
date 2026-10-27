/* Native Panther diagnostic reporter. No network credentials or configuration. */
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <stdio.h>

int main(void)
{
    io_iterator_t iterator = 0;
    io_service_t service;
    CFMutableDictionaryRef properties = 0;
    CFDataRef xml;
    kern_return_t result;
    int found = 0, failed = 0;
    result = IOServiceGetMatchingServices(kIOMasterPortDefault,
                IOServiceMatching("DWA121Diagnostic"), &iterator);
    if (result != KERN_SUCCESS) {
        fprintf(stderr, "IOServiceGetMatchingServices: 0x%08lx\n", (unsigned long)result);
        return 1;
    }
    while ((service = IOIteratorNext(iterator))) {
        found = 1;
        result = IORegistryEntryCreateCFProperties(service, &properties,
                                                   kCFAllocatorDefault, 0);
        IOObjectRelease(service);
        if (result != KERN_SUCCESS) {
            fprintf(stderr, "IORegistryEntryCreateCFProperties: 0x%08lx\n", (unsigned long)result);
            failed = 1;
            continue;
        }
        xml = CFPropertyListCreateXMLData(kCFAllocatorDefault, properties);
        CFRelease(properties);
        if (!xml) {
            failed = 1;
            continue;
        }
        if (fwrite(CFDataGetBytePtr(xml), 1, (size_t)CFDataGetLength(xml), stdout)
            != (size_t)CFDataGetLength(xml)) failed = 1;
        CFRelease(xml);
    }
    IOObjectRelease(iterator);
    if (!found) {
        fprintf(stderr, "No DWA121Diagnostic service: connect the DWA-121 A1 and load the diagnostic kext.\n");
        return 2;
    }
    return failed ? 1 : 0;
}
