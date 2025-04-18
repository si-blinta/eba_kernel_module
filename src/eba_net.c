#include "eba_net.h"
#include "eba.h"

int send_raw_ethernet_packet(const unsigned char *payload,
                             size_t payload_len,
                             const unsigned char *dst_mac,
                             int protocol, const char *ifname)
{
    struct net_device *dev = NULL;
    struct sk_buff *skb = NULL;
    struct ethhdr *eth;
    int ret;

    /* 1) Lookup the net_device by name in the init_net namespace */
    dev = dev_get_by_name(&init_net, ifname);
    if (!dev)
    {
        EBA_ERR("send_raw_ethernet_packet: Could not find device %s\n", ifname);
        return -ENODEV;
    }
    /* 2) Total frame size = Ethernet header + payload */
    size_t packet_len = ETH_HLEN + payload_len;

    /* 3) Allocate an sk_buff with room for head + tailroom */
    skb = alloc_skb(packet_len + dev->needed_tailroom, GFP_KERNEL);
    if (!skb)
    {
        EBA_ERR("send_raw_ethernet_packet: Failed to allocate sk_buff\n");
        dev_put(dev);
        return -ENOMEM;
    }

    /* 4) Reserve space for the Ethernet header */
    skb_reserve(skb, ETH_HLEN);

    /* 5) Set SKB metadata */
    skb->dev = dev;
    skb->protocol = htons(protocol);
    skb->pkt_type = PACKET_OUTGOING;

    /* 6) Push down pointer to make room for ethhdr, then fill it */
    eth = (struct ethhdr *)skb_push(skb, ETH_HLEN);
    /* Fill in destination MAC address (provided as argument) */
    memcpy(eth->h_dest, dst_mac, ETH_ALEN);
    /* Use the device's own MAC address as source */
    memcpy(eth->h_source, dev->dev_addr, ETH_ALEN);
    /* Set the protocol field */
    eth->h_proto = htons(protocol);

    /* 7) Append payload data to the packet */
    if (payload && payload_len > 0)
    {
        void *payload_ptr = skb_put(skb, payload_len);
        memcpy(payload_ptr, payload, payload_len);
    }

    /* 8) Transmit — skb will be freed by network stack on error or success */
    ret = dev_queue_xmit(skb);
    if (ret < 0)
    {
        EBA_ERR("send_raw_ethernet_packet: Packet transmission failed: %d\n", ret);
    }
    else
    {
        EBA_INFO("send_raw_ethernet_packet: Packet transmitted successfully on %s\n", ifname);
    }
    /* 9) Drop our reference to the net_device */
    dev_put(dev);
    return ret;
}

int eba_net_get_max_mtu(const char *ifname)
{
    struct net_device *dev;

    if (!ifname)
        return -EINVAL;

    /* Look up the network device by name in the initial network namespace */
    dev = dev_get_by_name(&init_net, ifname);
    if (!dev)
    {
        EBA_ERR("EBA_NET: Could not find device %s\n", ifname);
        return -ENODEV;
    }

    EBA_INFO("EBA_NET: Device %s current MTU: %d, Maximum supported MTU: %d\n",
             dev->name, dev->mtu, dev->max_mtu);

    dev_put(dev);
    return dev->max_mtu;
}

int eba_net_get_current_mtu(const char *ifname)
{
    struct net_device *dev;

    if (!ifname)
        return -EINVAL;

    /* Look up the network device by name in the initial network namespace */
    dev = dev_get_by_name(&init_net, ifname);
    if (!dev)
    {
        EBA_ERR("EBA_NET: Could not find device %s\n", ifname);
        return -ENODEV;
    }

    EBA_INFO("EBA_NET: Device %s current MTU: %d, Maximum supported MTU: %d\n",
             dev->name, dev->mtu, dev->max_mtu);

    dev_put(dev);
    return dev->mtu;
}

