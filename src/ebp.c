#include "ebp.h"
/*
* Global packet_type structure for our extended buffer protocol.
*/
struct packet_type ebp_packet_type = {
    .type = htons(EBA_ETHERTYPE),      /* Custom protocol type in network byte order */
    .dev  = NULL,                      /* NULL = match on all devices (or set to a specific device name) */
    .func = ebp_handle_packets,             /* Packet receive callback function */
};
int ebp_handle_packets(struct sk_buff *skb, struct net_device *dev, struct packet_type *pt, struct net_device *orig_dev)
{
    if (!skb)
        return NET_RX_DROP;

    /* Log packet details for debugging */
    pr_info("EBP: Received packet on device %s, protocol: 0x%04x, length: %u\n",
    dev->name, ntohs(skb->protocol), skb->len);

    kfree_skb(skb);
    return NET_RX_SUCCESS;
}
void ebp_init(void)
{
    pr_info("EBP: Initializing packet receiver for protocol 0x%04x\n", EBA_ETHERTYPE);
    dev_add_pack(&ebp_packet_type);
}
void ebp_exit(void)
{
    dev_remove_pack(&ebp_packet_type);
    pr_info("EBP: Unregistered packet receiver\n");
}