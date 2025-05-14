#include "ebp.h"
#include "eba_internals.h"
#include "eba_net.h"
#include <linux/delay.h>
#include <linux/workqueue.h>
#include "eba.h"
#include "eba_utils.h"

struct iid_waiter iid_waiters[MAX_WAITERS];
spinlock_t waiter_lock;        /* initialised in ebp_init() */
spinlock_t buffer_waiter_lock;        /* initialised in ebp_init() */
struct buffer_waiter buffer_waiters[MAX_BUFFER_WAITERS];
spinlock_t node_info_lock;          /* initialised in ebp_init() */

atomic_t ebp_iid_ctr = ATOMIC_INIT(1);

/* Return the next unique 32-bit Invocation-ID */
static inline uint32_t ebp_next_iid(void)
{
        return (uint32_t)atomic_inc_return_relaxed(&ebp_iid_ctr);
}

uint64_t local_specs = 0; /* local node‑specs buffer, allocated in ebp_init() */

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

    EBA_DBG("%s: skb=%p\n", __func__, wb->skb);

    /* Call the full packet‐processing routine (frees wb->skb internally) */
    ebp_process_skb(wb->skb, wb->dev);

    EBA_INFO("%s: finished work for skb %p\n", __func__, wb->skb);

    /* Free our work descriptor */
    kfree(wb);
}

int ebp_handle_packets(struct sk_buff *skb, struct net_device *dev, struct packet_type *pt, struct net_device *orig_dev)
{
    struct ebp_work *wb;
    struct sk_buff  *cloned_skb;
    EBA_DBG("%s: skb=%p dev=%s\n", __func__, skb, dev->name);

    if (!skb) {
        EBA_ERR("%s: skb is NULL\n", __func__);
        return NET_RX_DROP;
    }

    /* Clone skb for deferred processing */
    cloned_skb = skb_clone(skb, GFP_ATOMIC);
    if (!cloned_skb) {
        EBA_ERR("%s: skb_clone() failed\n", __func__);
        kfree_skb(skb);
        return NET_RX_DROP;
    }
    EBA_INFO("%s: cloned %p -> %p\n", __func__, skb, cloned_skb);

    /* Allocate and initialize our work item */
    wb = kmalloc(sizeof(*wb), GFP_ATOMIC);
    if (!wb) {
        EBA_ERR("%s: kmalloc(ebp_work) failed\n", __func__);
        kfree_skb(cloned_skb);
        kfree_skb(skb);
        return NET_RX_DROP;
    }
    INIT_WORK(&wb->work, ebp_work_handler);
    wb->skb      = cloned_skb;
    wb->dev      = dev;

    EBA_INFO("%s: queueing work for skb %p\n", __func__, cloned_skb);
    queue_work(ebp_wq, &wb->work);

    /* Free the original skb immediately; our clone lives on */
    kfree_skb(skb);
    return NET_RX_SUCCESS;
}

/**
 * node_id_to_mac() - look up MAC for a node_id, or print an error.
 * @node_id: the node index you want
 *
 * Return: pointer to 6‑byte MAC if found, or NULL (and an EBA_ERR()) if not.
 */
static const unsigned char *node_id_to_mac(uint16_t node_id);
void ebp_init(void)
{
    EBA_DBG("%s: entry\n", __func__);
    /* Create our single‑threaded workqueue ( simpler for now )*/
    ebp_wq = create_singlethread_workqueue("ebp_wq");
    if (!ebp_wq) {
        EBA_ERR("%s: create_singlethread_workqueue failed\n", __func__);
    }
    EBA_INFO("%s: workqueue ebp_wq created\n", __func__);
    /* Register so that ebp_handle_packets() is invoked for every frame that matches our Ethertype. */
    dev_add_pack(&ebp_packet_type);
    spin_lock_init(&waiter_lock);
    spin_lock_init(&node_info_lock);
    spin_lock_init(&buffer_waiter_lock);
    /* One‑time init of global tables (node list, op registry…). */
    node_info_array_init();
    invoke_tracker_array_init();
    op_entry_array_init();
    ebp_ops_init();

    /* Pre‑allocate and fill the buffer holding *our* node‑specs; lifetime 0 = infinite. */
    local_specs = eba_internals_malloc(EBP_NODE_SPECS_MAX_SIZE, 0);
    if (eba_utils_file_to_buf("/var/lib/eba/node_local.eba",local_specs) < 0) {
        EBA_ERR("%s: reading node_local.eba failed\n", __func__);
    }
    /* Debug */

    for (int i = 0; i < MAX_WAITERS; i++)
        iid_waiters[i].iid = 0;      /* mark slot free                  */

}
void ebp_exit(void)
{
    EBA_DBG("%s: entry\n", __func__);
    /* Unregister the packet handler */
    dev_remove_pack(&ebp_packet_type);
    /* Drain & destroy workqueue */
    destroy_workqueue(ebp_wq);
    EBA_INFO("%s: workqueue destroyed\n", __func__);
}

/* Constants and global arrays for nodes, invokes, and ops */
struct node_info node_infos[MAX_NODE_COUNT];
int nodes_count = 1;
struct invoke_tracker invoke_trackers[MAX_INVOKE_COUNT];
struct op_entry op_entries[MAX_OP_COUNT];

void print_node_infos(void)
{
    int i;
    EBA_DBG("%s: dumping node_infos (count=%d)\n", __func__, nodes_count);
    for (i = 0; i < MAX_NODE_COUNT; i++)
    {
        if (node_infos[i].id != UNUSED_NODE_ID)
        {
            EBA_DBG("%s: slot=%d id=%d MTU=%u MAC=%pM specs=%llu\n",
                __func__,
                i,
                node_infos[i].id,
                node_infos[i].mtu,
                node_infos[i].mac,
                node_infos[i].node_specs);
        }
    }
}

