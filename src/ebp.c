#include "ebp.h"
#include "eba_internals.h"
#include "eba_net.h"
#include <linux/delay.h>
#include <linux/workqueue.h>
#include "eba.h"
#include "eba_utils.h"

void *local_specs = NULL; /* local node‑specs buffer, allocated in ebp_init() */

/*
 * Global packet_type structure for our 0xeba0 protocol.
 */
struct packet_type ebp_packet_type = {
    .type = htons(EBP_ETHERTYPE), /* Custom protocol type in network byte order */
    .dev = NULL,                  /* NULL = match on all devices */
    .func = ebp_handle_packets,   /* Packet receive callback function */
};

/*==================================================*/
/*                  Workqueue                       */
/*==================================================*/

static struct workqueue_struct *ebp_wq;

void ebp_work_handler(struct work_struct *work)
{
    struct ebp_work *wb = container_of(work, struct ebp_work, work);

    EBA_INFO("ebp_work_handler: [workqueue] starting work for skb %p\n", wb->skb);

    /* Call the full packet‐processing routine (frees wb->skb internally) */
    ebp_process_skb(wb->skb, wb->dev);

    EBA_INFO("ebp_work_handler: [workqueue] finished work for skb %p\n", wb->skb);

    /* Free our work descriptor */
    kfree(wb);
}

int ebp_handle_packets(struct sk_buff *skb, struct net_device *dev, struct packet_type *pt, struct net_device *orig_dev)
{
    struct ebp_work *wb;
    struct sk_buff  *cloned_skb;

    if (!skb) {
        EBA_ERR("ebp_handle_packets: skb is NULL\n");
        return NET_RX_DROP;
    }

    /* Clone skb for deferred processing */
    cloned_skb = skb_clone(skb, GFP_ATOMIC);
    if (!cloned_skb) {
        EBA_ERR("ebp_handle_packets: skb_clone() failed\n");
        kfree_skb(skb);
        return NET_RX_DROP;
    }
    EBA_INFO("ebp_handle_packets: cloned skb %p -> %p\n", skb, cloned_skb);

    /* Allocate and initialize our work item */
    wb = kmalloc(sizeof(*wb), GFP_ATOMIC);
    if (!wb) {
        EBA_ERR("ebp_handle_packets: kmalloc(ebp_work) failed\n");
        kfree_skb(cloned_skb);
        kfree_skb(skb);
        return NET_RX_DROP;
    }
    INIT_WORK(&wb->work, ebp_work_handler);
    wb->skb      = cloned_skb;
    wb->dev      = dev;

    EBA_INFO("ebp_handle_packets: queueing work for skb %p on ebp_wq\n", cloned_skb);
    queue_work(ebp_wq, &wb->work);

    /* Free the original skb immediately; our clone lives on */
    kfree_skb(skb);
    return NET_RX_SUCCESS;
}

void ebp_init(void)
{
    
    /* Create our single‑threaded workqueue ( simpler for now )*/
    ebp_wq = create_singlethread_workqueue("ebp_wq");
    if (!ebp_wq) {
        EBA_ERR("ebp_init: failed to create workqueue\n");
    }
    EBA_INFO("ebp_init: workqueue ebp_wq created\n");
    /* Register so that ebp_handle_packets() is invoked for every frame that matches our Ethertype. */
    dev_add_pack(&ebp_packet_type);

    /* One‑time init of global tables (node list, op registry…). */
    node_info_array_init();
    invoke_tracker_array_init();
    op_entry_array_init();
    ebp_ops_init();

    /* Pre‑allocate and fill the buffer holding *our* node‑specs; lifetime 0 = infinite. */
    local_specs = eba_internals_malloc(EBP_NODE_SPECS_MAX_SIZE, 0);
    if (eba_utils_file_to_buf("/var/lib/eba/node_local.eba",(uint64_t)local_specs) < 0) {
        EBA_ERR("ebp_init: read node_local failed\n");
    }
    /* Debug */
    print_node_infos();
}
void ebp_exit(void)
{
    /* Unregister the packet handler */
    dev_remove_pack(&ebp_packet_type);
    /* Drain & destroy workqueue */
    destroy_workqueue(ebp_wq);
    EBA_INFO("ebp_exit: workqueue ebp_wq destroyed\n");
}

