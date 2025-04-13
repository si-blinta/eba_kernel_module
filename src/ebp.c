#include "ebp.h"
/*
 * Global packet_type structure for our extended buffer protocol.
 */
struct packet_type ebp_packet_type = {
    .type = htons(EBA_ETHERTYPE), /* Custom protocol type in network byte order */
    .dev = NULL,                  /* NULL = match on all devices */
    .func = ebp_handle_packets,   /* Packet receive callback function */
};
int ebp_handle_packets(struct sk_buff *skb, struct net_device *dev,
                       struct packet_type *pt, struct net_device *orig_dev)
{
    struct ebp_header *eba_hdr;
    unsigned char *payload;
    int payload_len;
    struct ethhdr *eth;

    if (!skb)
        return NET_RX_DROP;

    /* Verify packet is long enough for an Ethernet header + EBA header */
    if (skb->len < ETH_HLEN + sizeof(struct ebp_header))
    {
        pr_err("EBP: Packet too short on device %s, length: %u\n",dev->name, skb->len);
        kfree_skb(skb);
        return NET_RX_DROP;
    }

    /* Get the Ethernet header */
    eth = eth_hdr(skb);
    pr_info("EBP: Source MAC address: %pM\n", eth->h_source);

    /* EBA header */
    payload = skb->data;
    payload_len = skb->len;

    /* debug dump*/
    print_hex_dump(KERN_INFO, "EBP: Payload: ", DUMP_PREFIX_OFFSET, 16, 1,
                   payload, payload_len, true);

    eba_hdr = (struct ebp_header *)payload;

    pr_info("EBP: Received packet on %s, protocol: 0x%04x, length: %u, EBA msgType: 0x%02x\n",dev->name, ntohs(skb->protocol), skb->len, eba_hdr->msgType);
    
