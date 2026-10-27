/* Included by the WPA owner. One control connection owns keys AND packets. */
#include "../control/network.h"
#include <sys/wait.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
static struct dwa_net_reply network_reply;
static unsigned int network_pulse_ms;
static char network_bsd[16];
static pid_t network_dhcp_pid;
static int network_started,network_finished;
static unsigned int network_address,network_ip_check,network_ip_wait_start;
static int network_call(unsigned int op,const unsigned char *frame,unsigned int n)
{
    struct dwa_net_request r;IOByteCount size=sizeof(network_reply);kern_return_t rc;
    if(n>DWA_NET_FRAME_MAX)return 0;
    memset(&r,0,sizeof(r));memset(&network_reply,0,sizeof(network_reply));
    r.version=DWA_NET_VERSION;r.operation=op;r.length=n;if(n)memcpy(r.data,frame,n);
    rc=IOConnectMethodStructureIStructureO(connection,3,sizeof(r),&size,&r,&network_reply);
    wipe(&r,sizeof(r));
    if(op==DWA_NET_RX&&rc==kIOReturnNoSpace)return 1; /* bounded queue drop; heartbeat verifies liveness */
    if(rc!=KERN_SUCCESS)fprintf(stderr,"Network command %u failed: 0x%08lx\n",op,(unsigned long)rc);
    return rc==KERN_SUCCESS&&size==sizeof(network_reply)&&network_reply.version==DWA_NET_VERSION&&
        network_reply.length<=DWA_NET_FRAME_MAX&&(op==DWA_NET_CLOSE||network_reply.active);
}
static int network_pulse(void)
{
    if((unsigned int)(now_ms()-network_pulse_ms)<500)return 1;
    if(!network_call(DWA_NET_PULSE,0,0))return 0;
    network_pulse_ms=now_ms();return 1;
}
static int network_name(void)
{
    io_service_t parent=0,port=0,iface=0;io_iterator_t ports=0,ifaces=0;CFTypeRef name=0;int ok=0;
    if(IOServiceGetMatchingServices(kIOMasterPortDefault,IOServiceMatching("DWA121NetworkInterface"),&ports)==KERN_SUCCESS){
        while((port=IOIteratorNext(ports))){
            int ours=0;
            if(IORegistryEntryGetParentEntry(port,kIOServicePlane,&parent)==KERN_SUCCESS){
                CFTypeRef mac=IORegistryEntryCreateCFProperty(parent,CFSTR("MACAddress"),0,0);
                ours=mac&&CFGetTypeID(mac)==CFDataGetTypeID()&&CFDataGetLength((CFDataRef)mac)==6&&
                    !memcmp(CFDataGetBytePtr((CFDataRef)mac),station.mac,6);
                if(mac)CFRelease(mac);IOObjectRelease(parent);parent=0;
            }
            if(ours&&IORegistryEntryGetChildIterator(port,kIOServicePlane,&ifaces)==KERN_SUCCESS){
                while((iface=IOIteratorNext(ifaces))){
                    if(IOObjectConformsTo(iface,"IOEthernetInterface")){
                        name=IORegistryEntryCreateCFProperty(iface,CFSTR("BSD Name"),0,0);
                        if(name&&CFGetTypeID(name)==CFStringGetTypeID())ok=CFStringGetCString((CFStringRef)name,network_bsd,sizeof(network_bsd),kCFStringEncodingASCII);
                        if(name)CFRelease(name);name=0;
                    }
                    IOObjectRelease(iface);if(ok)break;
                }
                IOObjectRelease(ifaces);ifaces=0;
            }
            IOObjectRelease(port);if(ok)break;
        }
        IOObjectRelease(ports);
    }
    if(ok){unsigned int i;if(network_bsd[0]!='e'||network_bsd[1]!='n'||!network_bsd[2])return 0;
        for(i=2;network_bsd[i];++i)if(network_bsd[i]<'0'||network_bsd[i]>'9')return 0;}
    return ok;
}
static pid_t network_ipconfig(const char *mode)
{
    pid_t pid=fork();
    if(!pid){execl("/usr/sbin/ipconfig","ipconfig","set",network_bsd,mode,(char *)0);_exit(127);}
    return pid;
}
static int network_start(void)
{
    unsigned int start=now_ms();
    if(!network_call(DWA_NET_OPEN,0,0))return -1;
    network_started=1;network_pulse_ms=now_ms();network_ip_wait_start=now_ms();
    while(!network_name()){
        if((unsigned int)(now_ms()-start)>10000||!network_pulse()){fprintf(stderr,"Cannot find the DWA121 BSD interface\n");return -1;}
        usleep(20000);
    }
    network_dhcp_pid=network_ipconfig("DHCP");if(network_dhcp_pid<0)return -1;
    printf("Network interface: %s; WPA2/CCMP active; DHCP requested\n",network_bsd);fflush(stdout);
    return 0;
}
static void network_signal(int sig,void *cookie)
{ (void)sig;(void)cookie;network_finished=1;eloop_terminate(); }
static void network_stop(void)
{
    if(network_started){
        /* Close authorization before changing BSD configuration. The native
         * stop also blocks TX, drains USB and erases CAM/session state. */
        probe_stopped=network_call(DWA_NET_CLOSE,0,0);probe_ended=1;
        if(network_bsd[0]){pid_t pid=network_ipconfig("NONE");if(pid>0)(void)waitpid(pid,0,0);}
        if(network_dhcp_pid>0)(void)waitpid(network_dhcp_pid,0,WNOHANG);
        network_started=0;
    }
}
static int network_send(const unsigned char *eth,unsigned int length)
{
    struct dwa_wpa_request r;size_t n=0;unsigned int start=now_ms(),seq=probe_sequence++&4095;
    memset(&r,0,sizeof(r));
    if(dwa_data_encrypt(probe_crypto,eth,length,r.data,sizeof(r.data),&n)){fprintf(stderr,"Network encrypt refused length=%u type=%02x%02x\n",length,eth[12],eth[13]);return -1;}
    r.data[22]=(unsigned char)(seq<<4);r.data[23]=(unsigned char)(seq>>4);
    r.operation=DWA_WPA_DATA_SEND;r.length=n;if(wpa_call(&r))return -1;
    do {
        if(!network_pulse()||!transmit(DWA_ASSOC_POLL,0,0))return -1;
        if(tx.result){if(tx.result!=RTL_TX_SUCCESS||tx.failed)return -1;++data_sends;return 0;}
        usleep(1000);
    }while((unsigned int)(now_ms()-start)<3500);
    return -1;
}
static int network_ip_ready(void)
{
    struct ifreq r;int fd;unsigned int address=0;
    if((unsigned int)(now_ms()-network_ip_check)<1000)return 0;
    network_ip_check=now_ms();memset(&r,0,sizeof(r));strncpy(r.ifr_name,network_bsd,sizeof(r.ifr_name)-1);
    fd=socket(AF_INET,SOCK_DGRAM,0);if(fd<0)return -1;
    if(ioctl(fd,SIOCGIFADDR,&r)==0){
        struct sockaddr_in *in=(struct sockaddr_in *)&r.ifr_addr;
        if(in->sin_family==AF_INET)address=ntohl(in->sin_addr.s_addr);
    }
    close(fd);
    if(address&&(address>>16)!=0xa9fe&&(address>>24)!=127){
        if(address!=network_address){printf("IPv4 ready on %s: %u.%u.%u.%u\n",network_bsd,address>>24,(address>>16)&255,(address>>8)&255,address&255);fflush(stdout);}
        network_address=address;network_ip_wait_start=now_ms();
    }else {
        if(network_address){puts("IPv4 address lost; waiting for DHCP renewal");fflush(stdout);network_address=0;}
        if((unsigned int)(now_ms()-network_ip_wait_start)>90000){
        fprintf(stderr,"No DHCP IPv4 address after 90 seconds\n");return -1;
        }
    }
    return 0;
}
static int network_tick(void)
{
    unsigned int i;
    if(!network_pulse())return -1;
    if(network_dhcp_pid>0){int code;pid_t pid=waitpid(network_dhcp_pid,&code,WNOHANG);
        if(pid>0){network_dhcp_pid=0;/* Apple bootp-110.9 S_set returns 1 even on success. Confirm
         * the assigned IPv4 address instead of mistaking this for failure. */
         if(!WIFEXITED(code)||WEXITSTATUS(code)>1){fprintf(stderr,"ipconfig could not execute: wait status=%d\n",code);return -1;}}}
    if(network_ip_ready())return -1;
    if(network_seconds&&(unsigned int)(now_ms()-data_start)>=network_seconds*1000U){network_finished=1;eloop_terminate();return 0;}
    for(i=0;i<4;++i){
        unsigned char frame[DWA_NET_FRAME_MAX];unsigned int n;int error;
        if(!network_call(DWA_NET_TX,0,0))return -1;
        n=network_reply.length;if(!n)break;
        memcpy(frame,network_reply.data,n);wipe(&network_reply,sizeof(network_reply));
        error=network_send(frame,n);wipe(frame,sizeof(frame));if(error)return -1;
    }
    return 0;
}
