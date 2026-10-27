#ifndef DWA121_DHCP_PROBE_H
#define DWA121_DHCP_PROBE_H
#include "../rtl8188cu/identity.h"
#define DWA_DHCP_DISCOVER_SIZE 342U
struct dwa_dhcp_offer { rtl_u8 address[4],server[4]; };
/* Bounded DHCP discovery only (RFC2131/2132). No REQUEST/lease/configuration.
 * xid is four random bytes. All wire fields are explicitly big endian. */
int dwa_dhcp_discover(rtl_u8 *,unsigned int,const rtl_u8 mac[6],
    const rtl_u8 xid[4],unsigned int seconds);
/* Input is decrypted/authenticated Ethernet, without FCS. Checks IP/UDP
 * checksums (UDP zero allowed for IPv4), lengths, ports, xid, chaddr, message
 * type and server identifier. No fragmentation, option overload or IP options.
 * Output cleared on failure; ignores other traffic without retaining bytes. */
int dwa_dhcp_parse_offer(const rtl_u8 *,unsigned int,const rtl_u8 mac[6],
    const rtl_u8 xid[4],struct dwa_dhcp_offer *);
#endif
