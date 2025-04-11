#ifndef EBA_NET
#define EBA_NET
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/if_ether.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/string.h>
/**
 * send_raw_ethernet_packet - Send a raw Ethernet packet.
 * @param payload:    Pointer to the payload data to include in the packet.
 * @param payload_len: Length of the payload in bytes.
 * @param dst_mac:    Pointer to the destination MAC address (typically a 6-byte array).
 * @param protocol:   Ethernet protocol type (e.g., ETH_P_IP, ETH_P_ARP) in host byte order.
 * @param ifname:     Network interface name (e.g., "eth0") through which the packet should be transmitted.
 *
 * This function allocates an sk_buff, constructs the Ethernet header including setting the 
 * destination and source MAC addresses and the specified protocol, appends the provided payload, 
 * and then transmits the packet using the appropriate network device. On success, the packet is 
 * handed to the networking stack for transmission.
 *
 * @returns: 0 on success, or a negative error code on failure.
 */
int send_raw_ethernet_packet(const unsigned char *payload, size_t payload_len,const unsigned char *dst_mac,int protocol,const char *ifname);

/**
 * eba_net_get_max_mtu - Retrieves the maximum MTU supported by the specified network device.
 * @param ifname: The network device name (e.g., "eth0").
 * @param max_mtu: Pointer to an integer where the maximum MTU value will be stored.
 *
 * @returns 0 on success or a negative error code on failure.
 */
int eba_net_get_max_mtu(const char *ifname, int *max_mtu);

/**
 * eba_net_set_mtu - Changes the MTU of the specified network device.
 * @param ifname: The network device name (e.g., "eth0").
 * @param new_mtu: The new MTU value to set.
 *
 * @returns 0 on success or a negative error code on failure.
 */
int eba_net_set_mtu(const char *ifname, int new_mtu);
#endif