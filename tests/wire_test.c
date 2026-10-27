#include <stdio.h>
#include "../src/usb/rtl_wire.h"
#include "../src/usb/rtl_usb_profile.h"
int main(void)
{
    /* Non-palindromic and unaligned input catches PPC host-endian mistakes. */
    const unsigned char wire[] = {0x99, 0x12, 0x34, 0x56, 0x89};
    const unsigned char max[] = {0xff, 0xff, 0xff, 0xff};
    struct rtl_usb_profile profile;
    if (!rtl_usb_select_profile(RTL_USB_FULL_SPEED,64,64,&profile) ||
        profile.megabits != 12 || profile.bulk_packet != 64 ||
        !rtl_usb_select_profile(RTL_USB_HIGH_SPEED,512,512,&profile) ||
        profile.megabits != 480 || profile.bulk_packet != 512 ||
        rtl_usb_select_profile(RTL_USB_FULL_SPEED,512,512,&profile) ||
        rtl_usb_select_profile(RTL_USB_HIGH_SPEED,64,64,&profile) ||
        rtl_usb_select_profile(RTL_USB_FULL_SPEED,64,512,&profile) ||
        rtl_usb_select_profile(0,64,64,&profile) ||
        rtl_usb_select_profile(3,512,512,&profile) ||
        rtl_usb_select_profile(RTL_USB_FULL_SPEED,0,0,&profile) ||
        rtl_usb_select_profile(RTL_USB_FULL_SPEED,64,64,0)) {
        fprintf(stderr,"FAIL: USB Full/High Speed transport consistency\n");
        return 1;
    }
    if (rtl_get_le32(wire + 1) != 0x89563412UL ||
        rtl_get_le32(max) != 0xffffffffUL) {
        fprintf(stderr, "FAIL: little-endian register decoding\n");
        return 1;
    }
    puts("PASS: unaligned little-endian decoding; USB Full/64 and High/512, inconsistent transports rejected");
    return 0;
}