int eba_net_set_mtu(const char *ifname, int new_mtu)
{
    struct net_device *dev;
    int ret = 0;

    if (!ifname)
        return -EINVAL;

    /* Look up the network device by name */
    dev = dev_get_by_name(&init_net, ifname);
    if (!dev)
    {
        EBA_ERR("EBA_NET: Could not find device %s\n", ifname);
        return -ENODEV;
    }

    /* Check if the device's driver provides the ndo_change_mtu callback */
    if (dev->netdev_ops && dev->netdev_ops->ndo_change_mtu)
    {
        ret = dev->netdev_ops->ndo_change_mtu(dev, new_mtu);
        if (ret)
        {
            EBA_ERR("EBA_NET: Failed to change MTU on %s to %d, error: %d\n",
                    dev->name, new_mtu, ret);
        }
        else
        {
            EBA_INFO("EBA_NET: Successfully changed MTU on %s to %d\n",
                     dev->name, new_mtu);
        }
    }
    else
    {
        EBA_ERR("EBA_NET: Device %s does not support MTU change\n", dev->name);
        ret = -EOPNOTSUPP;
    }

    dev_put(dev);
    return ret;
}

/*========================================================================*/
/*                  Packet‑builder helpers for EBP messages               */
/*========================================================================*/

char *build_discover_req_packet(uint16_t mtu, uint64_t *out_len)
{
    uint64_t len = sizeof(struct ebp_discover_req);
    struct ebp_discover_req *req = kmalloc(len, GFP_ATOMIC);
    if (!req)
        return NULL;
    req->header.msgType = EBP_MSG_DISCOVER;
    req->mtu = htons(mtu);
    *out_len = len;
    return (char *)req;
}

char *build_discover_ack_packet(uint64_t buffer_id, uint64_t *out_len)
{
    uint64_t len = sizeof(struct ebp_discover_ack);
    struct ebp_discover_ack *ack = kmalloc(len, GFP_ATOMIC);
    if (!ack)
        return NULL;
    ack->header.msgType = EBP_MSG_DISCOVER_ACK;
    ack->buffer_id = cpu_to_be64(buffer_id);
    *out_len = len;
    return (char *)ack;
}

char *build_invoke_req_packet(uint32_t iid, uint32_t opid,
                              const char *args, uint64_t args_len,
                              const char *payload, uint64_t payload_len,
                              uint64_t *out_len)
{
    uint64_t total_len = sizeof(struct ebp_invoke_req) + args_len + payload_len;
    char *buf = kmalloc(total_len, GFP_ATOMIC);
    if (!buf)
        return NULL;

    struct ebp_invoke_req *req = (struct ebp_invoke_req *)buf;
    req->header.msgType = EBP_MSG_INVOKE;
    req->iid = htonl(iid);
    req->opid = htonl(opid);
    /* Set args_len as the sum of the header length and the payload length */
    req->args_len = cpu_to_be64(args_len + payload_len);

    /* Copy args then payload into buf */

    if (args && args_len > 0)
        memcpy(buf + sizeof(struct ebp_invoke_req), args, args_len);

    if (payload && payload_len > 0)
        memcpy(buf + sizeof(struct ebp_invoke_req) + args_len, payload, payload_len);

    *out_len = total_len;
    return buf;
}

char *build_invoke_ack_packet(uint8_t status, uint64_t *out_len)
{
    uint64_t len = sizeof(struct ebp_invoke_ack);
    struct ebp_invoke_ack *ack = kmalloc(len, GFP_ATOMIC);
    if (!ack)
        return NULL;

    ack->header.msgType = EBP_MSG_INVOKE_ACK;
    ack->status = status;
    *out_len = len;

    return (char *)ack;
}

/*========================================================================*/
/*                  High‑level send wrappers for each EBP message         */
/*========================================================================*/

