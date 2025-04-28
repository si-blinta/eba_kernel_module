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
    
    EBA_DBG("%s: proto=0x%04x payload_len=%zu ifname=%s dst_mac=%pM\n",
        __func__, protocol, payload_len, ifname, dst_mac);
    /* 1) Lookup the net_device by name in the init_net namespace */
    dev = dev_get_by_name(&init_net, ifname);
    if (!dev)
    {
        EBA_ERR("%s: net_device \"%s\" not found\n", __func__, ifname);
        return -ENODEV;
    }
    /* 2) Total frame size = Ethernet header + payload */
    size_t packet_len = ETH_HLEN + payload_len;

    /* 3) Allocate an sk_buff with room for head + tailroom */
    skb = alloc_skb(packet_len + dev->needed_tailroom, GFP_KERNEL);
    if (!skb)
    {
        EBA_ERR("%s: alloc_skb(len=%zu) failed\n", __func__,packet_len + dev->needed_tailroom);
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
        EBA_ERR("%s: dev_queue_xmit failed, ret=%d\n", __func__, ret);
    }
    else
    {
        EBA_DBG("%s: packet sent on %s, len=%zu\n",__func__, ifname, packet_len);
    }
    /* 9) Drop our reference to the net_device */
    dev_put(dev);
    return ret;
}

int eba_net_get_max_mtu(const char *ifname)
{
    struct net_device *dev;

    if (!ifname)
    {
        EBA_ERR("%s: ifname is NULL\n", __func__);
        return -EINVAL;
    }

    /* Look up the network device by name in the initial network namespace */
    dev = dev_get_by_name(&init_net, ifname);
    if (!dev)
    {
        EBA_ERR("%s: net_device \"%s\" not found\n", __func__, ifname);
        return -ENODEV;
    }

    EBA_DBG("%s: %s MTU %d max_mtu %d\n",__func__, dev->name, dev->mtu, dev->max_mtu);

    dev_put(dev);
    return dev->max_mtu;
}

int eba_net_get_current_mtu(const char *ifname)
{
    struct net_device *dev;

    if (!ifname)
    {
        EBA_ERR("%s: ifname is NULL\n", __func__);
        return -EINVAL;
    }

    /* Look up the network device by name in the initial network namespace */
    dev = dev_get_by_name(&init_net, ifname);
    if (!dev)
    {
        EBA_ERR("%s: net_device \"%s\" not found\n", __func__, ifname);
        return -ENODEV;
    }

    EBA_DBG("EBA_NET: Device %s current MTU: %d, Maximum supported MTU: %d\n",
             dev->name, dev->mtu, dev->max_mtu);

    dev_put(dev);
    return dev->mtu;
}

int eba_net_set_mtu(const char *ifname, int new_mtu)
{
    struct net_device *dev;
    int ret = 0;

    if (!ifname)
    {
        EBA_ERR("%s: ifname is NULL\n", __func__);
        return -EINVAL;
    }

    /* Look up the network device by name */
    dev = dev_get_by_name(&init_net, ifname);
    if (!dev)
    {
        EBA_ERR("%s: net_device \"%s\" not found\n", __func__, ifname);
        return -ENODEV;
    }

    /* Check if the device's driver provides the ndo_change_mtu callback */
    if (dev->netdev_ops && dev->netdev_ops->ndo_change_mtu)
    {
        ret = dev->netdev_ops->ndo_change_mtu(dev, new_mtu);
        if (ret)
        {
            EBA_ERR("%s: change_mtu on %s -> %d failed: %d\n",__func__, dev->name, new_mtu, ret);;
        }
        else
        {
            EBA_INFO("%s: %s MTU set to %d\n", __func__, dev->name, new_mtu);
        }
    }
    else
    {
        EBA_ERR("%s: %s does not support MTU change\n",__func__, dev->name);;
        ret = -EOPNOTSUPP;
    }

    dev_put(dev);
    return ret;
}

/*========================================================================*/
/*                  Packet‑builder helpers for EBP messages               */
/*========================================================================*/


char *build_invoke_req_packet(uint32_t iid, uint32_t opid,
                              const char *args, uint64_t args_len,
                              const char *payload, uint64_t payload_len,
                              uint64_t *out_len)
{
    uint64_t total_len = sizeof(struct ebp_invoke_req) + args_len + payload_len;
    char *buf = kmalloc(total_len, GFP_ATOMIC);
    if (!buf)
    {
        EBA_ERR("%s: kmalloc(%llu) failed\n", __func__, total_len);
        return NULL;
    }

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
    EBA_DBG("%s: built invoke_req len=%llu iid=%u opid=%u args_len=%llu payload_len=%llu\n",__func__, total_len, iid, opid, args_len, payload_len);
    return buf;
}

char *build_invoke_ack_packet(uint8_t status, uint64_t data,
                              uint64_t *out_len)
{
    uint64_t len = sizeof(struct ebp_invoke_ack);
    struct ebp_invoke_ack *ack = kmalloc(len, GFP_ATOMIC);
    if (!ack)
    {
        EBA_ERR("%s: kmalloc(%llu) failed\n", __func__, len);
        return NULL;
    }

    ack->header.msgType = EBP_MSG_INVOKE_ACK;
    ack->status         = status;
    ack->data           = cpu_to_be64(data);

    *out_len = len;
    EBA_DBG("%s: built invoke_ack len=%llu status=0x%02x data=%llu\n",
            __func__, len, status, data);
    return (char *)ack;
}

/*========================================================================*/
/*                  High‑level send wrappers for each EBP message         */
/*========================================================================*/


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
        EBA_ERR("%s: build_invoke_req_packet() failed\n", __func__);
        return -ENOMEM;
    }

    ret = send_raw_ethernet_packet(packet, pkt_len, dest_mac, EBP_ETHERTYPE, ifname);
    if (ret < 0)
    {
        EBA_ERR("%s: send_raw_ethernet_packet() failed, ret=%d\n",__func__, ret);
        kfree(packet);
        return ret;
    }

    kfree(packet);
    EBA_INFO("%s: sent INVOKE iid=%u opid=%u to %pM via %s\n",__func__, iid, opid, dest_mac, ifname);

    return 0;
}

int send_invoke_ack_packet(uint8_t status, uint64_t data,
                           const unsigned char dest_mac[6],
                           const char *ifname)
{
    uint64_t pkt_len = 0;
    char *packet = build_invoke_ack_packet(status, data, &pkt_len);
    int ret;

    if (!packet)
    {
        EBA_ERR("%s: build_invoke_ack_packet() failed\n", __func__);
        return -ENOMEM;
    }

    ret = send_raw_ethernet_packet(packet, pkt_len,
                                   dest_mac, EBP_ETHERTYPE, ifname);
    if (ret < 0)
    {
        EBA_ERR("%s: send_raw_ethernet_packet() failed, ret=%d\n",
                __func__, ret);
        kfree(packet);
        return ret;
    }

    kfree(packet);
    EBA_INFO("%s: sent INVOKE_ACK status=0x%02x data=%llu to %pM via %s\n",
             __func__, status, data, dest_mac, ifname);
    return 0;
}