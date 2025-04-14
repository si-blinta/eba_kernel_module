#include "ebp.h"
#include "eba_internals.h"
#include "eba_net.h"
#include <linux/delay.h> 
#include "eba.h"
/*
 * Global packet_type structure for our extended buffer protocol.
 */
struct packet_type ebp_packet_type = {
    .type = htons(EBP_ETHERTYPE), /* Custom protocol type in network byte order */
    .dev = NULL,                  /* NULL = match on all devices */
    .func = ebp_handle_packets,   /* Packet receive callback function */
};
//handle errors perfectly
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
        EBA_ERR("Packet too short on device %s, length: %u\n",dev->name, skb->len);
        kfree_skb(skb);
        return NET_RX_DROP;
    }

    /* Get the Ethernet header */
    eth = eth_hdr(skb);
    EBA_DBG("Source MAC address: %pM\n", eth->h_source);

    /* EBA header */
    payload = skb->data;
    payload_len = skb->len;
    eba_hdr = (struct ebp_header *)payload;

    EBA_DBG("Received packet on %s, protocol: 0x%04x, length: %u, EBA msgType: 0x%02x\n",dev->name, ntohs(skb->protocol), skb->len, eba_hdr->msgType);
    
    switch (eba_hdr->msgType)
    {
    case EBP_MSG_DISCOVER: /* 0x01 */
        if (skb->len < ETH_HLEN + sizeof(struct ebp_discover_req))
        {
            EBA_ERR("Discover Request packet too short\n");
        }
        else
        {
            struct ebp_discover_req *disc = (struct ebp_discover_req *)eba_hdr;
            EBA_DBG("Discover Request: MTU = %u\n", ntohs(disc->mtu));
            //allocate node specs buffer
            void* node_specs = eba_internals_malloc(EBP_NODE_SPECS_MAX_SIZE,EBP_NODE_SPECS_MAX_LIFE_TIME);
            //register the new node
            //handle errors lol 
            int ret = ebp_register_node(ntohs(disc->mtu),eth->h_source,(uint64_t)node_specs);
            //send the discover ack with the allocated buffer to enable remote node to share its data.
            ret = send_discover_ack_packet((uint64_t)node_specs,eth->h_source,"enp0s8");
        }
        break;
    case EBP_MSG_DISCOVER_ACK: /* 0x03 */
        if (skb->len < ETH_HLEN + sizeof(struct ebp_discover_ack))
        {
            EBA_ERR("Discover Ack packet too short\n");
        }
        else
        {
            struct ebp_discover_ack *ack = (struct ebp_discover_ack *)eba_hdr;
            uint64_t buff_id = be64_to_cpu(ack->buffer_id);
            EBA_DBG("Discover Ack: buffer_id = 0x%llu \n", buff_id);
            //here read a file called node_specs.eba, and write its content to the distant buffer : todo
            //todo handle mtu
            /*char *file_data = NULL;
            uint64_t file_size = 0;
            int rc = read_file_into_buffer("/var/lib/eba/node_local.eba", &file_data, &file_size);
            if (rc < 0) {
                EBA_ERR("Failed to read node_local.eba, ret=%d\n", rc);
                // Possibly stop or continue with empty data
                //handle errors
            } else {
                EBA_INFO("Read %llu bytes from node_local.eba, sending to remote\n", file_size);

                rc = ebp_remote_write(buff_id, 0, file_size, file_data, eth->h_source);
                if (rc < 0) {
                    EBA_ERR("Failed to ebp_remote_write() node_local.eba content, ret=%d\n", rc);
                }
                kfree(file_data);
            }*/
        }
        break;
    case EBP_MSG_INVOKE: /* 0x02 */
        if (skb->len < ETH_HLEN + sizeof(struct ebp_invoke_req))
        {
            EBA_ERR("Invoke Request packet too short\n");
        }
        else
        {
            struct ebp_invoke_req *inv = (struct ebp_invoke_req *)eba_hdr;
            uint32_t iid = ntohl(inv->iid);
            uint32_t opid = ntohl(inv->opid);
            uint64_t args_len = be64_to_cpu(inv->args_len);

            EBA_DBG("Invoke Request: IID = %u, OpID = %u, args_len = %llu\n",iid, opid, args_len);
            int ret = ebp_invoke_op(opid, inv->args, args_len, eth->h_source);
            if (ret < 0) {
                EBA_ERR("ebp_invoke_op(opid=%u) failed ret=%d\n", opid, ret);
                /* Possibly build & send an error ack or something... TODO */
            } else {
                /* Possibly build an ack for success... TODO */
            }
        
        }
        break;
    case EBP_MSG_INVOKE_ACK: /* 0x04 */
        if (skb->len < ETH_HLEN + sizeof(struct ebp_invoke_ack))
        {
            EBA_ERR("Invoke Ack packet too short\n");
        }
        else
        {
            struct ebp_invoke_ack *inv_ack = (struct ebp_invoke_ack *)eba_hdr;
            EBA_DBG("Invoke Ack: status = 0x%02x TODO \n ", inv_ack->status);
        }
        break;
    default:
        EBA_ERR("Unknown EBA msgType: 0x%02x\n", eba_hdr->msgType);
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
}
void ebp_exit(void)
{
    dev_remove_pack(&ebp_packet_type);
}