    switch (eba_hdr->msgType)
    {
    case EBP_MSG_DISCOVER: /* 0x01 */
        if (skb->len < ETH_HLEN + sizeof(struct ebp_discover_req))
        {
            pr_err("EBP: Discover Request packet too short\n");
        }
        else
        {
            struct ebp_discover_req *disc = (struct ebp_discover_req *)eba_hdr;
            pr_info("EBP: Discover Request: MTU = %u\n TODO", ntohs(disc->mtu));
        }
        break;
    case EBP_MSG_DISCOVER_ACK: /* 0x03 */
        if (skb->len < ETH_HLEN + sizeof(struct ebp_discover_ack))
        {
            pr_err("EBP: Discover Ack packet too short\n");
        }
        else
        {
            struct ebp_discover_ack *ack = (struct ebp_discover_ack *)eba_hdr;
            pr_info("EBP: Discover Ack: buffer_id = 0x%llx TODO \n", be64_to_cpu(ack->buffer_id));
        }
        break;
    case EBP_MSG_INVOKE: /* 0x02 */
        if (skb->len < ETH_HLEN + sizeof(struct ebp_invoke_req))
        {
            pr_err("EBP: Invoke Request packet too short\n");
        }
        else
        {
            struct ebp_invoke_req *inv = (struct ebp_invoke_req *)eba_hdr;
            uint32_t iid = ntohl(inv->iid);
            uint32_t opid = ntohl(inv->opid);
            uint64_t args_len = be64_to_cpu(inv->args_len);

            pr_info("EBP: Invoke Request: IID = %u, OpID = %u, args_len = %llu\n",iid, opid, args_len);
            /*debug
            char *arg_data = (char *)inv->args;
            pr_info("args = %s", arg_data);*/
            int ret = ebp_invoke_op(opid, inv->args, (size_t)args_len);
            if (ret < 0) {
                pr_err("EBP: ebp_invoke_op(opid=%u) failed ret=%d\n", opid, ret);
                /* Possibly build & send an error ack or something... TODO */
            } else {
                /* Possibly build an ack for success... TODO */
            }
        
        }
        break;
    case EBP_MSG_INVOKE_ACK: /* 0x04 */
        if (skb->len < ETH_HLEN + sizeof(struct ebp_invoke_ack))
        {
            pr_err("EBP: Invoke Ack packet too short\n");
        }
        else
        {
            struct ebp_invoke_ack *inv_ack = (struct ebp_invoke_ack *)eba_hdr;
            pr_info("EBP: Invoke Ack: status = 0x%02x TODO \n ", inv_ack->status);
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
    ebp_ops_init();
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
    uint64_t i;
    for (i = 0; i < MAX_NODE_COUNT; i++)
    {
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
    uint64_t i;
    for (i = 0; i < MAX_INVOKE_COUNT; i++)
    {
        invoke_trackers[i].iid = 0;
        invoke_trackers[i].pid = 0;
        invoke_trackers[i].done = false;
        invoke_trackers[i].status = INVOKE_DEFAULT;
    }
    return 0;
}

int op_entry_array_init(void)
{
    uint64_t i;
    for (i = 0; i < MAX_OP_COUNT; i++)
    {
        op_entries[i].op_id = 0;
        op_entries[i].op_ptr = NULL;
    }
    return 0;
}
int eba_register_op(uint16_t op_id, ebp_op_handler_t fn)
{
    int i;

    /* Quick check if already registered or function is null */
    if (!fn) {
        pr_err("eba_register_op: null function pointer\n");
        return -EINVAL;
    }
    for (i = 0; i < MAX_OP_COUNT; i++) {
        if (op_entries[i].op_id == op_id) {
            pr_err("eba_register_op: op_id %u already exists\n", op_id);
            return -EEXIST; 
        }
    }
    /* Find a free entry */
    for (i = 0; i < MAX_OP_COUNT; i++) {
        if (op_entries[i].op_ptr == NULL && op_entries[i].op_id == 0) {
            op_entries[i].op_id  = op_id;
            op_entries[i].op_ptr = fn;
            pr_info("Registered op_id %u in slot %d\n", op_id, i);
            return 0;
        }
    }
    pr_err("eba_register_op: No space for new op_id %u\n", op_id);
    return -ENOSPC;
}

int ebp_invoke_op(uint32_t op_id, const void *args, size_t arg_len)
{
    int i;

    for (i = 0; i < MAX_OP_COUNT; i++) {
        if (op_entries[i].op_id == op_id && op_entries[i].op_ptr) {
            /* Found the matching operation; call it. */
            return op_entries[i].op_ptr(args, arg_len);
        }
    }
    pr_err("ebp_invoke_op: No matching op_id %u found\n", op_id);
    return -EINVAL;
}


int ebp_ops_init(void)
{
    int ret = 0;
    if (eba_register_op(EBP_OP_WRITE  ebp_op_write_handler) < 0)
        ret = -1;
    return ret;
}

char *build_discover_req_packet(uint16_t mtu, uint64_t *out_len)
{
    uint64_t len = sizeof(struct ebp_discover_req);
    struct ebp_discover_req *req = kmalloc(len, GFP_KERNEL);
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
    struct ebp_discover_ack *ack = kmalloc(len, GFP_KERNEL);
    if (!ack)
        return NULL;
    ack->header.msgType = EBP_MSG_DISCOVER_ACK;
    ack->buffer_id = cpu_to_be64(buffer_id);
    *out_len = len;
    return (char *)ack;
}

char *build_invoke_req_packet(uint32_t iid, uint32_t opid,
                              const char *args, uint64_t args_len,
                              uint64_t *out_len)
{
    uint64_t total_len = sizeof(struct ebp_invoke_req) + args_len;
    char *buf = kmalloc(total_len, GFP_KERNEL);
    if (!buf)
        return NULL;

    {
        struct ebp_invoke_req *req = (struct ebp_invoke_req *)buf;
        req->header.msgType = EBP_MSG_INVOKE;
        req->iid = htonl(iid);
        req->opid = htonl(opid);
        req->args_len = cpu_to_be64(args_len);
    }
    if (args && args_len > 0)
        memcpy(buf + fixed_len, args, args_len);
    *out_len = total_len;
    return buf;
}

char *build_invoke_ack_packet(uint8_t status, uint64_t *out_len)
{
    uint64_t len = sizeof(struct ebp_invoke_ack);
    struct ebp_invoke_ack *ack = kmalloc(len, GFP_KERNEL);
    if (!ack)
        return NULL;
    ack->header.msgType = EBP_MSG_INVOKE_ACK;
    ack->status = status;
    *out_len = len;
    return (char *)ack;
}

int ebp_op_write_handler(const void *args, size_t arg_len)
{
    if (!args || arg_len < sizeof(struct ebp_op_write_args))
        return -EINVAL;

    const struct ebp_op_write_args *wr = args;
    return eba_internals_write(wr->src, wr->buff_id, wr->offset, wr->size);

    //TODO send the response ( invoke ack)
}
