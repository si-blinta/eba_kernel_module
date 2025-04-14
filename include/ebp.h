#ifndef EBP_H
#define EBP_H
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/if_ether.h>
#include <linux/errno.h>
#include <linux/types.h> 
#define EBP_ETHERTYPE 0xEBA0
#define EBP_NODE_SPECS_MAX_SIZE 512
#define EBP_NODE_SPECS_MAX_LIFE_TIME 0
#define MAX_NODE_COUNT    10
#define MAX_INVOKE_COUNT  20
#define MAX_OP_COUNT      5

enum INVOKE_STATUS {
    INVOKE_QUEUED = 0,   
    INVOKE_IN_PROGRESS,    
    INVOKE_COMPLETED,      
    INVOKE_FAILED,
    INVOKE_DEFAULT     
};

/**
 * struct node_info - Contains metadata for an EBA node.
 * @param id:         Unique identifier for the node.
 * @param mtu:        Maximum transmission unit supported by the node.
 * @param mac:        6-byte MAC address.
 * @param node_specs: Buffer id containing node specification details.
 */
struct node_info {
    uint16_t id;
    uint16_t mtu;
    char mac[6];
    uint64_t node_specs;
};

/**
 * struct invoke_tracker - Tracks an invocation in the EBA protocol.
 * @param iid:    Invocation identifier.
 * @param pid:    Process identifier corresponding to the invocation.
 * @param done:   Flag indicating whether the invocation is complete.
 * @param status: Current status of the invocation.
 */
struct invoke_tracker {
    uint32_t iid;
    pid_t pid;
    bool done;
    enum INVOKE_STATUS status;
};

/**
 * @brief The function pointer type used by each op_entry.
 * 
 * All operation handlers take a pointer to the raw arguments buffer 
 * plus its length and destination mac address.
 * Inside the handler, you cast and parse `args` as needed.
 */
typedef int (*ebp_op_handler_t)(const void *args, uint64_t arg_len, const char mac[6]);

/**
 * @struct op_entry
 * @brief Represents an operation entry in the EBA protocol.
 * @param op_id   Operation identifier.
 * @param op_ptr  Pointer to the corresponding operation function (ebp_op_handler_t).
 */
struct op_entry {
    uint32_t          op_id;
    ebp_op_handler_t  op_ptr;
    
};

/**
 * node_info_array_init - Initialize an array of node_info structures.
 * - id is set to 0.
 * - mtu is set to 0.
 * - mac is zeroed out.
 * - node_specs is set to 0.
 *
 * @returns: 0 on success, or -EINVAL if info_array is NULL.
 */
int node_info_array_init(void);

/**
 * invoke_tracker_array_init - Initialize an array of invoke_tracker structures.
 * - iid is set to 0.
 * - pid is set to 0.
 * - done is set to false.
 * - status is set to INVOKE_DEFAULT.
 *
 * @returns: 0 on success, or -EINVAL if tracker_array is NULL.
 */
int invoke_tracker_array_init(void);

/**
 * op_entry_array_init - Initialize an array of op_entry structures.
 * - op_id is set to 0.
 * - op_ptr is set to NULL.
 *
 * @returns: 0 on success, or -EINVAL if op_array is NULL.
 */
int op_entry_array_init(void);

/* EBA Message Types */
enum EBP_MSG
{
    EBP_MSG_DISCOVER = 0x01,
    EBP_MSG_INVOKE,
    EBP_MSG_DISCOVER_ACK,
    EBP_MSG_INVOKE_ACK
};

/**
 * @brief Enumerates the internal EBA operation IDs.
 * 
 * You can map these IDs to the functions or wrappers that will be stored
 * in the op_entry array. Make sure these do not collide with any other
 * IDs you use in your system.
 */
enum EBP_OP_IDS {
    EBP_OP_ALLOC = 1,
    EBP_OP_READ,
    EBP_OP_WRITE
};

/**
 * @brief Arguments for the internal EBA "write" operation.
 * @param buff_id: The identifier (virtual address) of the target buffer. 
 * @param offset: The offset (in bytes) from where to start writing.
 * @param size:  The number of bytes to write..
 *              unique identifier
 */
struct ebp_op_write_args {
    uint64_t buff_id;
    uint64_t offset; 
    uint64_t size;  
}__attribute__((packed));


