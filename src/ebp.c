#include "ebp.h"
#include "eba_internals.h"
#include "eba_net.h"
#include <linux/delay.h>
#include "eba.h"
#include "eba_utils.h"
void *local_specs = NULL;
/*
 * Global packet_type structure for our extended buffer protocol.
 */
struct packet_type ebp_packet_type = {
    .type = htons(EBP_ETHERTYPE), /* Custom protocol type in network byte order */
    .dev = NULL,                  /* NULL = match on all devices */
    .func = ebp_handle_packets,   /* Packet receive callback function */
};
// handle errors perfectly
int ebp_handle_packets(struct sk_buff *skb, struct net_device *dev,
                       struct packet_type *pt, struct net_device *orig_dev)
{
    struct ebp_header *eba_hdr;
    unsigned char *payload;
    int payload_len;
    struct ethhdr *eth;

    if (!skb)
    {
        EBA_ERR("ebp_handle_packets: Packet is NULL\n");
        return NET_RX_DROP;
    }

    /* Verify packet is long enough for an Ethernet header + EBA header */
    if (skb->len < ETH_HLEN + sizeof(struct ebp_header))
    {
        EBA_ERR("ebp_handle_packets: Packet too short on device %s, length: %u\n", dev->name, skb->len);
        kfree_skb(skb);
        return NET_RX_DROP;
    }

    /* Get the Ethernet header */
    eth = eth_hdr(skb);
    EBA_INFO("ebp_handle_packets: Source MAC address: %pM\n", eth->h_source);

    /* EBA header */
    payload = skb->data;
    payload_len = skb->len;
    eba_hdr = (struct ebp_header *)payload;

    EBA_INFO("ebp_handle_packets: Received packet on %s, protocol: 0x%04x, length: %u, EBA msgType: 0x%02x\n", dev->name, ntohs(skb->protocol), skb->len, eba_hdr->msgType);