/* Constants and global arrays for nodes, invokes, and ops */
struct node_info node_infos[MAX_NODE_COUNT];
int nodes_count = 1;
struct invoke_tracker invoke_trackers[MAX_INVOKE_COUNT];
struct op_entry op_entries[MAX_OP_COUNT];

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

    EBA_INFO("ebp_op_read: dest_buffer_id = %llu, src_buffer_id = %llu, dst_offset = %llu, src_offset = %llu, size = %llu\n", 
            rd_args->dst_buffer_id, rd_args->src_buffer_id,rd_args->dst_offset, rd_args->src_offset, rd_args->size);


    void *read_data = kmalloc(rd_args->size, GFP_ATOMIC);
    int ret = eba_internals_read(read_data, rd_args->src_buffer_id, rd_args->src_offset, rd_args->size);
    if (ret < 0)
    {
        EBA_ERR("ebp_op_read: eba_internals_read() failed with error %d\n", ret);
        kfree(read_data);
        return ret;
    }
    ret = ebp_remote_write(rd_args->dst_buffer_id, rd_args->dst_offset, rd_args->size, (const char *)read_data, mac);
    if (ret < 0)
    {
        EBA_ERR("ebp_op_read: ebp_remote_write() failed with error %d\n", ret);
        kfree(read_data);
        return ret;
    }

    EBA_INFO("ebp_op_read: Successfully read %llu bytes from src_buffer_id %llu into dest_buffer_id %llu\n",
             rd_args->size, rd_args->src_buffer_id, rd_args->dst_buffer_id);

    return 0;
}
int ebp_remote_alloc(uint64_t size, uint64_t life_time, uint64_t local_buff_id, const char mac[6] /* TODO modify it to be come node*/)
{
    struct ebp_op_alloc_args alloc_args = {
        .size = size,
        .life_time = life_time,
        .buffer_id = local_buff_id};

    int ret = send_invoke_req_packet(0x1234, EBP_OP_ALLOC, (char *)&alloc_args, sizeof(alloc_args), NULL, 0, mac, "enp0s8");
    if (ret < 0)
    {
        EBA_ERR("ebp_remote_alloc: send_invoke_req_packet() failed with error %d\n", ret);
        return ret;
    }

    EBA_INFO("ebp_remote_alloc: Sent EBP_OP_ALLOC request with local_buf_id = %llu\n", local_buff_id);
    return 0;
}