/**
 * @brief Arguments for the internal EBA "alloc" operation.
 *
 * These parameters are provided by the remote node to request a buffer allocation.
 * @param size:       The number of bytes to allocate.
 * @param life_time:  The lifetime (in a defined unit) of the allocation.
 * @param buffer_id:  A field that the target node will fill in with the allocated buffer's
 *              unique identifier.
 */
struct ebp_op_alloc_args {
    uint64_t size;      
    uint64_t life_time; 
    uint64_t buffer_id;  
} __attribute__((packed));


/**
 * @brief Arguments for the internal EBA "read" operation.
 *
 * These parameters are provided by a remote node to request a read operation.
 *
 * @param dest_buffer_id: The identifier (virtual address) of the destination buffer where the data will be copied. ( distant node )
 * @param src_buffer_id:  The identifier (virtual address) of the source buffer to read from.
 * @param dst_offset:         The offset (in bytes) within the destination buffer.
 * @param src_offset:         The offset (in bytes) within the source buffer from where the read should start.
 * @param size:           The number of bytes to read.
 */
struct ebp_op_read_args {
    uint64_t dst_buffer_id;
    uint64_t src_buffer_id;
    uint64_t dst_offset;
    uint64_t src_offset;
    uint64_t size;
} __attribute__((packed));


/**
 * struct ebp_header - Minimal EBA protocol header.
 * @param msgType: Indicates the type of EBA message.
 *
 * This header is placed immediately after the Ethernet header.
 */
struct ebp_header {
    uint8_t msgType;
} __attribute__((packed));

/**
 * struct eba_discover_req - Structure for the EBA Discover Request message.
 * @param msgType: Message type, should be set to EBP_MSG_DISCOVER.
 * @param mtu: The maximum transmission unit (2 bytes) of the sender.
 *
 * This message is broadcast by a node on startup to initiate neighbor discovery.
 */
struct ebp_discover_req {
    struct ebp_header header;
    uint16_t mtu;
} __attribute__((packed));

/**
 * struct ebp_discover_ack - Structure for the EBA Discover Acknowledgment message.
 * @param buffer_id: A 64-bits identifier of the buffer containing node information.
 *
 * The receiving node uses this message to respond to a discovery request.
 */
struct ebp_discover_ack {
    struct ebp_header header;
    uint64_t buffer_id;
} __attribute__((packed));

/**
 * struct ebp_invoke_req - Structure for the EBA Invoke Request message.
 * @param iid:  A 32-bit Invocation ID.
 * @param opid: A 32-bit Operation ID identifying the requested operation.
 * @param args_len: A 64-bit size of args.
 * @param args: A flexible array member for variable-length arguments.
 *
 * This message is used to remotely invoke operations on another node.
 */
struct ebp_invoke_req {
    struct ebp_header header;
    uint32_t iid;
    uint32_t opid;
    uint64_t args_len;
    uint8_t args[];  /* Inline variable-length argument data */
} __attribute__((packed));

/**
 * @param ebp_invoke_ack - Structure for the EBA Invoke Acknowledgment message.
 * @param status: A status code indicating whether the invocation has been queued,
 *          completed, or encountered an error.
 *
 * This message acknowledges a previously received Invoke Request.
 */
struct ebp_invoke_ack {
    struct ebp_header header;
    uint8_t status;
} __attribute__((packed));


 /**
 * ebp_handle_packets - Callback function for processing received Ethernet frames.
 * @param skb: Pointer to the socket buffer containing the packet.
 * @param dev: The network device that received the packet.
 * @param pt: Pointer to the packet_type structure.
 * @param orig_dev: The original network device (if the packet was received via bridging or similar).
 *
 * This function is called by the networking stack for each received Ethernet packet
 * that matches the registered protocol.
 *
 * @returns: NET_RX_SUCCESS if the packet was processed successfully,or an appropriate drop code on error.
 */
int ebp_handle_packets(struct sk_buff *skb, struct net_device *dev, struct packet_type *pt, struct net_device *orig_dev);

/**
* Registers our packet handler so that Ethernet frames with the specified protocol
* are delivered to our callback.
*
* @returns: 0 on success or a negative error code on failure.
*/
void ebp_init(void);

/*
* Unregisters the packet handler from the networking stack.
*/
void ebp_exit(void);

/**
 * @brief This function will handle the alloc operation when invoked remotely.
 * @param args Pointer to the invoke packet’s argument blob.
 * @param arg_len Length of the arguments.
 * @param mac dest mac address.
 * @returns 0 on success or 1 on fail.
 */