void print_op_entries(void)
{
    int i;
    EBA_DBG("%s: dumping op_entries\n", __func__);
    for (i = 0; i < MAX_OP_COUNT; i++)
    {
        EBA_DBG("%s: slot=%d op_id=%u ptr=%p\n",
            __func__, i,
            op_entries[i].op_id,
            op_entries[i].op_ptr);
    }
}

int node_info_array_init(void)
{
        EBA_DBG("%s\n", __func__);
        spin_lock(&node_info_lock);
        memset(node_infos, 0, sizeof(node_infos));   
        nodes_count = 1;                            
        spin_unlock(&node_info_lock);
        return 0;
}

int invoke_tracker_array_init(void)
{
    EBA_DBG("%s\n", __func__);
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

    for (i = 0; i < MAX_OP_COUNT; ++i) {
        op_entries[i].op_id  = UINT_MAX;
        op_entries[i].op_ptr = NULL;
    }
    return 0;
}
int ebp_register_op(uint32_t op_id, ebp_op_t fn)
{
    EBA_DBG("%s: op_id=%u fn=%p\n", __func__, op_id, fn);
    int i;

    if (!fn)
    {
        EBA_ERR("%s: null function pointer\n", __func__);
        return -EINVAL;
    }

    /* Check if this op_id is already registered */
    for (i = 0; i < MAX_OP_COUNT; i++)
    {
        if (op_entries[i].op_id == op_id)
        {
            EBA_ERR("%s: op_id %u already in slot %d\n",__func__, op_id, i);
            return -EEXIST;
        }
    }

    /* Find a free entry */
    for (i = 0; i < MAX_OP_COUNT; i++)
    {
        if (op_entries[i].op_ptr == NULL && op_entries[i].op_id == UINT_MAX)
        {
            op_entries[i].op_id = op_id;
            op_entries[i].op_ptr = fn;
            EBA_INFO("%s: registered op_id %u at slot %d\n",__func__, op_id, i);
            return 0;
        }
    }
    EBA_ERR("%s: no free slot for op_id %u\n", __func__, op_id);
    return -ENOSPC;
}

int ebp_invoke_op(uint32_t iid, uint32_t op_id, const void *args, uint64_t arg_len, uint16_t node_id, const unsigned char src_mac[6])
{
    EBA_DBG("%s: op_id=%u arg_len=%llu node_id=%u\n",__func__, op_id, arg_len, node_id);
    int i;
    for (i = 0; i < MAX_OP_COUNT; i++)
    {
        if (op_entries[i].op_id == op_id)
        {
            EBA_INFO("%s: calling handler slot %d\n",__func__, i);
            return op_entries[i].op_ptr(iid,args, arg_len, node_id,src_mac);
        }
    }
    EBA_ERR("%s: op_id %u not found\n", __func__, op_id);
    return -EINVAL;
}

int ebp_ops_init(void)
{
    EBA_DBG("%s\n", __func__);
    int ret = 0;
    if (ebp_register_op(EBP_OP_DISCOVER, ebp_op_discover)< 0)
        ret = -1;
    if (ebp_register_op(EBP_OP_ALLOC, ebp_op_alloc) < 0)
        ret = -1;
    if (ebp_register_op(EBP_OP_WRITE, ebp_op_write) < 0)
        ret = -1;
    if (ebp_register_op(EBP_OP_READ, ebp_op_read) < 0)
        ret = -1;
    if (ebp_register_op(EBP_OP_ENQUEUE, ebp_op_enqueue) < 0)
        ret = -1;
    return ret;
}

int ebp_op_write(uint32_t iid, const void *args, uint64_t arg_len,uint16_t node_id, const unsigned char src_mac[6])
{
    EBA_DBG("%s: args=%p arg_len=%llu node_id=%u\n",__func__, args, arg_len, node_id);
    /* Check that args is not NULL and that its is large enough for our fixed-size header */
    if (!args || arg_len < sizeof(struct ebp_op_write_args))
    {
        EBA_ERR("%s: invalid arg_len=%llu (must be ≥%zu) or NULL args=%p\n",__func__, arg_len, sizeof(struct ebp_op_write_args), args);
        return -EINVAL;
    }
    const struct ebp_op_write_args *wr = args;
    uint64_t header_size = sizeof(struct ebp_op_write_args);

    if (arg_len < header_size + wr->size)
    {
        EBA_ERR("%s: args too short: arg_len=%llu header=%llu need payload=%llu\n",__func__, arg_len, header_size, wr->size);
        return -EINVAL;
    }

    const uint8_t *payload = (const uint8_t *)args + header_size;

    EBA_DBG("%s: buff_id=%llx offset=%llu size=%llu\n",__func__, wr->buff_id, wr->offset, wr->size);
    int ret = eba_internals_write(payload, wr->buff_id, wr->offset, wr->size);
    if (ret < 0 )
    {
        EBA_ERR("%s: eba_internals_write failed\n",__func__);
        send_invoke_ack_packet(iid,INVOKE_FAILED,0,src_mac, "enp0s8");
        return ret;
        
    }
        spin_lock(&buffer_waiter_lock);
        for (int i = 0; i < MAX_BUFFER_WAITERS; i++) {
            struct buffer_waiter *bw= &buffer_waiters[i];

            if (bw->buffer_id == wr->buff_id) {

                    bw->done = 1;
                    bw->rc   = 0;          /* success                 */
                    if (bw->task)
                            wake_up_process(bw->task);
            }
        }
        spin_unlock(&buffer_waiter_lock);
    return send_invoke_ack_packet(iid,INVOKE_COMPLETED,0,src_mac, "enp0s8");
}

