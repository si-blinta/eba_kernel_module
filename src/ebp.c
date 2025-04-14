#include "ebp.h"
#include "eba_internals.h"
#include "eba_net.h"
/*
 * Global packet_type structure for our extended buffer protocol.
 */
struct packet_type ebp_packet_type = {
    .type = htons(EBP_ETHERTYPE), /* Custom protocol type in network byte order */
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
            int ret = ebp_invoke_op(opid, inv->args, args_len);
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
    pr_info("EBP: Initializing packet receiver for protocol EBPETHERTYPE\n");
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
void print_op_entries(void)
{
    int i;
    pr_info("=== op_entries Array Dump ===\n");
    for (i = 0; i < MAX_OP_COUNT; i++) {
        pr_info("Slot %d: op_id = %u, op_ptr = %p\n", 
                 i, op_entries[i].op_id, op_entries[i].op_ptr);
    }
    pr_info("=============================\n");
}
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
int ebp_register_op(uint32_t op_id, ebp_op_handler_t fn)
{
    int i;

    if (!fn) {
        pr_err("eba_register_op: null function pointer\n");
        return -EINVAL;
    }
    
    /* Check if this op_id is already registered */
    for (i = 0; i < MAX_OP_COUNT; i++) {
        if (op_entries[i].op_id == op_id) {
            pr_err("eba_register_op: op_id %u already exists (slot %d)\n", op_id, i);
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


int ebp_invoke_op(uint32_t op_id, const void *args, uint64_t arg_len)
{
    int i;
    for (i = 0; i < MAX_OP_COUNT; i++) {
        if (op_entries[i].op_id == op_id) {
            pr_info("Found op_id %u in slot %d, calling handler...\n", op_id, i);
            return op_entries[i].op_ptr(args, arg_len);
        }
    }
    pr_err("ebp_invoke_op: No matching op_id %u found\n", op_id);
    return -EINVAL;
}



int ebp_ops_init(void)
{
    int ret = 0;
    if (ebp_register_op(EBP_OP_ALLOC,ebp_op_alloc_handler) < 0)
        ret = -1;
    if (ebp_register_op(EBP_OP_WRITE,ebp_op_write_handler) < 0)
        ret = -1;
    if (ebp_register_op(EBP_OP_READ,ebp_op_read_handler) < 0)
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
    const char *payload, uint64_t payload_len,
    uint64_t *out_len)
{
    uint64_t total_len = sizeof(struct ebp_invoke_req) + args_len + payload_len;
    char *buf = kmalloc(total_len, GFP_KERNEL);
    if (!buf)
        return NULL;

    struct ebp_invoke_req *req = (struct ebp_invoke_req *)buf;
    req->header.msgType = EBP_MSG_INVOKE;
    req->iid = htonl(iid);
    req->opid = htonl(opid);
    /* Set args_len as the sum of the write header length and the payload length */
    req->args_len = cpu_to_be64(args_len + payload_len);
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
    struct ebp_invoke_ack *ack = kmalloc(len, GFP_KERNEL);
    if (!ack)
        return NULL;
    ack->header.msgType = EBP_MSG_INVOKE_ACK;
    ack->status = status;
    *out_len = len;
    return (char *)ack;
}
int ebp_op_write_handler(const void *args, uint64_t arg_len)
{
    /* Check that args is not NULL and that its is large enough for our fixed-size header */
    if (!args || arg_len < sizeof(struct ebp_op_write_args))
    {
        pr_err("ebp_op_write_handler: invalid arg_len %llu (must be >= %zu) or null pointer %p\n",arg_len, sizeof(struct ebp_op_write_args), args);
        return -EINVAL;
    }

    const struct ebp_op_write_args *wr = args;
    uint64_t header_size = sizeof(struct ebp_op_write_args);

    if (arg_len < header_size + wr->size)
    {
        pr_err("ebp_op_write_handler: args too short for payload data: arg_len = %llu, header_size = %llu, expected payload size = %llu\n",
               arg_len, header_size, wr->size);
        return -EINVAL;
    }

    const uint8_t *payload = (const uint8_t *)args + header_size;

    pr_info("ebp_op_write_handler: buff_id = %llx offset = %llu size = %llu\n",wr->buff_id, wr->offset, wr->size);
     // TODO here inqueue to the invoke queue and send an ack
    print_hex_dump(KERN_INFO, "ebp_op_write_handler payload :", DUMP_PREFIX_OFFSET, 16, 1,payload, wr->size, true);
    uint64_t len = 0;
    char* packet =  build_invoke_ack_packet(INVOKE_QUEUED,&len);
    // TODO send to the only node,
    char broadcast[ETH_ALEN] = {0xff,0xff,0xff,0xff,0xff,0xff};
    send_raw_ethernet_packet(packet,len,broadcast,EBP_ETHERTYPE,"enp0s3");
    return eba_internals_write(payload, wr->buff_id, wr->offset, wr->size);
}

int ebp_op_alloc_handler(const void *args, uint64_t arg_len)
{
    if (!args || arg_len < sizeof(struct ebp_op_alloc_args)) {
        pr_err("ebp_op_alloc_handler: invalid arg_len %llu (must be >= %zu) or null pointer %p\n",
               arg_len, sizeof(struct ebp_op_alloc_args), args);
        return -EINVAL;
    }

    const struct ebp_op_alloc_args *alloc_args = args;
    pr_info("ebp_op_alloc_handler: Allocation request received: size = %llu, life_time = %llu, receive bufferID = %llu\n",
            alloc_args->size, alloc_args->life_time, alloc_args->buffer_id);

    void *new_buf = eba_internals_malloc(alloc_args->size, alloc_args->life_time);
    if (!new_buf) {
        pr_err("ebp_op_alloc_handler: Allocation failed for size %llu\n", alloc_args->size);
        return -ENOMEM;
    }
    uint64_t buf_id = (uint64_t)new_buf;

    uint64_t len = 0;
    struct ebp_op_write_args wr_args = {
        .buff_id = alloc_args->buffer_id,
        .offset  = 0x00,
        .size    = sizeof(buf_id)
    };
    char *packet = build_invoke_req_packet(0x1234, EBP_OP_WRITE,
                           (char *)&wr_args, sizeof(wr_args),
                           (char *)&buf_id, wr_args.size,
                           &len);
    if (!packet) {
        pr_err("ebp_op_alloc_handler: Failed to build invoke request packet\n");
        return -ENOMEM;
    }

    /* Send the packet to the target node (here, using broadcast as an example) */
    char broadcast[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    send_raw_ethernet_packet(packet, len, broadcast, EBP_ETHERTYPE, "enp0s3");
    kfree(packet);
    return 0;
}




int ebp_op_read_handler(const void *args, uint64_t arg_len)
{
    /* Verify that a valid argument is provided and that it's at least as large as our header. */
    if (!args || arg_len < sizeof(struct ebp_op_read_args)) {
        pr_err("ebp_op_read_handler: invalid arg_len %llu (must be >= %zu) or null pointer %p\n",arg_len, sizeof(struct ebp_op_read_args), args);
        return -EINVAL;
    }

    const struct ebp_op_read_args *rd_args = args;

    pr_info("ebp_op_read_handler: dest_buffer_id = %llx, src_buffer_id = %llx, offset = %llu, size = %llu\n",rd_args->dest_buffer_id,rd_args->src_buffer_id, rd_args->offset, rd_args->size);
    // TODO here inquue to the invoke queue, if it is queued send an ack
    void* read_data = kmalloc(rd_args->size,GFP_KERNEL);
    int ret = eba_internals_read(read_data, rd_args->src_buffer_id, rd_args->offset, rd_args->size);
    
    kfree(read_data);
    //build packet and send it then free the read_data
    // TODO send the invoke with ( write request to the concerned node with read_data as the payload ! ).
    
    if (ret < 0) {
        pr_err("ebp_op_read_handler: eba_internals_read() failed with error %d\n", ret);
        return ret;
    }

    pr_info("ebp_op_read_handler: Successfully read %llu bytes from src_buffer_id 0x%llx into dest_buffer_id 0x%llx\n", rd_args->size, rd_args->src_buffer_id, rd_args->dest_buffer_id);
    return 0;
}