/* Offline native Panther result checker; no dependency on Python or Xcode. */
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static int errors;
static void problem(const char *key)
{ fprintf(stderr,"FAIL: %s\n",key);++errors; }
static int boolean(CFDictionaryRef d,const char *name,int wanted)
{
    CFStringRef key=CFStringCreateWithCString(0,name,kCFStringEncodingASCII);
    CFTypeRef v=CFDictionaryGetValue(d,key);int good;
    good=v && CFGetTypeID(v)==CFBooleanGetTypeID() && CFBooleanGetValue((CFBooleanRef)v)==wanted;
    CFRelease(key);if(!good)problem(name);return good;
}
static long number(CFDictionaryRef d,const char *name)
{
    CFStringRef key=CFStringCreateWithCString(0,name,kCFStringEncodingASCII);
    CFTypeRef v=CFDictionaryGetValue(d,key);long n=-1;
    if(!v || CFGetTypeID(v)!=CFNumberGetTypeID() || !CFNumberGetValue((CFNumberRef)v,kCFNumberLongType,&n) || n<0)problem(name);
    CFRelease(key);return n;
}
static void equal(CFDictionaryRef d,const char *a,const char *b)
{ long x=number(d,a),y=number(d,b);if(x<0||x!=y)problem(a); }
int main(int argc,char **argv)
{
    FILE *f;long length,speed,packet;unsigned char *buffer;CFDataRef data;
    CFPropertyListRef plist;CFStringRef error=0;CFDictionaryRef d;
    int running,rx,association,wpa,ccmp,usb11;
    if(argc<3||argc>4|| (strcmp(argv[2],"running")&&strcmp(argv[2],"stopped")&&strcmp(argv[2],"rx")&&strcmp(argv[2],"association")&&strcmp(argv[2],"wpa")&&strcmp(argv[2],"data")) ||
       (argc==4&&strcmp(argv[3],"--usb11"))) {
        fprintf(stderr,"Usage: dwa121-check FILE running|stopped|rx|association|wpa|data [--usb11]\n");return 1;
    }
    running=!strcmp(argv[2],"running");rx=!strcmp(argv[2],"rx");ccmp=!strcmp(argv[2],"data");wpa=ccmp||!strcmp(argv[2],"wpa");association=wpa||!strcmp(argv[2],"association");usb11=argc==4;
    f=fopen(argv[1],"rb");if(!f){perror(argv[1]);return 1;}
    if(fseek(f,0,SEEK_END)||(length=ftell(f))<=0||length>1048576||fseek(f,0,SEEK_SET)){fclose(f);return 1;}
    buffer=(unsigned char *)malloc((size_t)length);if(!buffer){fclose(f);return 1;}
    if(fread(buffer,1,(size_t)length,f)!=(size_t)length){fclose(f);free(buffer);return 1;}fclose(f);
    data=CFDataCreate(0,buffer,length);free(buffer);if(!data)return 1;
    plist=CFPropertyListCreateFromXMLData(0,data,kCFPropertyListImmutable,&error);CFRelease(data);
    if(error)CFRelease(error);
    if(!plist)return 1;
    if(CFGetTypeID(plist)!=CFDictionaryGetTypeID()){CFRelease(plist);return 1;}d=(CFDictionaryRef)plist;
    boolean(d,"USBTransportConsistent",1);boolean(d,"WiFiImplemented",0);boolean(d,"WPA2Implemented",0);
    speed=number(d,"USBLinkSpeedMbps");packet=number(d,"USBBulkMaxPacket");
    if(!((speed==12&&packet==64)||(speed==480&&packet==512)))problem("USB speed/packet combination");
    if(usb11&&speed!=12)problem("USB 1.1 expected: link must be 12 Mb/s with 64-byte bulk packets");
    boolean(d,"RXRunning",running);
    if(!running) {
        boolean(d,"PersistentStopVerified",1);boolean(d,"RXHardwareStopVerified",1);
        boolean(d,"RXRequestsDrained",1);boolean(d,"RadioQuiesced",1);boolean(d,"FirmwareQuiesced",1);
        boolean(d,"PersistentRXActive",0);boolean(d,"RadioRunning",0);boolean(d,"FirmwareRunning",0);
        equal(d,"RXUSBSubmitted","RXUSBCompleted");
    }
    if(rx||association) {
        if(number(d,"RXBeacons")<1)problem("No verified beacon");
        if(number(d,"RXUSBErrors")!=0)problem("RXUSBErrors");
        if(number(d,"RXWatchdogs")!=0)problem("RXWatchdogs");
        if(number(d,"RXSubmissionsRejected")!=0)problem("RXSubmissionsRejected");
        if(number(d,"RXMalformed")!=0)problem("RXMalformed");
    }
    if(association) {
        boolean(d,"AssociationTXConfigured",1);boolean(d,"AssociationTXBlocked",1);boolean(d,"AssociationTXDrained",1);
        equal(d,"AssociationTXSubmitted","AssociationTXCompleted");
        if(number(d,"AssociationTXSubmitted")<2)problem("Fewer than two TX requests");
        if(number(d,"AssociationTXErrors")!=0)problem("AssociationTXErrors");
        if(number(d,"AssociationTXWatchdogs")!=0)problem("AssociationTXWatchdogs");
        if(number(d,"RXEAPOL")<1)problem("No EAPOL frame");
    }
    if(wpa) {
        long mask=number(d,"WPA2KeysInstalledMask");
        boolean(d,"WPA2HandshakeReported",1);boolean(d,"WPA2KeysErased",1);
        boolean(d,"WPA2ProbeActive",0);boolean(d,"WPA2DataPortAuthorized",0);
        if(mask<0||!(mask&16)||!(mask&15))problem("Missing pairwise/group key installation");
        if(number(d,"WPA2EAPOLSubmitted")<2)problem("Missing M2/M4 transmission");
    }
    if(ccmp) {
        long sent=number(d,"CCMPDataSubmitted");
        boolean(d,"CCMPDHCPOfferReported",1);boolean(d,"CCMPSoftwareCrypto",1);boolean(d,"CCMPDataProbeActive",0);
        if(sent<1||sent>3)problem("CCMP TX budget");
        if(number(d,"RXCCMP")<1)problem("No encrypted RX frame");
    }
    CFRelease(plist);
    printf("%s: %s; USB=%ld Mb/s bulk=%ld; errors=%d\n",errors?"FAIL":"PASS",argv[2],speed,packet,errors);
    return errors?1:0;
}