int ebp_op_enqueue(uint32_t iid, const void *args, uint64_t arg_len,uint16_t node_id, const unsigned char src_mac[6])
{
    EBA_DBG("%s: args=%p arg_len=%llu node_id=%u\n",__func__, args, arg_len, node_id);
    /* Check that args is not NULL and that its is large enough for our fixed-size header */
    if (!args || arg_len < sizeof(struct ebp_op_enqueue_args))
    {
        EBA_ERR("%s: invalid arg_len=%llu (must be ≥%zu) or NULL args=%p\n",__func__, arg_len, sizeof(struct ebp_op_enqueue_args), args);
        return -EINVAL;
    }
    const struct ebp_op_enqueue_args *en = args;
    uint64_t header_size = sizeof(struct ebp_op_enqueue_args);

    if (arg_len < header_size + en->size)
    {
        EBA_ERR("%s: args too short: arg_len=%llu header=%llu need payload=%llu\n",__func__, arg_len, header_size, en->size);
        return -EINVAL;
    }

    const uint8_t *payload = (const uint8_t *)args + header_size;

    EBA_DBG("%s: buff_id=%llx size=%llu\n",__func__, en->buffer_id, en->size);
    int ret = eba_internals_enqueue(en->buffer_id,(void*)payload, en->size);
    if (ret < 0 )
    {
        EBA_ERR("%s: eba_internals_enqueue failed\n",__func__);
        send_invoke_ack_packet(iid,INVOKE_FAILED,0,src_mac, "enp0s8");
        return ret;
        
    }
        spin_lock(&buffer_waiter_lock);
        for (int i = 0; i < MAX_BUFFER_WAITERS; i++) {
            struct buffer_waiter *bw= &buffer_waiters[i];

            if (bw->buffer_id == en->buffer_id) {

                    bw->done = 1;
                    bw->rc   = 0;          /* success                 */
                    if (bw->task)
                            wake_up_process(bw->task);
            }
        }
        spin_unlock(&buffer_waiter_lock);
    return send_invoke_ack_packet(iid,INVOKE_COMPLETED,0,src_mac, "enp0s8");
}


int ebp_op_alloc(uint32_t iid, const void *args, uint64_t arg_len, uint16_t node_id, const unsigned char src_mac[6])
{
    EBA_DBG("%s: args=%p arg_len=%llu node_id=%u\n",__func__, args, arg_len, node_id);
    if (!args || arg_len < sizeof(struct ebp_op_alloc_args))
    {
        EBA_ERR("%s: invalid arg_len=%llu (must be ≥%zu) or NULL args=%p\n",__func__, arg_len, sizeof(struct ebp_op_alloc_args), args);
        return -EINVAL;
    }

    const struct ebp_op_alloc_args *alloc_args = args;
    EBA_DBG("%s: size=%llu life_time=%llu recv_buf=%llu\n",__func__,alloc_args->size,alloc_args->life_time,alloc_args->buffer_id);

    uint64_t buf_id = eba_internals_malloc(alloc_args->size, alloc_args->life_time);
    if (!buf_id)
    {
        EBA_ERR("%s: eba_internals_malloc failed for size=%llu\n",__func__, alloc_args->size);
        send_invoke_ack_packet(iid,INVOKE_FAILED,0,src_mac, "enp0s8");
        return -ENOMEM;
    }
    ebp_remote_write(alloc_args->buffer_id, 0, sizeof(buf_id), (char *)&buf_id, node_id,NULL); 
    return send_invoke_ack_packet(iid,INVOKE_COMPLETED,0,src_mac, "enp0s8");
}

int ebp_op_read(uint32_t iid,const void *args, uint64_t arg_len,uint16_t node_id, const unsigned char src_mac[6])
{
    EBA_DBG("%s: args=%p arg_len=%llu node_id=%u\n",__func__, args, arg_len, node_id);
    /* Verify that a valid argument is provided and that it's at least as large as our header. */
    if (!args || arg_len < sizeof(struct ebp_op_read_args))
    {
        EBA_ERR("%s: invalid arg_len=%llu (must be ≥%zu) or NULL args=%p\n",__func__, arg_len, sizeof(struct ebp_op_read_args), args);
        return -EINVAL;
    }
    const struct ebp_op_read_args *rd_args = args;

    EBA_DBG("%s: dst_buf=%llu src_buf=%llu dst_off=%llu src_off=%llu size=%llu\n",__func__,rd_args->dst_buffer_id,rd_args->src_buffer_id,
        rd_args->dst_offset,rd_args->src_offset,rd_args->size);

    void *read_data = kmalloc(rd_args->size, GFP_ATOMIC);
    int ret = eba_internals_read(read_data, rd_args->src_buffer_id, rd_args->src_offset, rd_args->size);
    if (ret < 0)
    {
        EBA_ERR("%s: eba_internals_read failed rc=%d\n",__func__, ret);;
        send_invoke_ack_packet(iid,INVOKE_FAILED,0,src_mac, "enp0s8");
        kfree(read_data);
        return ret;
    }
    ret = ebp_remote_write(rd_args->dst_buffer_id, rd_args->dst_offset, rd_args->size, (const char *)read_data, node_id,NULL);
    if (ret < 0)
    {
        EBA_ERR("%s: ebp_remote_write failed rc=%d\n",__func__, ret);
        send_invoke_ack_packet(iid,INVOKE_FAILED,0,src_mac, "enp0s8");
        kfree(read_data);
        return ret;
    }

    EBA_DBG("%s: read+forward %llu bytes\n", __func__, rd_args->size);
    return send_invoke_ack_packet(iid,INVOKE_COMPLETED,0,src_mac,"enp0s8");
}

