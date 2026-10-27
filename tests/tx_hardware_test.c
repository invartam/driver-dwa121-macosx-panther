#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/rtl8188cu/cam.h"
#include "../src/rtl8188cu/txpower.h"
#include "../src/rtl8188cu/tx.h"
#include "../src/rtl8188cu/station_hw.h"
#include "../src/control/wpa.h"
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while(0)
struct mock {
    rtl_u32 regs[4096],cam[256],data,readback;
    unsigned int calls,fail,after,dead,busy,delay,lost,allow_station;
};
static struct mock m;
static int rd(void *c,rtl_u16 a,unsigned int w,rtl_u32 *v)
{
    struct mock *s=c; (void)w;
    ++s->calls; if(s->dead || s->calls==s->fail) return -1;
    if(a==0x670) *v=s->busy ? 0x80000000U : 0;
    else if(a==0x678) *v=s->readback;
    else { CHECK(a<4096); *v=s->regs[a]; }
    return 0;
}
static int wr(void *c,rtl_u16 a,unsigned int w,rtl_u32 v)
{
    struct mock *s=c; int fail;
    (void)w; ++s->calls; fail=s->calls==s->fail;
    if(s->dead || (fail && !s->after)) return -1;
    if(!s->allow_station)CHECK(a!=0x100 && a!=0x522 && a!=0x680);
    CHECK(a!=0x680); /* probe never enables hardware crypto */
    if(s->allow_station&&(a==0x670||a==0x674))CHECK(s->regs[0x522]==255);
    if(a==0x674) s->data=v;
    else if(a==0x670) {
        unsigned int i;
        CHECK((v&65535)<256);
        CHECK((v&7)<6); /* Reserved CAM words are not writable key RAM. */
        if(v&0x40000000U) {
            if(!s->lost) for(i=0;i<32;++i) s->cam[8*i]&=~0x8000U;
        } else if(v&0x10000) {
            if(!s->lost) s->cam[v&255]=s->data;
        } else {
            s->readback=s->cam[v&255];
            /* Measured CU behavior: reserved control bits in entries 0..3
             * read high. They are not key/valid/algorithm fields. */
            if((v&7)==0 && (v&255)<32) s->readback=(s->readback&~3U)|0x7f00;
        }
    } else { CHECK(a<4096); if(!s->lost) s->regs[a]=v; }
    return fail ? -1 : 0;
}
static void delay(void *c,unsigned int n)
{ struct mock *s=c; s->delay+=n; CHECK(n<=10); }
static struct rtl_bus b={&m,rd,wr,delay};
static const rtl_u8 ap[6]={2,0x12,0x34,0x56,0x78,0x9a};
static const rtl_u8 mac[6]={2,1,2,3,4,5};
static const rtl_u8 key[16]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
static void reset(void)
{ memset(&m,0,sizeof(m)); m.regs[0x522]=255; m.regs[0x100]=0x2f0; }
static void cam_test(void)
{
    unsigned int i,j,n,after; int e;
    struct rtl_cam_report report;
    reset(); CHECK(rtl8188cu_cam_install(&b,1,0,ap,key)==0); n=m.calls;
    CHECK(m.cam[32]==0x12028010U && m.cam[33]==0x9a785634U);
    CHECK(m.cam[34]==0x03020100U && m.cam[37]==0x0f0e0d0cU);
    CHECK(rtl8188cu_cam_clear(&b,4)==0);
    for(i=0;i<8;++i) CHECK(m.cam[32+i]==0);
    for(i=0;i<4;++i) {
        CHECK(rtl8188cu_cam_install(&b,0,i,0,key)==0);
        CHECK(m.cam[i*8]==0x8010U && m.cam[i*8+1]==0);
    }
    for(i=0;i<256;++i) m.cam[i]=0xffffffffU;
    CHECK(rtl8188cu_cam_clear_all(&b)==0);
    for(i=0;i<256;++i) CHECK(m.cam[i]==((i%8)<6 ? 0 : 0xffffffffU));
    for(after=0;after<2;++after) for(i=1;i<=n;++i) {
        reset(); m.fail=i; m.after=after;
        e=rtl8188cu_cam_install(&b,1,0,ap,key); CHECK(e!=0);
        for(j=0;j<8;++j) CHECK(m.cam[32+j]==0);
    }
    reset(); m.busy=1; CHECK(rtl8188cu_cam_install(&b,1,0,ap,key)==RTL_RESTORE);
    CHECK(m.delay==7000); /* operation + six bounded cleanup polls */
    reset(); m.dead=1; CHECK(rtl8188cu_cam_install(&b,1,0,ap,key)==RTL_IO);
    reset(); m.lost=1; CHECK(rtl8188cu_cam_install(&b,1,0,ap,key)==RTL_IO);
    reset(); m.cam[32]=0x8010; m.lost=1; CHECK(rtl8188cu_cam_clear(&b,4)==RTL_RESTORE);
    CHECK(rtl8188cu_cam_clear_all_report(&b,&report)==RTL_RESTORE);
    CHECK(report.first_word==32 && report.step==8 && report.error==RTL_IO);
    CHECK(report.mismatches==1 && report.word_mask==1 && report.entry_mask==16);
    CHECK(report.control_difference==0x8010);
    reset(); CHECK(rtl8188cu_cam_install(&b,1,1,ap,key)==RTL_BAD_ARGUMENT);
    CHECK(rtl8188cu_cam_clear(&b,32)==RTL_BAD_ARGUMENT && m.calls==0);
    m.regs[0x522]=0; CHECK(rtl8188cu_cam_install(&b,1,0,ap,key)==RTL_BAD_ARGUMENT);
    m.regs[0x522]=255; m.regs[0x680]=1;
    CHECK(rtl8188cu_cam_install(&b,1,0,ap,key)==RTL_BAD_ARGUMENT);
    m.regs[0x680]=0; m.regs[0x100]=0;
    CHECK(rtl8188cu_cam_install(&b,1,0,ap,key)==RTL_BAD_ARGUMENT);
    printf("PASS: CAM CCMP encoding, erasure, busy timeout, readback, %u injected I/O faults\n",n*2);
}
static void fixture(struct rtl_identity *id)
{
    unsigned int i;
    memset(id,0,sizeof(*id)); id->sys_cfg=0x04400735;
    id->rom[0]=0x29; id->rom[1]=0x81;
    for(i=0;i<3;++i) {
        id->rom[0x5a+i]=(rtl_u8)(20+i); id->rom[0x60+i]=(rtl_u8)(25+i);
        id->rom[0x69+i]=3; id->rom[0x6c+i]=2; id->rom[0x72+i]=5;
    }
}
static void power_test(void)
{
    static const unsigned int a[6]={0xe08,0x86c,0xe00,0xe04,0xe10,0xe14};
    struct rtl_identity id; struct rtl_txpower p;
    unsigned int i,j,n,after; rtl_u32 snapshot[6];
    fixture(&id);
    CHECK(rtl8188cu_txpower_compute(&id,11,&p)==0);
    CHECK(p.rate[0]==22 && p.rate[4]==41 && p.rate[12]==44 && p.rate[19]==32);
    id.rom[0x79]=1;
    CHECK(rtl8188cu_txpower_compute(&id,6,&p)==0 && p.rate[4]==28 && p.rate[12]==29);
    CHECK(rtl8188cu_txpower_compute(&id,11,&p)==0 && p.rate[4]==33 && p.rate[12]==30);
    id.rom[0x79]=2;
    CHECK(rtl8188cu_txpower_compute(&id,1,&p)==0 && p.rate[4]==27 && p.rate[12]==28);
    id.rom[0x79]=3;
    CHECK(rtl8188cu_txpower_compute(&id,1,&p)==0 && p.rate[4]==32 && p.rate[12]==33);
    id.rom[0x60]=63;
    CHECK(rtl8188cu_txpower_compute(&id,1,&p)==0 && p.rate[4]==63 && p.rate[12]==63);
    id.rom[0x5a]=255;
    CHECK(rtl8188cu_txpower_compute(&id,1,&p)==RTL_BAD_ROM);
    for(i=0;i<20;++i) CHECK(p.rate[i]==0);
    CHECK(rtl8188cu_txpower_compute(&id,14,&p)==RTL_BAD_ARGUMENT);
    fixture(&id); CHECK(rtl8188cu_txpower_compute(&id,11,&p)==0);
    reset(); m.regs[0xe08]=0xabcdef01U; m.regs[0x86c]=0x12345678U;
    CHECK(rtl8188cu_txpower_program(&b,&p)==0); n=m.calls;
    CHECK(m.regs[0xe08]==0xabcd1601U && m.regs[0x86c]==0x16161678U);
    CHECK(m.regs[0xe00]==0x27292929U && m.regs[0xe10]==0x282a2b2cU);
    for(after=0;after<2;++after) for(i=1;i<=n;++i) {
        reset(); for(j=0;j<6;++j) snapshot[j]=m.regs[a[j]]=0x11223344U+j;
        m.fail=i; m.after=after;
        CHECK(rtl8188cu_txpower_program(&b,&p)!=0);
        for(j=0;j<6;++j) CHECK(m.regs[a[j]]==snapshot[j]);
    }
    reset(); m.lost=1; CHECK(rtl8188cu_txpower_program(&b,&p)==RTL_IO);
    reset(); m.regs[0x100]=4;
    CHECK(rtl8188cu_txpower_program(&b,&p)==RTL_BAD_ARGUMENT);
    printf("PASS: eFuse TX power groups/regulatory modes, clamping, preservation, %u injected I/O faults\n",n*2);
}
static unsigned int get16(const rtl_u8 *p) { return p[0]|((unsigned int)p[1]<<8); }
static void tx_test(void)
{
    struct rtl_tx_packet packet; rtl_u8 f[2080],e[2048];
    unsigned int i,n,len,mps,sum;
    memset(f,0,sizeof(f)); f[0]=0xb0; f[26]=1; f[22]=0x30; f[23]=0x12;
    memcpy(f+4,ap,6); memcpy(f+10,mac,6); memcpy(f+16,ap,6);
    CHECK(rtl8188cu_tx_prepare(&packet,f,30,mac,ap,64)==0);
    CHECK(packet.length==62 && packet.endpoint==2 && !packet.needs_zlp);
    CHECK(get16(packet.bytes)==30 && packet.bytes[2]==32 && packet.bytes[3]==0x8c);
    CHECK(get16(packet.bytes+14)==0x123 && packet.bytes[16]==0 && packet.bytes[17]==1);
    CHECK(packet.bytes[5]==0x12 && packet.bytes[6]==6);
    CHECK(memcmp(packet.bytes+32,f,30)==0);
    f[1]=8; CHECK(rtl8188cu_tx_prepare(&packet,f,30,mac,ap,512)==0);
    f[1]=0x40; CHECK(rtl8188cu_tx_prepare(&packet,f,30,mac,ap,512)==RTL_BAD_ARGUMENT);
    f[1]=0; f[0]=0; f[28]=0; f[29]=1;
    CHECK(rtl8188cu_tx_prepare(&packet,f,30,mac,ap,64)==RTL_BAD_ARGUMENT);
    f[29]=0; CHECK(rtl8188cu_tx_prepare(&packet,f,30,mac,ap,64)==0);
    memset(e,0,sizeof(e)); e[0]=2; e[1]=3; e[4]=2;
    for(n=99;n<=2048;++n) {
        e[2]=(rtl_u8)((n-4)>>8); e[3]=(rtl_u8)(n-4);
        CHECK(rtl8188cu_eapol_frame(f,sizeof(f),&len,mac,ap,4095,e,n)==0);
        CHECK(len==n+32 && get16(f+22)==0xfff0);
        for(mps=64;mps<=512;mps*=8) {
            CHECK(rtl8188cu_tx_prepare(&packet,f,len,mac,ap,mps)==0);
            CHECK(packet.length==n+64 && packet.endpoint==3);
            CHECK(packet.needs_zlp==((n+64)%mps==0));
            sum=0; for(i=0;i<16;++i) sum^=get16(packet.bytes+2*i); CHECK(sum==0);
        }
    }
    CHECK(rtl8188cu_eapol_frame(f,30,&len,mac,ap,0,e,2048)==RTL_BAD_ARGUMENT && len==0);
    CHECK(rtl8188cu_eapol_frame(f,sizeof(f),&len,mac,ap,0,e,2048)==0);
    for(i=0;i<len;++i) CHECK(rtl8188cu_tx_prepare(&packet,f,i,mac,ap,64)==RTL_BAD_ARGUMENT);
    f[30]=8; f[31]=0; /* IPv4 must never pass this EAPOL-only gate */
    CHECK(rtl8188cu_tx_prepare(&packet,f,len,mac,ap,64)==RTL_BAD_ARGUMENT && packet.length==0);
    puts("PASS: auth/association/EAPOL descriptors, PPC wire encoding, checksum, 3900 USB64/512 lengths, truncation/port gate");
}
static void data_transport(void)
{
    struct dwa_wpa_request r;struct dwa_data_budget budget;struct rtl_tx_packet p;
    unsigned int n,mps,i,sum;
    memset(&r,0,sizeof(r));memset(&budget,0,sizeof(budget));
    r.version=1;r.operation=DWA_WPA_DATA_OPEN;CHECK(dwa_wpa_validate(&r)==0);
    r.operation=DWA_WPA_DATA_SEND;r.length=48;r.data[0]=8;r.data[1]=0x41;r.data[24]=1;r.data[27]=0x20;
    memcpy(r.data+4,ap,6);memcpy(r.data+10,mac,6);memset(r.data+16,255,6);
    CHECK(dwa_wpa_validate(&r)==0);
    CHECK(rtl8188cu_tx_prepare(&p,r.data,48,mac,ap,64)!=0);
    for(n=48;n<=1548;++n)for(mps=64;mps<=512;mps*=8) {
        r.length=n;CHECK(rtl8188cu_tx_prepare_ccmp(&p,r.data,n,mac,ap,mps)==0);
        CHECK(p.length==n+32&&p.endpoint==3&&p.needs_zlp==((n+32)%mps==0));
        CHECK((p.bytes[6]&0xc0)==0); /* SEC_TYPE=0: no double encryption */
        sum=0;for(i=0;i<16;++i)sum^=get16(p.bytes+2*i);CHECK(!sum);
    }
    r.length=48;CHECK(dwa_wpa_data_reserve(&budget,&r,mac,ap)==0&&budget.sent==1);
    CHECK(dwa_wpa_data_reserve(&budget,&r,mac,ap)!=0&&budget.sent==1);
    r.data[24]=0;CHECK(dwa_wpa_data_reserve(&budget,&r,mac,ap)!=0);
    r.data[24]=2;r.data[4]^=2;CHECK(dwa_wpa_data_reserve(&budget,&r,mac,ap)!=0);r.data[4]^=2;
    CHECK(dwa_wpa_data_reserve(&budget,&r,mac,ap)==0);r.data[24]=0;r.data[25]=1;
    CHECK(dwa_wpa_data_reserve(&budget,&r,mac,ap)==0&&budget.sent==3);
    r.data[24]=1;CHECK(dwa_wpa_data_reserve(&budget,&r,mac,ap)!=0);
    r.data[27]=0x60;CHECK(dwa_wpa_validate(&r)!=0);r.data[27]=0x20;
    r.data[48]=1;CHECK(dwa_wpa_validate(&r)!=0);r.data[48]=0;
    r.data[1]=1;CHECK(dwa_wpa_validate(&r)!=0);
    puts("PASS: separate software-CCMP descriptors USB64/512, cipher bits zero, no plain-path bypass, monotonic PN reservation and three-TX budget");
}
static void wpa_reset(void)
{ reset();m.allow_station=1;m.regs[0x522]=0x6f;m.regs[0x100]=0x2ff; }
static void wpa_hardware(void)
{
    unsigned int n,i,j,after,kn,pn;
    wpa_reset();m.regs[0x522]=255;m.regs[0x100]=0x2fa;
    for(i=0;i<256;++i)m.cam[i]=0xffffffffU;
    CHECK(rtl8188cu_station_hw_wpa_prepare(&b)==0);pn=m.calls;
    for(i=0;i<256;++i)CHECK(m.cam[i]==((i%8)<6?0:0xffffffffU));
    CHECK(m.regs[0x522]==255&&m.regs[0x100]==0x2fa);
    for(after=0;after<2;++after)for(i=1;i<=pn;++i) {
        wpa_reset();m.regs[0x522]=255;m.regs[0x100]=0x2fa;m.fail=i;m.after=after;
        CHECK(rtl8188cu_station_hw_wpa_prepare(&b)!=0);
        CHECK(m.regs[0x522]==255&&m.regs[0x100]==0x2fa);
    }
    wpa_reset();CHECK(rtl8188cu_station_hw_eapol(&b)==0);n=m.calls;
    CHECK(m.regs[0x522]==0x6b&&m.regs[0x102]==2&&!m.regs[0x680]);
    CHECK(rtl8188cu_station_hw_key(&b,1,0,ap,key)==0);
    CHECK(m.cam[32]==0x12028010U&&m.regs[0x522]==0x6b);
    CHECK(rtl8188cu_station_hw_key(&b,0,1,ap,key)==0);
    CHECK(rtl8188cu_station_hw_block(&b)==0);
    CHECK(rtl8188cu_station_hw_erase(&b)==0);
    for(i=0;i<5;++i)for(j=0;j<6;++j)CHECK(m.cam[i*8+j]==0);
    for(after=0;after<2;++after)for(i=1;i<=n;++i) {
        wpa_reset();m.fail=i;m.after=after;
        CHECK(rtl8188cu_station_hw_eapol(&b)!=0);
        CHECK(m.regs[0x522]==255&&m.regs[0x100]==0x2fa&&!m.regs[0x680]);
    }
    wpa_reset();m.regs[0x522]=0x6b;
    CHECK(rtl8188cu_station_hw_key(&b,1,0,ap,key)==0);kn=m.calls;
    for(after=0;after<2;++after)for(i=1;i<=kn;++i) {
        wpa_reset();m.regs[0x522]=0x6b;m.fail=i;m.after=after;
        CHECK(rtl8188cu_station_hw_key(&b,1,0,ap,key)!=0);
        CHECK(m.regs[0x522]==255&&m.regs[0x100]==0x2fa);
        CHECK(rtl8188cu_station_hw_erase(&b)==0);
        for(j=0;j<6;++j)CHECK(m.cam[32+j]==0);
    }
    wpa_reset();m.dead=1;CHECK(rtl8188cu_station_hw_eapol(&b)==RTL_RESTORE);
    CHECK(rtl8188cu_station_hw_erase(&b)==RTL_RESTORE);
    printf("PASS: WPA pre-auth CAM clear, station/BE setup, pause/readback/erasure, closed security; %u register failures\n",2*(n+kn+pn));
}
static void wpa_abi(void)
{
    struct dwa_wpa_request r;rtl_u8 e[121];unsigned int len,i;
    CHECK(sizeof(r)==2112);
    memset(&r,0,sizeof(r));r.version=1;r.operation=DWA_WPA_ENTER;r.aid=1;
    CHECK(dwa_wpa_validate(&r)==0);r.aid=2008;CHECK(dwa_wpa_validate(&r)!=0);
    r.aid=1;r.data[2079]=1;CHECK(dwa_wpa_validate(&r)!=0);
    memset(&r,0,sizeof(r));r.version=1;r.operation=DWA_WPA_SEND;
    memset(e,0,sizeof(e));e[0]=2;e[1]=3;e[3]=117;e[4]=2;e[5]=1;e[6]=10;e[98]=22;
    CHECK(rtl8188cu_eapol_frame(r.data,2080,&len,mac,ap,1,e,121)==0);r.length=len;
    CHECK(dwa_wpa_validate(&r)==0);
    for(i=0;i<len;++i){r.length=i;CHECK(dwa_wpa_validate(&r)!=0);}r.length=len;
    r.data[len]=1;CHECK(dwa_wpa_validate(&r)!=0);r.data[len]=0;
    r.data[38]|=0x80;CHECK(dwa_wpa_validate(&r)!=0);r.data[38]&=~0x80;
    r.data[38]|=0x20;CHECK(dwa_wpa_validate(&r)!=0);r.data[38]&=~0x20;
    r.data[130]++;CHECK(dwa_wpa_validate(&r)!=0);
    memset(&r,0,sizeof(r));r.version=1;r.operation=DWA_WPA_KEY;r.length=16;r.pairwise=1;
    CHECK(dwa_wpa_validate(&r)==0);r.index=1;CHECK(dwa_wpa_validate(&r)!=0);
    r.pairwise=0;CHECK(dwa_wpa_validate(&r)==0);r.rsc[6]=1;CHECK(dwa_wpa_validate(&r)!=0);
    r.rsc[6]=0;r.data[16]=1;CHECK(dwa_wpa_validate(&r)!=0);
    memset(&r,0,sizeof(r));r.version=1;r.operation=DWA_WPA_VERIFIED;
    CHECK(dwa_wpa_validate(&r)==0);r.length=16;CHECK(dwa_wpa_validate(&r)!=0);
    puts("PASS: WPA method ABI, padding, AID/key bounds, M2/M4-only admission, truncation and key-data lengths");
}
int main(void) { cam_test(); power_test(); tx_test();data_transport(); wpa_hardware();wpa_abi();return 0; }