    switch (eba_hdr->msgType)
    {
    case EBP_MSG_DISCOVER: /* 0x01 */
        if (skb->len < ETH_HLEN + sizeof(struct ebp_discover_req))
        {
            EBA_ERR("ebp_handle_packets: Discover Request packet too short\n");
            kfree_skb(skb);
            return NET_RX_DROP;
        }
        else
        {
            struct ebp_discover_req *disc = (struct ebp_discover_req *)eba_hdr;
            EBA_INFO("ebp_handle_packets: Discover Request: MTU = %u\n", ntohs(disc->mtu));
            // allocate node specs buffer
            void *node_specs = eba_internals_malloc(EBP_NODE_SPECS_MAX_SIZE, EBP_NODE_SPECS_MAX_LIFE_TIME);
            if (!node_specs)
            {
                EBA_ERR("ebp_handle_packets: Failed to allocate node_specs buffer\n");
                kfree_skb(skb);
                return NET_RX_DROP;
            }
            // register the new node
            // handle errors lol
            int ret = ebp_register_node(ntohs(disc->mtu), eth->h_source, (uint64_t)node_specs);
            if (ret == -EEXIST)
            {
                /*
                 * The node is already registered.
                 * In that case, we do NOT want to keep the newly allocated buffer.
                 * Instead, find the old node_specs from node_infos, and send the ACK with that pointer.
                 */
                eba_internals_free(node_specs);
                /* Lookup existing node_specs for this MAC */
                uint64_t existing_specs = ebp_get_specs_from_node_mac(eth->h_source);
                if (!existing_specs)
                {
                    EBA_ERR("ebp_handle_packets: Node was said to exist, but no specs found. MAC=%pM\n", eth->h_source);
                    /*
                     * Possibly return an error or continue,
                     * but there's no valid node_specs to send in the ACK.
                     */
                    kfree_skb(skb);
                    return NET_RX_DROP;
                }
                /* Send the Discover ACK using the previously stored node_specs */
                ret = send_discover_ack_packet(existing_specs, eth->h_source, "enp0s8");
                if (ret < 0)
                {
                    EBA_ERR("ebp_handle_packets: send discover ack packet failed\n");
                    kfree_skb(skb);
                    return NET_RX_DROP;
                }
            }
            else if (ret < 0)
            {
                /* Some other error from ebp_register_node */
                EBA_ERR("ebp_handle_packets: register node failed with error %d\n", ret);
                eba_internals_free(node_specs);
                kfree_skb(skb);
                return NET_RX_DROP;
            }
            else
            {
                /* New node successfully registered, so send an ACK using the newly allocated buffer */
                ret = send_discover_ack_packet((uint64_t)node_specs, eth->h_source, "enp0s8");
                if (ret < 0)
                {
                    EBA_ERR("ebp_handle_packets: send discover ack packet failed\n");
                    kfree_skb(skb);
                    eba_internals_free(node_specs);
                    return NET_RX_DROP;
                }
            }
        }
        break;
    case EBP_MSG_DISCOVER_ACK: /* 0x03 */
        if (skb->len < ETH_HLEN + sizeof(struct ebp_discover_ack))
        {
            EBA_ERR("ebp_handle_packets: Discover Ack packet too short\n");
            kfree_skb(skb);
            return NET_RX_DROP;
        }
        else
        {
            struct ebp_discover_ack *ack = (struct ebp_discover_ack *)eba_hdr;
            uint64_t buff_id = be64_to_cpu(ack->buffer_id);
            EBA_INFO("ebp_handle_packets: Discover Ack: buffer_id = %llu \n", buff_id);
            int node_id = ebp_get_node_id_from_mac(eth->h_source);
            int rc = eba_utils_file_to_buf("/var/lib/eba/node_local.eba", (uint64_t)local_specs);
            if (rc < 0)
            {
                EBA_ERR("ebp_handle_packets: Failed to read node_local.eba, ret=%d\n", rc);
                kfree_skb(skb);
                return NET_RX_DROP;
            }
            EBA_INFO("ebp_handle_packets: Read %llu bytes from node_local.eba, sending to remote\n", (uint64_t)EBP_NODE_SPECS_MAX_SIZE);
            if (node_id < 0)
            {
                /*
                 * Node not registered => do NOT register. Instead, just send the data
                 * using your new function that uses a forced MTU + MAC todo rethink about the design cuh.
                 */
                EBA_WARN("ebp_handle_packets: node not registered => sending data with forced MTU=%d\n",MINIMAL_MTU);
                int rc = ebp_remote_write_fixed_mtu(eth->h_source,MINIMAL_MTU,buff_id,EBP_NODE_SPECS_MAX_SIZE,(char*)local_specs);
                if (rc < 0)
                {
                    EBA_ERR("ebp_handle_packets: ebp_remote_write_fixed_mtu() failed, ret=%d\n",rc);
                    kfree_skb(skb);
                    return NET_RX_DROP;
                }
            }
            else
            {
                rc = ebp_remote_write_mtu(node_id, buff_id, (uint64_t)EBP_NODE_SPECS_MAX_SIZE, (char *)local_specs);
                if (rc < 0)
                {
                    EBA_ERR("ebp_handle_packets: Failed to ebp_remote_write_mtu() node_local.eba content, ret=%d\n", rc);
                    kfree_skb(skb);
                    return NET_RX_DROP;
                }
            }
        }
        break;
    case EBP_MSG_INVOKE: /* 0x02 */
        if (skb->len < ETH_HLEN + sizeof(struct ebp_invoke_req))
        {
            EBA_ERR("ebp_handle_packets: Invoke Request packet too short\n");
        }
        else
        {
            struct ebp_invoke_req *inv = (struct ebp_invoke_req *)eba_hdr;
            uint32_t iid = ntohl(inv->iid);
            uint32_t opid = ntohl(inv->opid);
            uint64_t args_len = be64_to_cpu(inv->args_len);

            EBA_INFO("ebp_handle_packets: Invoke Request: IID = %u, OpID = %u, args_len = %llu\n", iid, opid, args_len);
            int ret = ebp_invoke_op(opid, inv->args, args_len, eth->h_source);
            if (ret < 0)
            {
                EBA_ERR("ebp_handle_packets: ebp_invoke_op(opid=%u) failed ret=%d\n", opid, ret);
                kfree_skb(skb);
                return NET_RX_DROP;
                /* Possibly build & send an error ack or something... TODO */
            }
            else
            {
                /* Possibly build an ack for success... TODO */
            }
        }
        break;
    case EBP_MSG_INVOKE_ACK: /* 0x04 */
        if (skb->len < ETH_HLEN + sizeof(struct ebp_invoke_ack))
        {
            EBA_ERR("ebp_handle_packets: Invoke Ack packet too short\n");
            kfree_skb(skb);
            return NET_RX_DROP;
        }
        else
        {
            struct ebp_invoke_ack *inv_ack = (struct ebp_invoke_ack *)eba_hdr;
            EBA_INFO("ebp_handle_packets: Invoke Ack: status = 0x%02x TODO \n ", inv_ack->status);
        }
        break;
    default:
        EBA_ERR("ebp_handle_packets: Unknown EBA msgType: 0x%02x\n", eba_hdr->msgType);
        kfree_skb(skb);
        return NET_RX_DROP;
        break;
    }