int ebp_op_dequeue(uint32_t iid,const void *args, uint64_t arg_len,uint16_t node_id, const unsigned char src_mac[6])
{
    EBA_DBG("%s: args=%p arg_len=%llu node_id=%u\n",__func__, args, arg_len, node_id);
    /* Verify that a valid argument is provided and that it's at least as large as our header. */
    if (!args || arg_len < sizeof(struct ebp_op_dequeue_args))
    {
        EBA_ERR("%s: invalid arg_len=%llu (must be ≥%zu) or NULL args=%p\n",__func__, arg_len, sizeof(struct ebp_op_dequeue_args), args);
        return -EINVAL;
    }
    const struct ebp_op_dequeue_args *da = args;

    EBA_DBG("%s: dst_buf=%llu src_buf=%llu dst_offset=%llu size=%llu\n",__func__,da->dst_buffer_id,da->src_buffer_id,da->dst_offset,da->size);
    

    void *dequeued_data = kmalloc(da->size, GFP_ATOMIC);
    int ret = eba_internals_dequeue(da->src_buffer_id,dequeued_data, da->size);

    if (ret < 0)
    {
        EBA_ERR("%s: eba_internals_dequeue failed rc=%d\n",__func__, ret);;
        send_invoke_ack_packet(iid,INVOKE_FAILED,0,src_mac, "enp0s8");
        kfree(dequeued_data);
        return ret;
    }
    ret = ebp_remote_write(da->dst_buffer_id, da->dst_offset, da->size, (const char *)dequeued_data, node_id,NULL);

    if (ret < 0)
    {
        EBA_ERR("%s: ebp_remote_write failed rc=%d\n",__func__, ret);
        send_invoke_ack_packet(iid,INVOKE_FAILED,0,src_mac, "enp0s8");
        kfree(dequeued_data);
        return ret;
    }

    EBA_DBG("%s: read+forward %llu bytes\n", __func__, da->size);
    return send_invoke_ack_packet(iid,INVOKE_COMPLETED,0,src_mac,"enp0s8");
}

int ebp_op_register_queue(uint32_t iid,const void *args, uint64_t arg_len,uint16_t node_id, const unsigned char src_mac[6])
{
    EBA_DBG("%s: args=%p arg_len=%llu node_id=%u\n",__func__, args, arg_len, node_id);
    /* Verify that a valid argument is provided and that it's at least as large as our header. */
    if (!args || arg_len < sizeof(struct ebp_op_register_queue_args))
    {
        EBA_ERR("%s: invalid arg_len=%llu (must be ≥%zu) or NULL args=%p\n",__func__, arg_len, sizeof(struct ebp_op_register_queue_args), args);
        return -EINVAL;
    }
    const struct ebp_op_register_queue_args *ra = args;

    EBA_DBG("%s: src_buf=%llu\n",__func__,ra->buffer_id);

    int ret = register_queue(ra->buffer_id);

    if (ret < 0)
    {
        EBA_ERR("%s: eba_internals_register_queue failed rc=%d\n",__func__, ret);;
        send_invoke_ack_packet(iid,INVOKE_FAILED,0,src_mac, "enp0s8");
        return ret;
    }
    EBA_DBG("%s: eba_internals_register_queue rc=%d\n",__func__, ret);
    return send_invoke_ack_packet(iid,INVOKE_COMPLETED,0,src_mac,"enp0s8");
}

static const unsigned char broadcast_mac[6]={0xff,0xff,0xff,0xff,0xff,0xff};

int ebp_remote_alloc(uint64_t size, uint64_t life_time, uint64_t local_buff_id, uint16_t node_id,uint32_t *iid_out)
{
    EBA_DBG("%s: size=%llu life_time=%llu local_id=%llu node_id=%u\n",__func__, 
            size, life_time, local_buff_id, node_id);
    struct ebp_op_alloc_args alloc_args = {
        .size = size,
        .life_time = life_time,
        .buffer_id = local_buff_id};

    
    const unsigned char* dest_mac = NULL;
    if(node_id == 0)
    {
        dest_mac = broadcast_mac;
    }
    else
    {
        dest_mac = node_id_to_mac(node_id);
    }
    if(!dest_mac)
    {
        EBA_ERR("%s: invalid node_id=%u\n", __func__, node_id);
        return -EINVAL;
    }
    uint32_t iid = ebp_next_iid();
    if (iid_out)
                *iid_out = iid;
    int ret = send_invoke_req_packet(iid, EBP_OP_ALLOC, (char *)&alloc_args, sizeof(alloc_args), NULL, 0, dest_mac, "enp0s8");

    
    if (ret < 0)
    {
        EBA_ERR("%s: send_invoke_req_packet rc=%d\n",__func__, ret);
        return ret;
    }

    EBA_DBG("%s: sent remote_alloc local_id=%llu\n",__func__, local_buff_id);
    return 0;
}

int ebp_remote_write(uint64_t buff_id, uint64_t offset, uint64_t size, const char *payload, uint16_t node_id,uint32_t *iid_out)
{
    EBA_DBG("%s: buff_id=%llu off=%llu size=%llu node_id=%u\n",__func__, 
            buff_id, offset, size, node_id);
    struct ebp_op_write_args write_args = {
        .buff_id = buff_id,
        .offset = offset,
        .size = size};
    
    const unsigned char* dest_mac = NULL;
    if(node_id == 0)
    {
        dest_mac = broadcast_mac;
    }
    else
    {
        dest_mac = node_id_to_mac(node_id);
    }
    if(!dest_mac)
    {
        EBA_ERR("%s: invalid node_id=%u\n", __func__, node_id);
        return -EINVAL;
    }
    
    uint32_t iid = ebp_next_iid();
    if (iid_out)
                *iid_out = iid;
    int ret = send_invoke_req_packet(iid, EBP_OP_WRITE, (char *)&write_args, sizeof(write_args), payload, write_args.size,dest_mac , "enp0s8");
    if (ret < 0)
    {
        EBA_ERR("%s: send_invoke_req_packet ret=%d\n",__func__, ret);
        return ret;
    }
    EBA_DBG("%s: sent remote_write buff=%llu size=%llu\n",__func__, buff_id, size);
    return 0;
}

