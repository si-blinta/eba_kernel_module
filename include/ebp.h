#ifndef EBP_H
#define EBP_H
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/if_ether.h>
#include <linux/errno.h>
#define EBA_ETHERTYPE 0xEBA0
 /**
 * ebp_handle_packets - Callback function for processing received Ethernet frames.
 * @param skb: Pointer to the socket buffer containing the packet.
 * @param dev: The network device that received the packet.
 * @param pt: Pointer to the packet_type structure.
 * @param orig_dev: The original network device (if the packet was received via bridging or similar).
 *
 * This function is called by the networking stack for each received Ethernet packet
 * that matches the registered protocol. You can inspect the packet contents (e.g., the payload,
 * Ethernet header) and process it as required.
 *
 * @returns: NET_RX_SUCCESS if the packet was processed successfully,or an appropriate drop code (e.g., NET_RX_DROP) on error.
 */
int ebp_handle_packets(struct sk_buff *skb, struct net_device *dev, struct packet_type *pt, struct net_device *orig_dev);

/**
*
* Registers our packet handler so that Ethernet frames with the specified protocol
* are delivered to our callback.
*
* @returns: 0 on success or a negative error code on failure.
*/
void ebp_init(void);
/*
* Unregisters the packet handler from the networking stack.
*/
void ebp_exit(void);
#endif