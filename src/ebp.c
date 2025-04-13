#include "ebp.h"
/*
* Global packet_type structure for our extended buffer protocol.
*/
struct packet_type ebp_packet_type = {
    .type = htons(EBA_ETHERTYPE),      /* Custom protocol type in network byte order */
    .dev  = NULL,                      /* NULL = match on all devices (or set to a specific device name) */
    .func = ebp_handle_packets,             /* Packet receive callback function */
};
int ebp_handle_packets(struct sk_buff *skb, struct net_device *dev,
    struct packet_type *pt, struct net_device *orig_dev)
{
struct eba_header *eba_hdr;
unsigned char *payload;
int payload_len;
struct ethhdr *eth;

if (!skb)
return NET_RX_DROP;

/* Verify packet is long enough for an Ethernet header + EBA header */
if (skb->len < ETH_HLEN + sizeof(struct eba_header)) {
pr_err("EBP: Packet too short on device %s, length: %u\n",
dev->name, skb->len);
kfree_skb(skb);
return NET_RX_DROP;
}

/* Get the Ethernet header */
eth = eth_hdr(skb);
pr_info("EBP: Source MAC address: %pM\n", eth->h_source);

/* The EBA header is located right after the Ethernet header */
payload = skb->data;
payload_len = skb->len;

/* Dump the payload hex for debugging */
print_hex_dump(KERN_INFO, "EBP: Payload: ", DUMP_PREFIX_OFFSET, 16, 1,
payload, payload_len, true);

eba_hdr = (struct eba_header *)payload;

pr_info("EBP: Received packet on %s, protocol: 0x%04x, length: %u, EBA msgType: 0x%02x\n",
dev->name, ntohs(skb->protocol), skb->len, eba_hdr->msgType);

switch (eba_hdr->msgType) {
case EBA_MSG_DISCOVER:  /* 0x01 */
if (skb->len < ETH_HLEN + sizeof(struct eba_discover_req)) {
pr_err("EBP: Discover Request packet too short\n");
} else {
struct eba_discover_req *disc = (struct eba_discover_req *)eba_hdr;
pr_info("EBP: Discover Request: MTU = %u\n", ntohs(disc->mtu));
}
break;
case EBA_MSG_DISCOVER_ACK:  /* 0x03 */
if (skb->len < ETH_HLEN + sizeof(struct eba_discover_ack)) {
pr_err("EBP: Discover Ack packet too short\n");
} else {
struct eba_discover_ack *ack = (struct eba_discover_ack *)eba_hdr;
pr_info("EBP: Discover Ack: buffer_id = 0x%llx\n", be64_to_cpu(ack->buffer_id));
}
break;
case EBA_MSG_INVOKE:  /* 0x02 */
if (skb->len < ETH_HLEN + sizeof(struct eba_invoke_req)) {
pr_err("EBP: Invoke Request packet too short\n");
} else {
    struct eba_invoke_req *inv = (struct eba_invoke_req *)eba_hdr;
    uint32_t iid = ntohl(inv->iid);
    uint32_t opid = ntohl(inv->opid);
    uint64_t args_len = be64_to_cpu(inv->args_len);
    
    pr_info("EBP: Invoke Request: IID = %u, OpID = %u, args_len = %llu\n",
            iid, opid, args_len);

    char *arg_data = (char *)inv->args;  
    pr_info("%s",arg_data);
}
break;
case EBA_MSG_INVOKE_ACK:  /* 0x04 */
if (skb->len < ETH_HLEN + sizeof(struct eba_invoke_ack)) {
pr_err("EBP: Invoke Ack packet too short\n");
} else {
struct eba_invoke_ack *inv_ack = (struct eba_invoke_ack *)eba_hdr;
pr_info("EBP: Invoke Ack: status = 0x%02x\n", inv_ack->status);
}
break;
default:
pr_err("EBP: Unknown EBA msgType: 0x%02x\n", eba_hdr->msgType);
break;
}

kfree_skb(skb);
return NET_RX_SUCCESS;
}


void ebp_init(void)
{
    pr_info("EBP: Initializing packet receiver for protocol 0x%04x\n", EBA_ETHERTYPE);
    dev_add_pack(&ebp_packet_type);
    pr_info("EBP: Initializing Node structures: Node info, invoke tracker, op entries \n");
    node_info_array_init();
    invoke_tracker_array_init();
    op_entry_array_init();
}
void ebp_exit(void)
{
    dev_remove_pack(&ebp_packet_type);
    pr_info("EBP: Unregistered packet receiver\n");
}