    kfree_skb(skb);
    return NET_RX_SUCCESS;
}

void ebp_init(void)
{
    dev_add_pack(&ebp_packet_type);
    node_info_array_init();
    invoke_tracker_array_init();
    op_entry_array_init();
    ebp_ops_init();
    local_specs = eba_internals_malloc(EBP_NODE_SPECS_MAX_SIZE, 0);
    print_node_infos();
}
void ebp_exit(void)
{
    dev_remove_pack(&ebp_packet_type);
}
#define INVALID_NODE_ID 0xffff
/* Global arrays for the EBA protocol data structures */
struct node_info node_infos[MAX_NODE_COUNT];
int nodes_count = 1;
struct invoke_tracker invoke_trackers[MAX_INVOKE_COUNT];
struct op_entry op_entries[MAX_OP_COUNT];
/* Add a helper function to print the global node_infos array */
void print_node_infos(void)
{
    int i;
    EBA_INFO("=== node_infos Array Dump ===\n");
    for (i = 0; i < MAX_NODE_COUNT; i++)
    {
        if (node_infos[i].id != INVALID_NODE_ID)
        {
            EBA_INFO("Slot %d: NodeID=%d, MTU=%u, MAC=%pM, node_specs=%llu\n",
                     i,
                     node_infos[i].id,
                     node_infos[i].mtu,
                     node_infos[i].mac,
                     node_infos[i].node_specs);
        }
    }
    EBA_INFO("=============================\n");
}
void print_op_entries(void)
{
    int i;
    EBA_INFO("=== op_entries Array Dump ===\n");
    for (i = 0; i < MAX_OP_COUNT; i++)
    {
        EBA_INFO("Slot %d: op_id = %u, op_ptr = %p\n",
                 i, op_entries[i].op_id, op_entries[i].op_ptr);
    }
    EBA_INFO("=============================\n");
}
int node_info_array_init(void)
{
    uint64_t i;
    for (i = 0; i < MAX_NODE_COUNT; i++)
    {
        node_infos[i].id = INVALID_NODE_ID;
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
int ebp_register_op(uint32_t op_id, ebp_op_t fn)
{
    int i;

    if (!fn)
    {
        EBA_ERR("eba_register_op: null function pointer\n");
        return -EINVAL;
    }

    /* Check if this op_id is already registered */
    for (i = 0; i < MAX_OP_COUNT; i++)
    {
        if (op_entries[i].op_id == op_id)
        {
            EBA_ERR("eba_register_op: op_id %u already exists (slot %d)\n", op_id, i);
            return -EEXIST;
        }
    }

    /* Find a free entry */
    for (i = 0; i < MAX_OP_COUNT; i++)
    {
        if (op_entries[i].op_ptr == NULL && op_entries[i].op_id == 0)
        {
            op_entries[i].op_id = op_id;
            op_entries[i].op_ptr = fn;
            EBA_INFO("Registered op_id %u in slot %d\n", op_id, i);
            return 0;
        }
    }
    EBA_ERR("eba_register_op: No space for new op_id %u\n", op_id);
    return -ENOSPC;
}

int ebp_invoke_op(uint32_t op_id, const void *args, uint64_t arg_len, const char mac[6])
{
    int i;
    for (i = 0; i < MAX_OP_COUNT; i++)
    {
        if (op_entries[i].op_id == op_id)
        {
            EBA_INFO("Found op_id %u in slot %d, calling handler...\n", op_id, i);
            return op_entries[i].op_ptr(args, arg_len, mac);
        }
    }
    EBA_ERR("ebp_invoke_op: No matching op_id %u found\n", op_id);
    return -EINVAL;
}

int ebp_ops_init(void)
{
    int ret = 0;
    if (ebp_register_op(EBP_OP_ALLOC, ebp_op_alloc) < 0)
        ret = -1;
    if (ebp_register_op(EBP_OP_WRITE, ebp_op_write) < 0)
        ret = -1;
    if (ebp_register_op(EBP_OP_READ, ebp_op_read) < 0)
        ret = -1;
    return ret;
}

int ebp_op_write(const void *args, uint64_t arg_len, const char mac[6])
{
    /* Check that args is not NULL and that its is large enough for our fixed-size header */
    if (!args || arg_len < sizeof(struct ebp_op_write_args))
    {
        EBA_ERR("ebp_op_write: invalid arg_len %llu (must be >= %zu) or null pointer %p\n", arg_len, sizeof(struct ebp_op_write_args), args);
        return -EINVAL;
    }

    const struct ebp_op_write_args *wr = args;
    uint64_t header_size = sizeof(struct ebp_op_write_args);

    if (arg_len < header_size + wr->size)
    {
        EBA_ERR("ebp_op_write: args too short for payload data: arg_len = %llu, header_size = %llu, expected payload size = %llu\n",
                arg_len, header_size, wr->size);
        return -EINVAL;
    }

    const uint8_t *payload = (const uint8_t *)args + header_size;

    EBA_INFO("ebp_op_write: buff_id = %llx offset = %llu size = %llu\n", wr->buff_id, wr->offset, wr->size);
    // TODO here inqueue to the invoke queue and send an ack
    // print_hex_dump(KERN_INFO, "ebp_op_write payload :", DUMP_PREFIX_OFFSET, 16, 1,payload, wr->size, true);
    send_invoke_ack_packet(INVOKE_QUEUED, mac, "enp0s8");
    return eba_internals_write(payload, wr->buff_id, wr->offset, wr->size);
}

int ebp_op_alloc(const void *args, uint64_t arg_len, const char mac[6])
{
    if (!args || arg_len < sizeof(struct ebp_op_alloc_args))
    {
        EBA_ERR("ebp_op_alloc: invalid arg_len %llu (must be >= %zu) or null pointer %p\n",
                arg_len, sizeof(struct ebp_op_alloc_args), args);
        return -EINVAL;
    }

    const struct ebp_op_alloc_args *alloc_args = args;
    EBA_INFO("ebp_op_alloc: Allocation request received: size = %llu, life_time = %llu, receive bufferID = %llu\n",
             alloc_args->size, alloc_args->life_time, alloc_args->buffer_id);

    void *new_buf = eba_internals_malloc(alloc_args->size, alloc_args->life_time);
    if (!new_buf)
    {
        EBA_ERR("ebp_op_alloc: Allocation failed for size %llu\n", alloc_args->size);
        return -ENOMEM;
    }
    uint64_t buf_id = (uint64_t)new_buf;
    ebp_remote_write(alloc_args->buffer_id, 0, sizeof(buf_id), (char *)&buf_id, mac);
    return 0;
}
int ebp_op_read(const void *args, uint64_t arg_len, const char mac[6])
{
    /* Verify that a valid argument is provided and that it's at least as large as our header. */
    if (!args || arg_len < sizeof(struct ebp_op_read_args))
    {
        EBA_ERR("ebp_op_read: invalid arg_len %llu (must be >= %zu) or null pointer %p\n", arg_len, sizeof(struct ebp_op_read_args), args);
        return -EINVAL;
    }
    const struct ebp_op_read_args *rd_args = args;

    EBA_INFO("ebp_op_read: dest_buffer_id = %llu, src_buffer_id = %llu, dst_offset = %llu, src_offset = %llu, size = %llu\n", rd_args->dst_buffer_id, rd_args->src_buffer_id,
             rd_args->dst_offset, rd_args->src_offset, rd_args->size);
    // TODO here inquue to the invoke queue, if it is queued send an ack
    void *read_data = kmalloc(rd_args->size, GFP_KERNEL);
    int ret = eba_internals_read(read_data, rd_args->src_buffer_id, rd_args->src_offset, rd_args->size);
    ebp_remote_write(rd_args->dst_buffer_id, rd_args->dst_offset, rd_args->size, (const char *)read_data, mac);

    kfree(read_data);
    // build packet and send it then free the read_data
    //  TODO send the invoke with ( write request to the concerned node with read_data as the payload ! ).

    if (ret < 0)
    {
        EBA_ERR("ebp_op_read: eba_internals_read() failed with error %d\n", ret);
        return ret;
    }

    EBA_INFO("ebp_op_read: Successfully read %llu bytes from src_buffer_id %llu into dest_buffer_id %llu\n", rd_args->size, rd_args->src_buffer_id, rd_args->dst_buffer_id);
    return 0;
}
int ebp_remote_alloc(uint64_t size, uint64_t life_time, uint64_t local_buff_id, const char mac[6] /* TODO modify it to be come node*/)
{
    struct ebp_op_alloc_args alloc_args = {
        .size = size,
        .life_time = life_time,
        .buffer_id = local_buff_id};
    send_invoke_req_packet(0x1234, EBP_OP_ALLOC, (char *)&alloc_args, sizeof(alloc_args), NULL, 0, mac, "enp0s8");
    EBA_INFO("ebp_remote_alloc: Sent EBP_OP_ALLOC request with local_buf_id = %llu\n", local_buff_id);
    return 0;
}

int ebp_remote_write(uint64_t buff_id, uint64_t offset, uint64_t size, const char *payload, const char mac[6] /* TODO modify it to be come node*/)
{
    struct ebp_op_write_args write_args = {
        .buff_id = buff_id,
        .offset = offset,
        .size = size};
    send_invoke_req_packet(0x1234, EBP_OP_WRITE, (char *)&write_args, sizeof(write_args), payload, write_args.size, mac, "enp0s8");
    EBA_INFO("ebp_remote_write: Sent EBP_OP_WRITE request on buffer %llu\n", buff_id);
    return 0;
}

int ebp_remote_read(uint64_t dst_buffer_id, uint64_t src_buffer_id, uint64_t dst_offset, uint64_t src_offset, uint64_t size, const char mac[6] /* TODO modify it to be come node*/)
{
    struct ebp_op_read_args read_args = {
        .dst_buffer_id = dst_buffer_id,
        .src_buffer_id = src_buffer_id,
        .dst_offset = dst_offset,
        .src_offset = src_offset,
        .size = size};
    send_invoke_req_packet(0x1234, EBP_OP_READ, (char *)&read_args, sizeof(read_args), NULL, 0, mac, "enp0s8");
    EBA_INFO("ebp_remote_read: Sent EBP_OP_READ request from  %llu to %llu\n", src_buffer_id, dst_buffer_id);
    return 0;
}
int ebp_register_node(uint16_t mtu, const char mac[6], uint64_t node_specs)
{
    int free_slot = -1;
    int i;

    /* First, check if a node with this MAC already exists. */
    for (i = 0; i < MAX_NODE_COUNT; i++)
    {
        if (node_infos[i].id != INVALID_NODE_ID && memcmp(node_infos[i].mac, mac, 6) == 0)
        {
            /* Node already registered, return success (no error). */
            EBA_INFO("ebp_register_node: Node with MAC %pM already exists, skipping registration.\n", mac);
            return -EEXIST;
        }
    }

    /* If no existing node found, find the first free slot. */
    for (i = 0; i < MAX_NODE_COUNT; i++)
    {
        if (node_infos[i].id == INVALID_NODE_ID)
        {
            free_slot = i;
            break;
        }
    }

    if (free_slot == -1)
    {
        EBA_ERR("ebp_register_node: No space left to register a new node.\n");
        return -ENOSPC;
    }

    /* Register the node in the found free slot */
    node_infos[free_slot].id = nodes_count;
    node_infos[free_slot].mtu = mtu;
    memcpy(node_infos[free_slot].mac, mac, 6);
    node_infos[free_slot].node_specs = node_specs;

    EBA_INFO("ebp_register_node: Registered node with NodeID=%d (slot %d), MAC=%pM, MTU=%u, node_specs=%llu\n",
             node_infos[free_slot].id, free_slot, mac, mtu, node_specs);

    nodes_count++;
    print_node_infos();
    return 0;
}

int ebp_get_node_id_from_mac(const char mac[6])
{
    int i;
    for (i = 0; i < MAX_NODE_COUNT; i++)
    {
        if (node_infos[i].id != INVALID_NODE_ID &&
            memcmp(node_infos[i].mac, mac, ETH_ALEN) == 0)
        {
            return node_infos[i].id;
        }
    }
    return -1; /* Not found */
}

const unsigned char *ebp_get_mac_from_node_id(int node_id)
{
    int i;
    for (i = 0; i < MAX_NODE_COUNT; i++)
    {
        if (node_infos[i].id == node_id)
        {
            return node_infos[i].mac;
        }
    }
    return NULL; /* Not found */
}

uint16_t ebp_get_mtu_from_node_id(int node_id)
{
    int i;
    for (i = 0; i < MAX_NODE_COUNT; i++)
    {
        if (node_infos[i].id == node_id)
        {
            return node_infos[i].mtu;
        }
    }
    return 0; /* Not found */
}

uint64_t ebp_get_specs_from_node_id(int node_id)
{
    int i;
    for (i = 0; i < MAX_NODE_COUNT; i++)
    {
        if (node_infos[i].id == node_id)
        {
            return node_infos[i].node_specs;
        }
    }
    return 0; /* Not found */
}

uint64_t ebp_get_specs_from_node_mac(const char *mac_address)
{
    int i;
    for (i = 0; i < MAX_NODE_COUNT; i++)
    {
        if (strcmp(node_infos[i].mac, mac_address) == 0)
        {
            return node_infos[i].node_specs;
        }
    }
    return 0; /* Not found */
}

int ebp_remote_write_mtu(int node_id, uint64_t buff_id, uint64_t total_size, const char *payload)
{
    int ret = 0;
    uint16_t mtu;
    uint64_t offset = 0;
    const unsigned char *dest_mac;
    /* Retrieve the MTU for the specified node and substract the header size xd xd xd xd , take the min of the local node mtu and the remote one TODO */
    uint16_t remote_mtu = ebp_get_mtu_from_node_id(node_id);
    uint16_t local_mtu = eba_net_get_current_mtu("enp0s8");
    mtu = min((remote_mtu - (sizeof(struct ebp_invoke_req) + MTU_OVERHEAD)), local_mtu);
    EBA_INFO("ebp_remote_write_mtu: Calculated MTU = %u (remote_mtu=%u, local_mtu=%u, overhead=%lu)\n",mtu, remote_mtu, local_mtu, sizeof(struct ebp_invoke_req) + MTU_OVERHEAD);
    if (mtu <= 0)
    {
        EBA_ERR("ebp_remote_write_mtu: Invalid MTU retrieved for node %d\n", node_id);
        return -EINVAL;
    }
    /* Get the destination MAC address for the node */
    dest_mac = ebp_get_mac_from_node_id(node_id);
    if (!dest_mac)
    {
        EBA_ERR("ebp_remote_write_mtu: No MAC found for node %d\n", node_id);
        return -EINVAL;
    }

    /* Write the payload in segments, each no larger than the node's MTU */
    while (offset < total_size)
    {
        uint64_t segment_size = total_size - offset;

        if (segment_size > mtu)
            segment_size = mtu;

        ret = ebp_remote_write(buff_id, offset, segment_size, payload + offset, dest_mac);
        if (ret < 0)
        {
            EBA_ERR("ebp_remote_write_mtu: ebp_remote_write failed at offset %llu, ret=%d\n", offset, ret);
            return ret;
        }

        offset += segment_size;
    }

    return 0;
}

int ebp_discover(void)
{
    char mac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    int mtu = eba_net_get_current_mtu("enp0s8");
    if (mtu < 0)
    {
        EBA_ERR("ebp_discover: eba_net_get_current_mtu failed");
        return -1;
    }
    return send_discover_req_packet((uint16_t)mtu, mac, "enp0s8");
}
/**
 * ebp_remote_write_fixed_mtu() - Send data in chunks constrained by a given MTU.
 * @mac:         The remote node's MAC address (does not need to be registered).
 * @forced_mtu:  The caller-specified MTU to use for chunking.
 * @buff_id:     The remote buffer identifier to write into.
 * @total_size:  The total number of bytes in @payload.
 * @payload:     Pointer to the data to send.
 *
 * This function does not register the node nor look up any existing MTU in
 * node_infos[].  It simply uses @forced_mtu as the maximum segment size.
 * @note it is used to debug for now to find a way to do the discovery protocol. (TODO)
 * Returns 0 on success, or a negative error code if any chunk fails.
 */
int ebp_remote_write_fixed_mtu(const unsigned char *mac,uint16_t forced_mtu,uint64_t buff_id,uint64_t total_size,const char *payload)
{
    const uint16_t overhead = sizeof(struct ebp_invoke_req) + MTU_OVERHEAD;

    if (!mac || !payload)
    {
        EBA_ERR("ebp_remote_write_fixed_mtu: mac or payload is NULL\n");
        return -EINVAL;
    }
    if (forced_mtu <= overhead)
    {
        EBA_ERR("ebp_remote_write_fixed_mtu: forced_mtu (%u) <= overhead (%u), no space for data\n",
                forced_mtu, overhead);
        return -EINVAL;
    }

    uint16_t chunk_size = forced_mtu - overhead;
    uint64_t offset = 0;

    /* Break the data into chunks of 'chunk_size' and send each one. */
    while (offset < total_size)
    {
        uint64_t remaining = total_size - offset;
        uint64_t segment_size = (remaining < chunk_size) ? remaining : chunk_size;

        int ret = ebp_remote_write(buff_id, offset, segment_size,
                                   payload + offset, mac);
        if (ret < 0)
        {
            EBA_ERR("ebp_remote_write_fixed_mtu: ebp_remote_write() failed at offset=%llu, ret=%d\n",
                    offset, ret);
            return ret;
        }

        offset += segment_size;
    }

    return 0;
}