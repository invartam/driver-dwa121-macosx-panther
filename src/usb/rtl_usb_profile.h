#ifndef RTL_USB_PROFILE_H
#define RTL_USB_PROFILE_H
/* RTL8188CU bulk endpoints: 64 bytes at Full Speed, 512 at High Speed.
 * Do not confuse a guest controller's speed with the physical USB mode.
 * The caller maps its platform's speed enumeration to these constants.
 */
#define RTL_USB_FULL_SPEED 1U
#define RTL_USB_HIGH_SPEED 2U
struct rtl_usb_profile {
    unsigned int megabits, bulk_packet;
};
static int rtl_usb_select_profile(unsigned int speed, unsigned int minimum,
                                  unsigned int maximum, struct rtl_usb_profile *p)
{
    unsigned int packet, rate;
    if (!p) return 0;
    p->megabits = p->bulk_packet = 0;
    if (speed == RTL_USB_FULL_SPEED) { packet = 64; rate = 12; }
    else if (speed == RTL_USB_HIGH_SPEED) { packet = 512; rate = 480; }
    else return 0;
    if (minimum != packet || maximum != packet) return 0;
    p->megabits = rate; p->bulk_packet = packet;
    return 1;
}
#endif
