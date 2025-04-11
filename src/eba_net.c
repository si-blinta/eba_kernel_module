#include "eba_net.h"

int send_raw_ethernet_packet(const unsigned char *payload, size_t payload_len,const unsigned char *dst_mac,int protocol,const char *ifname)
{
    struct net_device *dev = NULL;
    struct sk_buff *skb = NULL;
    struct ethhdr *eth;
    int ret;

    /* Look up the network device by name */
    dev = dev_get_by_name(&init_net, ifname);
    if (!dev) {
        pr_err("send_raw_ethernet_packet: Could not find device %s\n", ifname);
        return -ENODEV;
    }
    /* Calculate total length: Ethernet header + payload */
    size_t packet_len = ETH_HLEN + payload_len;

    /*
     * Allocate an sk_buff.
     */
    skb = alloc_skb(packet_len + dev->needed_tailroom, GFP_KERNEL);
    if (!skb) {
        pr_err("send_raw_ethernet_packet: Failed to allocate sk_buff\n");
        dev_put(dev);
        return -ENOMEM;
    }

    /* Reserve headroom for the Ethernet header */
    skb_reserve(skb, ETH_HLEN);

    /* Set the device for the packet */
    skb->dev = dev;
    skb->protocol = htons(protocol);
    skb->pkt_type = PACKET_OUTGOING;

    /* Build the Ethernet header */
    eth = (struct ethhdr *)skb_push(skb, ETH_HLEN);
    /* Fill in destination MAC address (provided as argument) */
    memcpy(eth->h_dest, dst_mac, ETH_ALEN);
    /* Use the device's own MAC address as source */
    memcpy(eth->h_source, dev->dev_addr, ETH_ALEN);
    /* Set the protocol field */
    eth->h_proto = htons(protocol);

    /* Append payload data to the packet */
    if (payload && payload_len > 0) {
        void *payload_ptr = skb_put(skb, payload_len);
        memcpy(payload_ptr, payload, payload_len);
    }
    /* Queue the packet for transmission */
    ret = dev_queue_xmit(skb);
    if (ret < 0) {
        pr_err("send_raw_ethernet_packet: Packet transmission failed: %d\n", ret);
        /* In case of error, the kernel should free the skb */
    } else {
        pr_info("send_raw_ethernet_packet: Packet transmitted successfully on %s\n", ifname);
    }
    /* Release the reference to the network device */
    dev_put(dev);
    return ret;
}


int eba_net_get_max_mtu(const char *ifname, int *max_mtu)
{
    struct net_device *dev;

    if (!ifname || !max_mtu)
        return -EINVAL;

    /* Look up the network device by name in the initial network namespace */
    dev = dev_get_by_name(&init_net, ifname);
    if (!dev) {
        pr_err("EBA_NET: Could not find device %s\n", ifname);
        return -ENODEV;
    }

    /* Some drivers set max_mtu in the net_device structure */
    *max_mtu = dev->max_mtu;
    pr_info("EBA_NET: Device %s current MTU: %d, Maximum supported MTU: %d\n",
            dev->name, dev->mtu, dev->max_mtu);

    dev_put(dev);
    return 0;
}

int eba_net_set_mtu(const char *ifname, int new_mtu)
{
    struct net_device *dev;
    int ret = 0;

    if (!ifname)
        return -EINVAL;

    /* Look up the network device by name */
    dev = dev_get_by_name(&init_net, ifname);
    if (!dev) {
        pr_err("EBA_NET: Could not find device %s\n", ifname);
        return -ENODEV;
    }

    /* Check if the device's driver provides the ndo_change_mtu callback */
    if (dev->netdev_ops && dev->netdev_ops->ndo_change_mtu) {
        ret = dev->netdev_ops->ndo_change_mtu(dev, new_mtu);
        if (ret) {
            pr_err("EBA_NET: Failed to change MTU on %s to %d, error: %d\n",
                   dev->name, new_mtu, ret);
        } else {
            pr_info("EBA_NET: Successfully changed MTU on %s to %d\n",
                    dev->name, new_mtu);
        }
    } else {
        pr_err("EBA_NET: Device %s does not support MTU change\n", dev->name);
        ret = -EOPNOTSUPP;
    }

    dev_put(dev);
    return ret;
}