int ebp_remote_write(uint64_t buff_id, uint64_t offset, uint64_t size, const char *payload, const char mac[6] /* TODO modify it to be come node*/)
{
    struct ebp_op_write_args write_args = {
        .buff_id = buff_id,
        .offset = offset,
        .size = size};

    int ret = send_invoke_req_packet(0x1234, EBP_OP_WRITE, (char *)&write_args, sizeof(write_args), payload, write_args.size, mac, "enp0s8");
    if (ret < 0)
    {
        EBA_ERR("ebp_remote_write: send_invoke_req_packet() failed with error %d\n", ret);
        return ret;
    }
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

    int ret = send_invoke_req_packet(0x1234, EBP_OP_READ, (char *)&read_args, sizeof(read_args), NULL, 0, mac, "enp0s8");
    if (ret < 0)
    {
        EBA_ERR("ebp_remote_read: send_invoke_req_packet() failed with error %d\n", ret);
        return ret;
    }
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
        if (memcmp(node_infos[i].mac, mac_address, 6) == 0)
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
    EBA_INFO("ebp_remote_write_mtu: Calculated MTU = %u (remote_mtu=%u, local_mtu=%u, overhead=%lu)\n", mtu, remote_mtu, local_mtu, sizeof(struct ebp_invoke_req) + MTU_OVERHEAD);
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

int ebp_remote_write_fixed_mtu(const unsigned char *mac, uint16_t forced_mtu, uint64_t buff_id,
                               uint64_t total_size, const char *payload)
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

int ebp_handle_discover(struct sk_buff *skb,struct net_device *dev,
                        struct ethhdr  *eth,struct ebp_header  *hdr)
{
    struct ebp_discover_req *disc;
    void *node_specs;
    int ret;

    /* Sanity check packet length */
    if (skb->len < ETH_HLEN + sizeof(*disc)) {
        EBA_ERR("ebp_handle_discover: packet too short (len=%u)\n", skb->len);
        ret = NET_RX_DROP;
        goto out_free;
    }

    disc = (struct ebp_discover_req *)hdr;
    EBA_INFO("ebp_handle_discover: MTU=%u\n", ntohs(disc->mtu));

    /* Allocate specs buffer */
    node_specs = eba_internals_malloc(EBP_NODE_SPECS_MAX_SIZE, EBP_NODE_SPECS_MAX_LIFE_TIME);
    if (!node_specs) {
        EBA_ERR("ebp_handle_discover: malloc node_specs failed\n");
        ret = NET_RX_DROP;
        goto out_free;
    }

    /* Register node (or detect existing) */
    ret = ebp_register_node(ntohs(disc->mtu),eth->h_source, (uint64_t)node_specs);
    
    if (ret < 0 && ret != -EEXIST) {
        
        EBA_ERR("ebp_handle_discover: ebp_register_node err=%d\n", ret);
        eba_internals_free(node_specs);
        ret = NET_RX_DROP;
        goto out_free;
    }

    /* If already existed, free our new buffer and reuse old specs pointer */

    if (ret == -EEXIST) {
        eba_internals_free(node_specs);
        node_specs = (void* )ebp_get_specs_from_node_mac(eth->h_source);
    }

    /* Send the ACK */
    ret = send_discover_ack_packet((uint64_t)node_specs,eth->h_source,dev->name);
    
    if (ret < 0) {
        EBA_ERR("ebp_handle_discover: send_discover_ack err=%d\n", ret);
        ret = NET_RX_DROP;
        goto out_free;
    }

    ret = NET_RX_SUCCESS;

out_free:
    kfree_skb(skb);
    return ret;
}


int ebp_handle_discover_ack(struct sk_buff *skb,struct net_device *dev,
                            struct ethhdr *eth,struct ebp_header  *hdr)
{
    struct ebp_discover_ack *ack = (struct ebp_discover_ack *)hdr;
    uint64_t buff_id = be64_to_cpu(ack->buffer_id);
    int node_id;
    int rc;

    EBA_INFO("ebp_handle_discover_ack: buffer_id=%llu\n", buff_id);

    node_id = ebp_get_node_id_from_mac(eth->h_source);
    if (node_id < 0) {

        EBA_WARN("ebp_handle_discover_ack: unknown node, using MINIMAL_MTU\n");
        rc = ebp_remote_write_fixed_mtu(eth->h_source,MINIMAL_MTU,buff_id,EBP_NODE_SPECS_MAX_SIZE, (char *)local_specs);
    } 
    else {
        
        rc = ebp_remote_write_mtu(node_id,buff_id,EBP_NODE_SPECS_MAX_SIZE,(char *)local_specs);
    }

    if (rc < 0) {
        EBA_ERR("ebp_handle_discover_ack: remote write failed, rc=%d\n", rc);
        rc = NET_RX_DROP;
    } else {
        rc = NET_RX_SUCCESS;
    }

    kfree_skb(skb);
    return rc;
}


int ebp_handle_invoke(struct sk_buff *skb,struct net_device *dev,
                    struct ethhdr *eth,struct ebp_header *hdr)
{
    struct ebp_invoke_req *inv = (struct ebp_invoke_req *)hdr;
    uint32_t iid = ntohl(inv->iid);
    uint32_t opid = ntohl(inv->opid);
    uint64_t args_len = be64_to_cpu(inv->args_len);
    int rc;

    EBA_INFO("ebp_handle_invoke: IID=%u OpID=%u args_len=%llu\n", iid, opid, args_len);

    rc = ebp_invoke_op(opid, inv->args, args_len, eth->h_source);
    if (rc < 0) {
        EBA_ERR("ebp_handle_invoke: ebp_invoke_op failed, rc=%d\n", rc);
        rc = NET_RX_DROP;
    } else {
        rc = NET_RX_SUCCESS;
    }

    kfree_skb(skb);
    return rc;
}


int ebp_handle_invoke_ack(struct sk_buff *skb,struct net_device *dev,
                                 struct ethhdr *eth,struct ebp_header *hdr)
{
    struct ebp_invoke_ack *ack = (struct ebp_invoke_ack *)hdr;

    EBA_INFO("ebp_handle_invoke_ack TODO : status=0x%02x\n", ack->status);

    kfree_skb(skb);
    return NET_RX_SUCCESS;
}

int ebp_process_skb(struct sk_buff *skb,struct net_device *dev)
{
    struct ebp_header *hdr;
    struct ethhdr *eth;

    if (WARN_ON_ONCE(!skb))
        return NET_RX_DROP;

    if (skb->len < ETH_HLEN + sizeof(*hdr)) {
        EBA_ERR("ebp_process_skb: packet too short on %s (len=%u)\n", dev->name, skb->len);
        kfree_skb(skb);
        return NET_RX_DROP;
    }

    eth = eth_hdr(skb);
    hdr = (struct ebp_header *)skb->data;

    EBA_INFO("ebp_process_skb: dispatching msgType=0x%02x from %pM on %s\n", hdr->msgType, eth->h_source, dev->name);

    switch (hdr->msgType) {
    case EBP_MSG_DISCOVER:
        return ebp_handle_discover(skb, dev, eth, hdr);
    
    case EBP_MSG_DISCOVER_ACK:
        return ebp_handle_discover_ack(skb, dev, eth, hdr);
    
    case EBP_MSG_INVOKE:
        return ebp_handle_invoke(skb, dev, eth, hdr);
    
    case EBP_MSG_INVOKE_ACK:
        return ebp_handle_invoke_ack(skb, dev, eth, hdr);
    
    default:
        EBA_ERR("ebp_process_skb: unknown msgType=0x%02x\n", hdr->msgType);
        kfree_skb(skb);
        return NET_RX_DROP;
    }
}