int ebp_remote_read(uint64_t dst_buffer_id, uint64_t src_buffer_id, uint64_t dst_offset, uint64_t src_offset, uint64_t size, uint16_t node_id,uint32_t *iid_out)
{
    EBA_DBG("%s: dst_buffer_id=%llu src_buffer_id=%llu dst_offset=%llu src_offset=%llu size=%llu node_id=%u\n",__func__, 
        dst_buffer_id, src_buffer_id, dst_offset, src_offset, size, node_id);
    struct ebp_op_read_args read_args = {
        .dst_buffer_id = dst_buffer_id,
        .src_buffer_id = src_buffer_id,
        .dst_offset = dst_offset,
        .src_offset = src_offset,
        .size = size};
    
    const unsigned char* dest_mac = NULL;
    if(node_id == 0)
    {
        dest_mac = broadcast_mac;
    }
    else
    {
        dest_mac = node_id_to_mac(node_id);
    }
    if(!dest_mac)
    {
        EBA_ERR("%s: invalid node_id=%u\n", __func__, node_id);
        return -EINVAL;
    }
    
    uint32_t iid = ebp_next_iid();
    if (iid_out)
                *iid_out = iid;
    int ret = send_invoke_req_packet(iid, EBP_OP_READ, (char *)&read_args, sizeof(read_args), NULL, 0, dest_mac, "enp0s8");
    if (ret < 0)
    {
        EBA_ERR("%s: send_invoke_req_packet rc=%d\n",__func__, ret);
        return ret;
    }
    EBA_DBG("%s: sent remote_read src=%llu dst=%llu size=%llu\n",__func__, src_buffer_id, dst_buffer_id, size);
    return 0;
}

int ebp_remote_dequeue(uint64_t dst_buffer_id, uint64_t src_buffer_id, uint64_t dst_offset, uint64_t size, uint16_t node_id,uint32_t *iid_out)
{
    EBA_DBG("%s: dst_buffer_id=%llu src_buffer_id=%llu dst_offset=%llu size=%llu node_id=%u\n",__func__, 
        dst_buffer_id, src_buffer_id, dst_offset, size, node_id);
        
    struct ebp_op_dequeue_args dequeue_args = {
        .dst_buffer_id = dst_buffer_id,
        .src_buffer_id = src_buffer_id,
        .dst_offset = dst_offset,
        .size = size};

    const unsigned char* dest_mac = NULL;
    if(node_id == 0)
    {
        dest_mac = broadcast_mac;
    }
    else
    {
        dest_mac = node_id_to_mac(node_id);
    }
    if(!dest_mac)
    {
        EBA_ERR("%s: invalid node_id=%u\n", __func__, node_id);
        return -EINVAL;
    }
    
    uint32_t iid = ebp_next_iid();
    if (iid_out)
                *iid_out = iid;
    int ret = send_invoke_req_packet(iid, EBP_OP_DEQUEUE, (char *)&dequeue_args, sizeof(dequeue_args), NULL, 0, dest_mac, "enp0s8");
    if (ret < 0)
    {
        EBA_ERR("%s: send_invoke_req_packet rc=%d\n",__func__, ret);
        return ret;
    }
    EBA_DBG("%s: sent remote_dequeue src=%llu dst=%llu size=%llu\n",__func__, src_buffer_id, dst_buffer_id, size);
    return 0;
}

int ebp_remote_enqueue(uint64_t buff_id, uint64_t size, const char *payload, uint16_t node_id,uint32_t *iid_out)
{
    EBA_DBG("%s: buff_id=%llu size=%llu node_id=%u\n",__func__, 
            buff_id, size, node_id);
    
    struct ebp_op_enqueue_args enqueue_args = {
        .buffer_id = buff_id,
        .size = size};

    const unsigned char* dest_mac = NULL;
    if(node_id == 0)
    {
        dest_mac = broadcast_mac;
    }
    else
    {
        dest_mac = node_id_to_mac(node_id);
    }
    if(!dest_mac)
    {
        EBA_ERR("%s: invalid node_id=%u\n", __func__, node_id);
        return -EINVAL;
    }
    
    uint32_t iid = ebp_next_iid();
    if (iid_out)
                *iid_out = iid;
    int ret = send_invoke_req_packet(iid, EBP_OP_ENQUEUE, (char *)&enqueue_args, sizeof(enqueue_args), payload, enqueue_args.size,dest_mac , "enp0s8");
    if (ret < 0)
    {
        EBA_ERR("%s: send_invoke_req_packet ret=%d\n",__func__, ret);
        return ret;
    }
    EBA_DBG("%s: sent remote_enqueue buff=%llu size=%llu\n",__func__, buff_id, size);
    return 0;
}

int ebp_remote_register_queue(uint64_t buff_id, uint16_t node_id,uint32_t *iid_out)
{
    EBA_DBG("%s: buff_id=%llu node_id=%u\n",__func__, buff_id,node_id);

    struct ebp_op_register_queue_args reg_args = {
        .buffer_id = buff_id,};
    const unsigned char* dest_mac = NULL;
    if(node_id == 0)
    {
        dest_mac = broadcast_mac;
    }
    else
    {
        dest_mac = node_id_to_mac(node_id);
    }
    if(!dest_mac)
    {
        EBA_ERR("%s: invalid node_id=%u\n", __func__, node_id);
        return -EINVAL;
    }
    
    uint32_t iid = ebp_next_iid();
    if (iid_out)
                *iid_out = iid;
    int ret = send_invoke_req_packet(iid, EBP_OP_REGISTER_QUEUE, (char *)&reg_args, sizeof(reg_args), NULL,0,dest_mac , "enp0s8");
    if (ret < 0)
    {
        EBA_ERR("%s: send_invoke_req_packet ret=%d\n",__func__, ret);
        return ret;
    }
    EBA_DBG("%s: sent remote_register_quueue buff=%llu\n",__func__, buff_id);
    return 0;
}