/* Global arrays for the EBA protocol data structures */
struct node_info node_infos[MAX_NODE_COUNT];
struct invoke_tracker invoke_trackers[MAX_INVOKE_COUNT];
struct op_entry op_entries[MAX_OP_COUNT];

int node_info_array_init(void)
{
    size_t i;
    for (i = 0; i < MAX_NODE_COUNT; i++) {
        node_infos[i].id = 0;
        node_infos[i].mtu = 0;
        /* Zero out the 6-byte MAC address */
        memset(node_infos[i].mac, 0, sizeof(node_infos[i].mac));
        node_infos[i].node_specs = 0;
    }
    return 0;
}

int invoke_tracker_array_init(void)
{
    size_t i;
    for (i = 0; i < MAX_INVOKE_COUNT; i++) {
        invoke_trackers[i].iid = 0;
        invoke_trackers[i].pid = 0;
        invoke_trackers[i].done = false;
        invoke_trackers[i].status = INVOKE_DEFAULT;
    }
    return 0;
}

int op_entry_array_init(void)
{
    size_t i;
    for (i = 0; i < MAX_OP_COUNT; i++) {
        op_entries[i].op_id = 0;
        op_entries[i].op_ptr = NULL;
    }
    return 0;
}

/* Build a Discover Request packet.
 * mtu: the MTU value to send.
 * out_len: returns the total packet length.
 */
char *build_discover_req_packet(uint16_t mtu, size_t *out_len)
{
    size_t len = sizeof(struct eba_discover_req);
    struct eba_discover_req *req = kmalloc(len, GFP_KERNEL);
    if (!req)
        return NULL;
    req->header.msgType = EBA_MSG_DISCOVER;
    /* Convert 16-bit value to network byte order if needed */
    req->mtu = htons(mtu);
    *out_len = len;
    return (char *)req;
}

/* Build a Discover-Ack packet.
 * buffer_id: the 64-bit identifier.
 * out_len: returns the packet length.
 */
char *build_discover_ack_packet(uint64_t buffer_id, size_t *out_len)
{
    size_t len = sizeof(struct eba_discover_ack);
    struct eba_discover_ack *ack = kmalloc(len, GFP_KERNEL);
    if (!ack)
        return NULL;
    ack->header.msgType = EBA_MSG_DISCOVER_ACK;
    /* Convert 64-bit field to network byte order */
    ack->buffer_id = cpu_to_be64(buffer_id);
    *out_len = len;
    return (char *)ack;
}

/* Build an Invoke Request packet.
 * iid: 32-bit Invocation ID.
 * opid: 32-bit Operation ID.
 * args: pointer to the argument data.
 * args_len: length in bytes of the argument data.
 * out_len: returns the packet length.
 *
 * The returned packet is built as:
 * [ fixed portion | argument data ]
 * where the fixed portion is defined by eba_invoke_req_fixed.
 */
char *build_invoke_req_packet(uint32_t iid, uint32_t opid,
                                     const char *args, uint64_t args_len,
                                     size_t *out_len)
{
    size_t fixed_len = sizeof(struct eba_invoke_req);
    size_t total_len = fixed_len + args_len;
    char *buf = kmalloc(total_len, GFP_KERNEL);
    if (!buf)
        return NULL;

    {
        struct eba_invoke_req *req = (struct eba_invoke_req *)buf;
        req->header.msgType = EBA_MSG_INVOKE;
        req->iid = htonl(iid);
        req->opid = htonl(opid);
        req->args_len = cpu_to_be64(args_len);
    }
    /* Copy the argument data right after the fixed portion */
    if (args && args_len > 0)
        memcpy(buf + fixed_len, args, args_len);
    *out_len = total_len;
    return buf;
}

/* Build an Invoke-Ack packet.
 * status: the 8-bit status code.
 * out_len: returns the packet length.
 */
char *build_invoke_ack_packet(uint8_t status, size_t *out_len)
{
    size_t len = sizeof(struct eba_invoke_ack);
    struct eba_invoke_ack *ack = kmalloc(len, GFP_KERNEL);
    if (!ack)
        return NULL;
    ack->header.msgType = EBA_MSG_INVOKE_ACK;
    ack->status = status;
    *out_len = len;
    return (char *)ack;
}