int ebp_op_alloc_handler(const void *args, uint64_t arg_len, const char mac[6]);

/**
 * @brief This function will handle the write operation when invoked remotely.
 * @param args Arguments from the invoke packet.
 * @param arg_len Length of the arguments.
 * @param mac dest mac address.
 * @returns 0 on succes 1 on fail.
 */
int ebp_op_write_handler(const void *args, uint64_t arg_len, const char mac[6]);

/**
 * @brief This function will handle the read operation when invoked remotely.
 * @param args    Pointer to the invoke packet's argument.
 * @param arg_len Length of the arguments.
 * @param mac dest mac address.
 * @returns 0 on success or 1 on fail.
 */
int ebp_op_read_handler(const void *args, uint64_t arg_len, const char mac[6]);

/**
 * @brief Registers an operation into the global op_entries array.
 * @param op_id The operation ID
 * @param fn    The function pointer implementing that ID.
 * @return 0 on success, negative on failure.
 */
int ebp_register_op(uint32_t op_id, ebp_op_handler_t fn);

/**
 * @brief Finds and calls the handler in op_entries by op_id.
 * @param op_id    The ID to look for.
 * @param args     Pointer to raw argument data.
 * @param arg_len  Length of the argument data in bytes.
 * @param mac dest mac address.
 * @return The handler's return value, or a negative error if not found.
 */
int ebp_invoke_op(uint32_t op_id, const void *args, uint64_t arg_len, const char mac[6]);

/**
 * EBP_OPs_init - Registers the default EBA operations in the op_entries array.
 *
 * Return: 0 on success, negative error code on partial success/failure.
 */
int ebp_ops_init(void);

/**
 * @brief Utility function that prints available operation entries.
 * 
 */
void print_op_entries(void);

/**
 * @brief This function requests a buffer allocation from distant node. 
 * @param size Size of the buffer.
 * @param life_time Life time of the buffer.
 * @param local_buff_id The local buffer that will store the requested buffer id.
 * @param mac Remote node address. ( todo modify it to node id )
 * @returns 0 on success and negative on fail.
 */
int ebp_remote_alloc(uint64_t size, uint64_t life_time, uint64_t local_buff_id,const char mac[6]/* TODO modify it to be come node*/);

/**
 * @brief This function writes to a remote pre allocated buffer. 
 * @param buff_id Remote buffer id.
 * @param offset Offset.
 * @param size size of the payload.
 * @param mac Remote node address. ( todo modify it to node id )
 * @returns 0 on success and negative on fail.
 */
int ebp_remote_write(uint64_t buff_id, uint64_t offset, uint64_t size,const char* payload ,const char mac[6]/* TODO modify it to be come node*/);



/**
 * @brief This function reads from a remote pre allocated buffer into a local pre allocated buffer. 
 * @param dst_buffer_id destination buffer id (local buffer).
 * @param src_buffer_id source buffer id (remote buffer).
 * @param dst_offset Offset on the destination buffer (local buffer ).
 * @param src_offset Offset on the source buffer ( remote buffer ).
 * @param size size of the data to read.
 * @param mac Remote node address. ( todo modify it to node id )
 * @returns 0 on success and negative on fail.
 */
int ebp_remote_read(uint64_t dst_buffer_id, uint64_t src_buffer_id, uint64_t dst_offset,uint64_t src_offset ,uint64_t size,const char mac[6]/* TODO modify it to be come node*/);



/**
 * @brief Register a new node in the global node_infos array.
 * @param mtu:        MTU (Maximum Transmission Unit) for this node.
 * @param mac:        MAC address of the node.
 * @param node_specs: Buffer id containing node specification details.
 *
 * This function checks if the node identified by @param mac is already registered.
 * If not, it finds the first free slot in the node_infos array, populates it,
 * increments the global nodes_count, and returns 0. Returns -EEXIST if the node
 * (MAC) is already in the array, or -ENOSPC if there is no space to register
 * a new node.
 *
 * Return: 0 on success, negative errno code on failure.
 */
int ebp_register_node(uint16_t mtu, const char mac[6], uint64_t node_specs);

/*
    TODO:
    For this node ebp implementation : 
        Implement the invoke queue 
        Implement the full node discovery 
        ( modify the node_info struct to have the args structure so when we respond
        we build the corresponding packet perfectly )
        Add buffer types. ( disk )
        Handle MTU for each operation, make sure it uses the min(local mtu,remote mtu). 
*/
#endif