int ebp_register_node(uint16_t mtu, const char mac[6], uint64_t node_specs)
{
    EBA_DBG("%s: mtu=%u mac=%p node_specs=%llu\n",__func__, mtu, mac, node_specs);
    int free_slot = -1;
    int i;
    spin_lock(&node_info_lock);
    /* First, check if a node with this MAC already exists. */
    for (i = 0; i < MAX_NODE_COUNT; i++)
    {
        if (node_infos[i].id != UNUSED_NODE_ID && memcmp(node_infos[i].mac, mac, 6) == 0)
        {
            /* Node already registered, return success (no error). */
            EBA_WARN("%s: node already exists slot=%d id=%d\n",__func__, i, node_infos[i].id);
            spin_unlock(&node_info_lock);
            return -EEXIST;
        }
    }

    /* If no existing node found, find the first free slot. */
    for (i = 0; i < MAX_NODE_COUNT; i++)
    {
        if (node_infos[i].id == UNUSED_NODE_ID)
        {
            free_slot = i;
            break;
        }
    }

    if (free_slot == -1)
    {
        EBA_ERR("%s: no space for new node\n", __func__);
        spin_unlock(&node_info_lock);
        return -ENOSPC;
    }

    /* Register the node in the found free slot */
    node_infos[free_slot].id = nodes_count;
    node_infos[free_slot].mtu = mtu;
    memcpy(node_infos[free_slot].mac, mac, 6);
    node_infos[free_slot].node_specs = node_specs;

    EBA_INFO("%s: registered node slot=%d id=%d MTU=%u\n",__func__, free_slot, nodes_count, mtu);

    nodes_count++;
    spin_unlock(&node_info_lock);
    return 0;
}

int ebp_get_node_id_from_mac(const char mac[6])
{
    EBA_DBG("%s: mac=%pM\n", __func__, mac);
    int i;
    spin_lock(&node_info_lock);
    for (i = 0; i < MAX_NODE_COUNT; i++)
    {
        if (node_infos[i].id != UNUSED_NODE_ID &&
            memcmp(node_infos[i].mac, mac, ETH_ALEN) == 0)
        {
            EBA_DBG("%s: found node_id=%u at slot=%d\n",__func__, node_infos[i].id, i);
            spin_unlock(&node_info_lock);
            return node_infos[i].id;
        }
    }
    EBA_ERR("%s: mac %pM not found\n", __func__, mac);
    spin_unlock(&node_info_lock);
    return -1; /* Not found */
}

const unsigned char *ebp_get_mac_from_node_id(uint16_t node_id)
{
    EBA_DBG("%s: node_id=%u\n", __func__, node_id);
    int i;
    spin_lock(&node_info_lock);
    for (i = 0; i < MAX_NODE_COUNT; i++)
    {
        if (node_infos[i].id == node_id)
        {
            EBA_DBG("%s: slot %d -> mac %pM\n",__func__, i, node_infos[i].mac);
            spin_unlock(&node_info_lock);
            return node_infos[i].mac;
        }
    }
    EBA_ERR("%s: node_id=%u not registered\n", __func__, node_id);
    spin_unlock(&node_info_lock);
    return NULL; /* Not found */
}

uint16_t ebp_get_mtu_from_node_id(int node_id)
{
    EBA_DBG("%s: node_id=%d\n", __func__, node_id);
    int i;
    spin_lock(&node_info_lock);
    for (i = 0; i < MAX_NODE_COUNT; i++)
    {
        if (node_infos[i].id == node_id)
        {
            EBA_DBG("%s: node_id=%d mtu=%u\n",__func__, node_id, node_infos[i].mtu);
            spin_unlock(&node_info_lock);
            return node_infos[i].mtu;
        }
    }
    EBA_ERR("%s: node_id=%d not found, returning 0\n",__func__, node_id);
    spin_unlock(&node_info_lock);
    return 0; /* Not found */
}

uint64_t ebp_get_specs_from_node_id(int node_id)
{
    EBA_DBG("%s: node_id=%d\n", __func__, node_id);
    int i;
    spin_lock(&node_info_lock);
    for (i = 0; i < MAX_NODE_COUNT; i++)
    {
        if (node_infos[i].id == node_id)
        {
            EBA_DBG("%s: node_id=%d specs=%llu\n",__func__, node_id, node_infos[i].node_specs);
            spin_unlock(&node_info_lock);
            return node_infos[i].node_specs;
        }
    }
    EBA_ERR("%s: node_id=%d not found, returning 0\n",__func__, node_id);
    spin_unlock(&node_info_lock);
    return 0; /* Not found */
}

uint64_t ebp_get_specs_from_node_mac(const char *mac_address)
{
    EBA_DBG("%s: mac=%pM\n", __func__, mac_address);
    int i;
    spin_lock(&node_info_lock);
    for (i = 0; i < MAX_NODE_COUNT; i++)
    {
        if (memcmp(node_infos[i].mac, mac_address, 6) == 0)
        {
            EBA_DBG("%s: slot %d specs=%llu\n",__func__, i, node_infos[i].node_specs);
            spin_unlock(&node_info_lock);
            return node_infos[i].node_specs;
        }
    }
    spin_unlock(&node_info_lock);
    return 0; /* Not found */
}

int ebp_remote_write_mtu(int node_id, uint64_t buff_id, uint64_t total_size, const char *payload)
{
    EBA_DBG("%s: node_id=%d buff=%llu total_size=%llu\n",__func__, node_id, buff_id, total_size);
    int ret = 0;
    uint16_t mtu;
    uint64_t offset = 0;
    /* Retrieve the MTU for the specified node and substract the header size the min of the local node mtu and the remote one (there is some MTU_OVERHEAD i need figure out why is that,
    i found out using tcpdump that there is some 38 bytes that are added) */
    uint16_t remote_mtu = ebp_get_mtu_from_node_id(node_id);
    uint16_t local_mtu = eba_net_get_current_mtu("enp0s8");
    mtu = min((remote_mtu - (sizeof(struct ebp_invoke_req) + MTU_OVERHEAD)), local_mtu);
    EBA_DBG("%s: Calculated MTU = %u (remote_mtu=%u, local_mtu=%u, overhead=%lu)\n", __func__,mtu, remote_mtu, local_mtu, sizeof(struct ebp_invoke_req) + MTU_OVERHEAD);
    if (mtu <= 0)
    {
        EBA_ERR("%s: invalid mtu=%u\n", __func__, mtu);
        return -EINVAL;
    }

    /* Write the payload in segments, each no larger than the node's MTU */
    while (offset < total_size)
    {
        uint64_t segment_size = total_size - offset;

        if (segment_size > mtu)
            segment_size = mtu;

        ret = ebp_remote_write(buff_id, offset, segment_size, payload + offset, node_id,NULL);
        
        if (ret < 0)
        {
            EBA_ERR("%s: write at offset %llu failed rc=%d\n",__func__, offset, ret);
            return ret;
        }

        offset += segment_size;
    }
    EBA_DBG("%s: finished remote_write_mtu total=%llu\n",__func__, total_size);
    return 0;
}