/* Global arrays for the EBA protocol data structures */
struct node_info node_infos[MAX_NODE_COUNT];
int nodes_count = 0;
struct invoke_tracker invoke_trackers[MAX_INVOKE_COUNT];
struct op_entry op_entries[MAX_OP_COUNT];
void print_op_entries(void)
{
    int i;
    EBA_DBG("=== op_entries Array Dump ===\n");
    for (i = 0; i < MAX_OP_COUNT; i++) {
        EBA_DBG("Slot %d: op_id = %u, op_ptr = %p\n", 
                 i, op_entries[i].op_id, op_entries[i].op_ptr);
    }
    EBA_DBG("=============================\n");
}
int node_info_array_init(void)
{
    uint64_t i;
    for (i = 0; i < MAX_NODE_COUNT; i++)
    {
        node_infos[i].id = -1;
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
        EBA_ERR("eba_register_op: null function pointer\n");
        return -EINVAL;
    }
    
    /* Check if this op_id is already registered */
    for (i = 0; i < MAX_OP_COUNT; i++) {
        if (op_entries[i].op_id == op_id) {
            EBA_ERR("eba_register_op: op_id %u already exists (slot %d)\n", op_id, i);
            return -EEXIST;
        }
    }

    /* Find a free entry */
    for (i = 0; i < MAX_OP_COUNT; i++) {
        if (op_entries[i].op_ptr == NULL && op_entries[i].op_id == 0) {
            op_entries[i].op_id  = op_id;
            op_entries[i].op_ptr = fn;
            EBA_DBG("Registered op_id %u in slot %d\n", op_id, i);
            return 0;
        }
    }
    EBA_ERR("eba_register_op: No space for new op_id %u\n", op_id);
    return -ENOSPC;
}