int send_discover_req_packet(uint16_t mtu, const unsigned char dest_mac[6],
                             const char *ifname)
{
    uint64_t pkt_len = 0;
    char *packet = build_discover_req_packet(mtu, &pkt_len);
    int ret;

    if (!packet)
    {
        EBA_ERR("send_discover_req_packet: build_discover_req_packet() failed.\n");
        return -ENOMEM;
    }

    ret = send_raw_ethernet_packet(packet, pkt_len, dest_mac, EBP_ETHERTYPE, ifname);
    if (ret < 0)
    {
        EBA_ERR("send_discover_req_packet: send_raw_ethernet_packet failed, ret = %d\n", ret);
        kfree(packet);
        return ret;
    }

    kfree(packet);
    EBA_INFO("send_discover_req_packet: Sent EBP_MSG_DISCOVER to %pM via %s (MTU = %u)\n",
             dest_mac, ifname, mtu);

    return 0;
}

int send_discover_ack_packet(uint64_t buffer_id, const unsigned char dest_mac[6], const char *ifname)
{
    uint64_t pkt_len = 0;
    char *packet = build_discover_ack_packet(buffer_id, &pkt_len);
    int ret;

    if (!packet)
    {
        EBA_ERR("send_discover_ack_packet: build_discover_ack_packet() failed.\n");
        return -ENOMEM;
    }

    ret = send_raw_ethernet_packet(packet, pkt_len, dest_mac, EBP_ETHERTYPE, ifname);
    if (ret < 0)
    {
        EBA_ERR("send_discover_ack_packet: send_raw_ethernet_packet failed, ret = %d\n", ret);
        kfree(packet);
        return ret;
    }

    kfree(packet);
    EBA_INFO("send_discover_ack_packet: Sent EBP_MSG_DISCOVER_ACK to %pM via %s (buffer_id=%llu)\n",
             dest_mac, ifname, buffer_id);

    return 0;
}

int send_invoke_req_packet(uint32_t iid, uint32_t opid,
                           const char *args, uint64_t args_len,
                           const void *payload, uint64_t payload_len,
                           const unsigned char dest_mac[6],
                           const char *ifname)
{
    uint64_t pkt_len = 0;
    char *packet = build_invoke_req_packet(iid, opid,
                                           args, args_len,
                                           payload, payload_len,
                                           &pkt_len);
    int ret;

    if (!packet)
    {
        EBA_ERR("send_invoke_req_packet: build_invoke_req_packet() failed.\n");
        return -ENOMEM;
    }

    ret = send_raw_ethernet_packet(packet, pkt_len, dest_mac, EBP_ETHERTYPE, ifname);
    if (ret < 0)
    {
        EBA_ERR("send_invoke_req_packet: send_raw_ethernet_packet failed, ret = %d\n", ret);
        kfree(packet);
        return ret;
    }

    kfree(packet);
    EBA_INFO("send_invoke_req_packet: Sent EBP_MSG_INVOKE (IID=%u, OPID=%u) to %pM via %s\n",
             iid, opid, dest_mac, ifname);

    return 0;
}

int send_invoke_ack_packet(uint8_t status,
                           const unsigned char dest_mac[6],
                           const char *ifname)
{
    uint64_t pkt_len = 0;
    char *packet = build_invoke_ack_packet(status, &pkt_len);
    int ret;

    if (!packet)
    {
        EBA_ERR("send_invoke_ack_packet: build_invoke_ack_packet() failed.\n");
        return -ENOMEM;
    }

    ret = send_raw_ethernet_packet(packet, pkt_len, dest_mac, EBP_ETHERTYPE, ifname);
    if (ret < 0)
    {
        EBA_ERR("send_invoke_ack_packet: send_raw_ethernet_packet failed, ret = %d\n", ret);
        kfree(packet);
        return ret;
    }

    kfree(packet);
    EBA_INFO("send_invoke_ack_packet: Sent EBP_MSG_INVOKE_ACK (status=0x%02x) to %pM via %s\n",
             status, dest_mac, ifname);

    return 0;
}