int ebp_discover(void)
{
    EBA_DBG("%s:\n", __func__);
    int ret;
    struct ebp_op_discover_args dc = {
        .mtu = htons(eba_net_get_current_mtu("enp0s8"))
    };
    if (dc.mtu < 0)
    {
        EBA_ERR("%s: get_current_mtu failed rc=%d\n",__func__, dc.mtu);
        return -1;
    }
    ret =  send_invoke_req_packet(0, EBP_OP_DISCOVER,(char *)&dc, sizeof(dc),NULL, 0,broadcast_mac, "enp0s8");
    if(ret < 0 )
    {
        EBA_ERR("%s: send_discover_req_packet failed rc=%d\n",__func__, ret);
    }
    else
    {
        EBA_DBG("%s: broadcasted discover (mtu=%d)\n",__func__, dc.mtu);
    }
    return ret ;
}
int ebp_remote_write_fixed_mtu_mac(const unsigned char dest_mac[6],uint16_t  forced_mtu,uint64_t  buff_id,
                                   uint64_t  total_size,const char *payload)
{
    EBA_DBG("%s: mac=%pM forced_mtu=%u buff=%llu total=%llu\n",__func__, dest_mac, forced_mtu, buff_id, total_size);
    const uint16_t overhead = sizeof(struct ebp_invoke_req) + MTU_OVERHEAD;
    
    if (!dest_mac) {
        EBA_ERR("%s: NULL dest_mac\n", __func__);
        return -EINVAL;
    }
    if (!payload)
    {
        EBA_ERR("%s: NULL payload\n", __func__);
        return -EINVAL;
    }
    if (forced_mtu <= overhead)
    {
        EBA_ERR("%s: forced_mtu=%u ≤ overhead=%u\n",__func__, forced_mtu, overhead);
        return -EINVAL;
    }

    uint16_t chunk = forced_mtu - overhead;
    uint64_t off   = 0;

    while (off < total_size) {
        uint64_t seg = min((uint64_t)chunk, total_size - off);

        struct ebp_op_write_args wr = {
            .buff_id = buff_id,
            .offset  = off,
            .size    = seg
        };

        int rc = send_invoke_req_packet(ebp_next_iid(), EBP_OP_WRITE,(char *)&wr, sizeof(wr),
                                        payload + off, seg,dest_mac, "enp0s8");
        if (rc < 0)
        {
            EBA_ERR("%s: write_fixed at off=%llu rc=%d\n",__func__, off, rc);
            return rc;
        }

        off += seg;
    }
    EBA_DBG("%s: finished remote_write_fixed total=%llu\n",__func__, total_size);
    return 0;
}

int ebp_handle_invoke(struct sk_buff *skb,struct net_device *dev,
                    struct ethhdr *eth,struct ebp_header *hdr)
{
    EBA_DBG("%s: skb=%p dev=%s\n", __func__, skb, dev->name);
    
    struct ebp_invoke_req *inv = (struct ebp_invoke_req *)hdr;
    uint32_t iid = ntohl(inv->iid);
    uint32_t opid = ntohl(inv->opid);
    uint64_t args_len = be64_to_cpu(inv->args_len);
    int node_id    = ebp_get_node_id_from_mac(eth->h_source);
    int rc;

      /* Sanity check packet length */
    if (skb->len < ETH_HLEN + sizeof(*inv)) {
        EBA_ERR("%s: pkt too short len=%u\n",__func__, skb->len);
        rc = NET_RX_DROP;
        goto out_free;
    }

    EBA_DBG("%s: IID=%u OPID=%u args_len=%llu from %pM\n",__func__, iid, opid, args_len, eth->h_source);

    rc = ebp_invoke_op(iid,opid, inv->args, args_len, node_id,eth->h_source);
    if (rc < 0) {
        EBA_ERR("%s: invoke_op rc=%d\n", __func__, rc);
        rc = NET_RX_DROP;
        goto out_free;
    }
    
    rc = NET_RX_SUCCESS;

out_free:
    kfree_skb(skb);
    return rc;
}

int ebp_handle_invoke_ack(struct sk_buff *skb, struct net_device *dev,
                          struct ethhdr *eth, struct ebp_header *hdr)
{
    EBA_DBG("%s: skb=%p dev=%s\n", __func__, skb, dev->name);
     if (!skb || !dev || !eth || !hdr) {
        EBA_ERR("%s: NULL parameter(s)\n", __func__);
        return -EINVAL;
    }
    struct ebp_invoke_ack *ack = (struct ebp_invoke_ack *)hdr;
    uint64_t data = be64_to_cpu(ack->data);
    EBA_DBG("%s: received ACK status=0x%02x from %pM\n",__func__, ack->status, eth->h_source);
    
    spin_lock(&waiter_lock);
    for (int i = 0; i < MAX_WAITERS; i++) {
            struct iid_waiter *w = &iid_waiters[i];

            if (w->iid &&
                w->iid == ntohl(ack->iid) &&
                w->wanted_status == ack->status) {

                    w->done = 1;
                    w->rc   = 0;          /* success                 */

                    if (w->task)
                            wake_up_process(w->task);
            }
    }
    spin_unlock(&waiter_lock);