int ebp_invoke_op(uint32_t op_id, const void *args, uint64_t arg_len, const char mac[6])
{
    int i;
    for (i = 0; i < MAX_OP_COUNT; i++) {
        if (op_entries[i].op_id == op_id) {
            EBA_DBG("Found op_id %u in slot %d, calling handler...\n", op_id, i);
            return op_entries[i].op_ptr(args, arg_len,mac);
        }
    }
    EBA_ERR("ebp_invoke_op: No matching op_id %u found\n", op_id);
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

int ebp_op_write_handler(const void *args, uint64_t arg_len, const char mac[6])
{
    /* Check that args is not NULL and that its is large enough for our fixed-size header */
    if (!args || arg_len < sizeof(struct ebp_op_write_args))
    {
        EBA_ERR("ebp_op_write_handler: invalid arg_len %llu (must be >= %zu) or null pointer %p\n",arg_len, sizeof(struct ebp_op_write_args), args);
        return -EINVAL;
    }

    const struct ebp_op_write_args *wr = args;
    uint64_t header_size = sizeof(struct ebp_op_write_args);

    if (arg_len < header_size + wr->size)
    {
        EBA_ERR("ebp_op_write_handler: args too short for payload data: arg_len = %llu, header_size = %llu, expected payload size = %llu\n",
               arg_len, header_size, wr->size);
        return -EINVAL;
    }

    const uint8_t *payload = (const uint8_t *)args + header_size;

    EBA_INFO("ebp_op_write_handler: buff_id = %llx offset = %llu size = %llu\n",wr->buff_id, wr->offset, wr->size);
     // TODO here inqueue to the invoke queue and send an ack
    //print_hex_dump(KERN_INFO, "ebp_op_write_handler payload :", DUMP_PREFIX_OFFSET, 16, 1,payload, wr->size, true);
    send_invoke_ack_packet(INVOKE_QUEUED,mac,"enp0s8");
    return eba_internals_write(payload, wr->buff_id, wr->offset, wr->size);
}

int ebp_op_alloc_handler(const void *args, uint64_t arg_len, const char mac[6])
{
    if (!args || arg_len < sizeof(struct ebp_op_alloc_args)) {
        EBA_ERR("ebp_op_alloc_handler: invalid arg_len %llu (must be >= %zu) or null pointer %p\n",
               arg_len, sizeof(struct ebp_op_alloc_args), args);
        return -EINVAL;
    }

    const struct ebp_op_alloc_args *alloc_args = args;
    EBA_INFO("ebp_op_alloc_handler: Allocation request received: size = %llu, life_time = %llu, receive bufferID = %llu\n",
            alloc_args->size, alloc_args->life_time, alloc_args->buffer_id);

    void *new_buf = eba_internals_malloc(alloc_args->size, alloc_args->life_time);
    if (!new_buf) {
        EBA_ERR("ebp_op_alloc_handler: Allocation failed for size %llu\n", alloc_args->size);
        return -ENOMEM;
    }
    uint64_t buf_id = (uint64_t)new_buf;
    ebp_remote_write(alloc_args->buffer_id,0,sizeof(buf_id),(char *)&buf_id,mac);
    return 0;
}
int ebp_op_read_handler(const void *args, uint64_t arg_len, const char mac[6])
{
    /* Verify that a valid argument is provided and that it's at least as large as our header. */
    if (!args || arg_len < sizeof(struct ebp_op_read_args)) {
        EBA_ERR("ebp_op_read_handler: invalid arg_len %llu (must be >= %zu) or null pointer %p\n",arg_len, sizeof(struct ebp_op_read_args), args);
        return -EINVAL;
    }
    const struct ebp_op_read_args *rd_args = args;

    EBA_INFO("ebp_op_read_handler: dest_buffer_id = %llu, src_buffer_id = %llu, dst_offset = %llu, src_offset = %llu, size = %llu\n",rd_args->dst_buffer_id,rd_args->src_buffer_id,
         rd_args->dst_offset,rd_args->src_offset, rd_args->size);
    // TODO here inquue to the invoke queue, if it is queued send an ack
    void* read_data = kmalloc(rd_args->size,GFP_KERNEL);
    int ret = eba_internals_read(read_data, rd_args->src_buffer_id, rd_args->src_offset, rd_args->size);
    ebp_remote_write(rd_args->dst_buffer_id,rd_args->dst_offset,rd_args->size,(const char*)read_data,mac);

    kfree(read_data);
    //build packet and send it then free the read_data
    // TODO send the invoke with ( write request to the concerned node with read_data as the payload ! ).
    
    if (ret < 0) {
        EBA_ERR("ebp_op_read_handler: eba_internals_read() failed with error %d\n", ret);
        return ret;
    }

    EBA_INFO("ebp_op_read_handler: Successfully read %llu bytes from src_buffer_id %llu into dest_buffer_id %llu\n", rd_args->size, rd_args->src_buffer_id, rd_args->dst_buffer_id);
    return 0;
}
int ebp_remote_alloc(uint64_t size, uint64_t life_time, uint64_t local_buff_id,const char mac[6]/* TODO modify it to be come node*/)
{
    struct ebp_op_alloc_args alloc_args = {
        .size       = size,
        .life_time  = life_time,
        .buffer_id  = local_buff_id
    };
    send_invoke_req_packet(0x1234,EBP_OP_ALLOC,(char *)&alloc_args,sizeof(alloc_args),NULL,0,mac,"enp0s8");
    EBA_INFO("ebp_remote_alloc: Sent EBP_OP_ALLOC request with local_buf_id = %llu\n",local_buff_id);
    return 0;

}

int ebp_remote_write(uint64_t buff_id, uint64_t offset, uint64_t size,const char* payload ,const char mac[6]/* TODO modify it to be come node*/)
{
    struct ebp_op_write_args write_args = {
        .buff_id = buff_id,
        .offset = offset,
        .size = size
    };
    send_invoke_req_packet(0x1234,EBP_OP_WRITE,(char*)&write_args,sizeof(write_args),payload, write_args.size,mac,"enp0s8");
    EBA_INFO("ebp_remote_write: Sent EBP_OP_WRITE request on buffer %llu\n",buff_id);
    return 0;

}

int ebp_remote_read(uint64_t dst_buffer_id, uint64_t src_buffer_id, uint64_t dst_offset,uint64_t src_offset ,uint64_t size,const char mac[6]/* TODO modify it to be come node*/)
{
    struct ebp_op_read_args read_args = {
        .dst_buffer_id = dst_buffer_id,
        .src_buffer_id = src_buffer_id,
        .dst_offset    = dst_offset,
        .src_offset    = src_offset,
        .size          = size
    };
    send_invoke_req_packet(0x1234,EBP_OP_READ,(char *)&read_args,sizeof(read_args),NULL,0,mac,"enp0s8");
    EBA_INFO("ebp_remote_read: Sent EBP_OP_READ request from  %llu to %llu\n",src_buffer_id,dst_buffer_id);
    return 0;

}
int ebp_register_node(uint16_t mtu, const char mac[6], uint64_t node_specs)
{
    int i;
    for (i = 0; i < MAX_NODE_COUNT; i++) {
        if ((node_infos[i].id != -1) &&
            (memcmp(node_infos[i].mac, mac, sizeof(node_infos[i].mac)) == 0)) {
            EBA_ERR("ebp_register_node: Node with MAC %pM is already registered.\n", mac);
            return -EEXIST;
        }
    }
    for (i = 0; i < MAX_NODE_COUNT; i++) {
        if (node_infos[i].id == -1) {
            node_infos[i].id         = nodes_count; 
            node_infos[i].mtu        = mtu;
            memcpy(node_infos[i].mac, mac, 6);
            node_infos[i].node_specs = node_specs;
            nodes_count++;

            EBA_INFO("ebp_register_node: Registered node with ID=%d, MAC=%pM, MTU=%u, node_specs=%llu\n",
                     i, mac, mtu, node_specs);
            return 0;
        }
    }

    EBA_ERR("ebp_register_node: No space left to register a new node.\n");
    return -ENOSPC;
}


/*
 * write_buffer_to_file() - Writes @count bytes from @data into a file at @path.
 *                          Creates or overwrites the file.
 *
 * Return: 0 on success, negative errno on error.
 */
/*int write_buffer_to_file(const char *path, const void *data, uint64_t count)
{
    struct file *filp;
    loff_t pos = 0;
    suint64_t written;

    filp = filp_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (IS_ERR(filp)) {
        EBA_ERR("write_buffer_to_file: filp_open failed for '%s', err=%ld\n",
                path, PTR_ERR(filp));
        return PTR_ERR(filp);
    }
    written = kernel_write(filp, data, count, &pos);
    if (written < 0 || written != count) {
        EBA_ERR("write_buffer_to_file: kernel_write failed. ret=%zd\n", written);
        filp_close(filp, NULL);
        return (written < 0) ? (int)written : -EIO;
    }

    filp_close(filp, NULL);
    return 0;
}
    */

/*
 * export_all_node_specs - For each valid node, create a file with the name:
 *                         /var/lib/eba/node_<ID>.eba
 *                         and write the contents of node_specs to it.
 *
 * Return: 0 on success if at least one file was written, negative on error
 *         (If no valid nodes exist, returns 0 but writes no files.)
 */
/*int export_all_node_specs(void)
{
    int i, ret = 0;
    char path[128];

    for (i = 0; i < MAX_NODE_COUNT; i++) {
        if (node_infos[i].id != -1) {
            void *spec_ptr = (void *)node_infos[i].node_specs;
            if (!spec_ptr) {
                EBA_ERR("export_all_node_specs: node %d has null node_specs pointer\n", i);
                continue;
            }


            snprintf(path, sizeof(path), "/var/lib/eba/node_%d.eba", node_infos[i].id);


            ret = write_buffer_to_file(path, spec_ptr, EBP_NODE_SPECS_MAX_SIZE);
            if (ret < 0) {
                EBA_ERR("export_all_node_specs: failed to write node %d to %s, ret=%d\n",
                        node_infos[i].id, path, ret);
            } else {
                EBA_INFO("export_all_node_specs: saved node %d specs to %s\n",
                         node_infos[i].id, path);
            }
        }
    }
    return ret;
}
*/

/*
 * read_file_into_pointer() - Reads an entire file at @path into a newly kmalloc'd pointer.
 *                           The pointer is returned in *@out_buf and size in *@out_size.
 *
 * Return: 0 on success, negative errno on error.
 *         Caller must kfree(*out_buf) after use.
 */
/*int read_file_into_pointer(const char *path, char **out_buf, uint64_t *out_size)
{
    struct file *filp;
    mm_segment_t old_fs;
    loff_t size;
    suint64_t read_ret;
    char *buf;

    *out_buf = NULL;
    *out_size = 0;

    filp = filp_open(path, O_RDONLY, 0);
    if (IS_ERR(filp)) {
        EBA_ERR("read_file_into_buffer: filp_open failed for '%s', err=%ld\n",
                path, PTR_ERR(filp));
        return PTR_ERR(filp);
    }

    size = i_size_read(file_inode(filp));
    if (size <= 0) {
        EBA_ERR("read_file_into_buffer: file '%s' has invalid size=%llu\n", path, size);
        filp_close(filp, NULL);
        return -EINVAL;
    }

    buf = kmalloc(size, GFP_KERNEL);
    if (!buf) {
        filp_close(filp, NULL);
        return -ENOMEM;
    }
    read_ret = kernel_read(filp, buf, size, 0);
    filp_close(filp, NULL);

    if (read_ret < 0) {
        EBA_ERR("read_file_into_buffer: kernel_read failed=%zd\n", read_ret);
        kfree(buf);
        return (int)read_ret;
    }

    if (read_ret != size) {
        EBA_ERR("read_file_into_buffer: partial read. got=%zd, expected=%llu\n",
                read_ret, size);
        kfree(buf);
        return -EIO;
    }

    *out_buf  = buf;
    *out_size = size;
    return 0;
}*/