    struct eba_buffer *buf = get_buffer_by_id(local_specs) ;
    char* payload = (char*)buf->address;
    /* DISCOVER completed → ship our specs buffer to the peer          */
    if (ack->status == INVOKE_COMPLETED && data) {
        int node_id = ebp_get_node_id_from_mac(eth->h_source);

        if (node_id < 0)   /* peer still unknown ⇒ use direct-MAC path */
            ebp_remote_write_fixed_mtu_mac(eth->h_source, MINIMAL_MTU,data,EBP_NODE_SPECS_MAX_SIZE,payload);
        else               /* peer already registered                  */
            ebp_remote_write_mtu(node_id, data,EBP_NODE_SPECS_MAX_SIZE,payload);
    }

    kfree_skb(skb);
    return NET_RX_SUCCESS;

}
int ebp_process_skb(struct sk_buff *skb,struct net_device *dev)
{
    EBA_DBG("%s: skb=%p dev=%s\n", __func__, skb, dev->name);
    struct ebp_header *hdr;
    struct ethhdr *eth;

    if (WARN_ON_ONCE(!skb))
    {
        EBA_ERR("%s: NULL skb\n", __func__);
        return NET_RX_DROP;
    }

    if (skb->len < ETH_HLEN + sizeof(*hdr)) {
        EBA_ERR("%s: pkt too short len=%u\n", __func__, skb->len);
        kfree_skb(skb);
        return NET_RX_DROP;
    }

    eth = eth_hdr(skb);
    hdr = (struct ebp_header *)skb->data;

    EBA_DBG("%s: dispatch msgType=0x%02x from %pM\n",__func__, hdr->msgType, eth->h_source);

    switch (hdr->msgType) {    
    case EBP_MSG_INVOKE:
        return ebp_handle_invoke(skb, dev, eth, hdr);
    
    case EBP_MSG_INVOKE_ACK:
        return ebp_handle_invoke_ack(skb, dev, eth, hdr);
    
    default:
        EBA_ERR("%s: unknown msgType=0x%02x\n",__func__, hdr->msgType);
        kfree_skb(skb);
        return NET_RX_DROP;
    }
}

static const unsigned char *node_id_to_mac(uint16_t node_id)
{
    EBA_DBG("%s: node_id=%u\n", __func__, node_id);
    const unsigned char *mac = ebp_get_mac_from_node_id(node_id);
    if (!mac) {
        EBA_ERR("%s: invalid node_id=%u\n", __func__, node_id);
    }
    return mac;
}

int ebp_op_discover(uint32_t iid,const void *args, uint64_t arg_len,
                    uint16_t node_id, const unsigned char src_mac[6])
{
    const struct ebp_op_discover_args *dc;
    uint64_t specs_buf;
    int    rc;

    if (!args || arg_len < sizeof(*dc))
        return -EINVAL;

    dc = args;

    /* Allocate a specs buffer (infinite lifetime)     */
    specs_buf = eba_internals_malloc(EBP_NODE_SPECS_MAX_SIZE,
                                     EBP_NODE_SPECS_MAX_LIFE_TIME);
    if (!specs_buf)
        return -ENOMEM;

    /* Register or look-up node                        */
    rc = ebp_register_node(ntohs(dc->mtu), src_mac, specs_buf);
    if (rc == -EEXIST) {                 /* already present -> reuse buffer  */
        eba_internals_free(specs_buf);
        specs_buf = ebp_get_specs_from_node_mac(src_mac);
    } else if (rc < 0) {                 /* fatal error                     */
        eba_internals_free(specs_buf);
        return rc;
    }

    /* ACK with buffer-ID in the new “data” field      */
    return send_invoke_ack_packet(iid,INVOKE_COMPLETED,specs_buf,src_mac,"enp0s8");
}

void dump_iid_waiters(void)
{
    int i;
    EBA_DBG("%s: dumping iid_waiters\n", __func__);
    for (i = 0; i < MAX_WAITERS; i++)
    {
        EBA_DBG("%s: slot=%d iid=%u status=%u task=%p done=%d rc=%d\n",
            __func__, i,
            iid_waiters[i].iid,
            iid_waiters[i].wanted_status,
            iid_waiters[i].task,
            iid_waiters[i].done,
            iid_waiters[i].rc);
    }
}


struct iid_waiter * iid_waiter_alloc(u32 iid, u8 wanted_stat, struct task_struct *tsk)
{
        int i;
        dump_iid_waiters();
        for (i = 0; i < MAX_WAITERS; i++) {
                if (iid_waiters[i].iid == 0) {      /* free            */
                        iid_waiters[i].iid           = iid;
                        iid_waiters[i].wanted_status = wanted_stat;
                        iid_waiters[i].task          = tsk;
                        iid_waiters[i].done          = 0;
                        iid_waiters[i].rc            = -ETIMEDOUT;
                        return &iid_waiters[i];
                }
        }
        return NULL;                                /* table full      */
}



void dump_buffer_waiters(void)
{
    int i;
    EBA_DBG("%s: dumping buffer_waiters\n", __func__);
    for (i = 0; i < MAX_BUFFER_WAITERS; i++)
    {
        EBA_DBG("%s: slot=%d buffer_id=%llu task=%p done=%d rc=%d\n",
            __func__, i,
            buffer_waiters[i].buffer_id,
            buffer_waiters[i].task,
            buffer_waiters[i].done,
            buffer_waiters[i].rc);
    }
}
struct buffer_waiter* buffer_waiter_alloc(uint64_t buffer_id, struct task_struct *tsk)
{
    int i ;
    dump_buffer_waiters();
    for(i = 0; i < MAX_BUFFER_WAITERS; i++)
    {
        if(buffer_waiters[i].buffer_id == 0)
        {
            buffer_waiters[i].buffer_id = buffer_id;
            buffer_waiters[i].task = tsk;
            buffer_waiters[i].done = 0;
            buffer_waiters[i].rc = -ETIMEDOUT;
            return &buffer_waiters[i];
        }
    }
    return NULL; /* table full */
}