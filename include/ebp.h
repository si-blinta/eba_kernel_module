/**
 * @file ebp.h
 * @brief EBA Protocol Header File.
 *
 * This header defines the data structures, constants, and function prototypes
 * required to implement the EB protocol for remote buffer allocation,
 * read/write operations, node discovery, and operation invocation.
 *
 * The EB protocol uses custom Ethernet frame types for communication between nodes.
 */
#ifndef EBP_H
#define EBP_H
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/if_ether.h>
#include <linux/errno.h>
#include <linux/types.h> 
#include <linux/atomic.h>

/* EBA Protocol Constants */

/** EBA EtherType used to identify EBA frames in Ethernet communication. */
#define EBP_ETHERTYPE 0xEBA0
/** Maximum size for the node specification buffer. */
#define EBP_NODE_SPECS_MAX_SIZE 4096
/** Default maximum lifetime for node specifications buffer. */
#define EBP_NODE_SPECS_MAX_LIFE_TIME 0
/** Maximum number of nodes that can be registered. */
#define MAX_NODE_COUNT    10
/** Maximum number of invocation entries allowed. */
#define MAX_INVOKE_COUNT  20
/** Maximum number of operation entries allowed in the op_entries array. */
#define MAX_OP_COUNT      20
#define MTU_OVERHEAD 38
#define MINIMAL_MTU 512
#define UNUSED_NODE_ID   0u    
/**
 * enum INVOKE_STATUS - Possible statuses for an invocation.
 * @INVOKE_QUEUED:      Invocation is queued for processing.
 * @INVOKE_IN_PROGRESS: Invocation is currently being processed.
 * @INVOKE_COMPLETED:   Invocation has completed successfully.
 * @INVOKE_FAILED:      Invocation has failed.
 * @INVOKE_DEFAULT:     Default state for an uninitialized invocation.
 */
enum INVOKE_STATUS {
    INVOKE_QUEUED = 0,    
    INVOKE_COMPLETED,      
    INVOKE_FAILED,
    INVOKE_DEFAULT     
};

/**
 * struct node_info - Contains metadata for an EBA node.
 * @id:          Unique identifier for the node.
 * @mtu:         Maximum transmission unit supported by the node.
 * @mac:         6-byte MAC address of the node.
 * @node_specs:  Identifier for the buffer containing node specification details.
 */
struct node_info {
    uint16_t id;
    uint16_t mtu;
    char mac[6];
    uint64_t node_specs;
};

/**
 * struct invoke_tracker - Tracks the status of an EBA protocol invocation.
 * @iid:    Unique identifier for the invocation.
 * @pid:    Process identifier associated with the invocation.
 * @done:   Boolean flag indicating whether the invocation is complete.
 * @status: Current status of the invocation (see INVOKE_STATUS).
 * @wq    : Wait queue
 */
struct invoke_tracker {
    uint32_t iid;                
    pid_t    pid;                
    bool     done;               
    enum INVOKE_STATUS status;   
};

/**
 * @brief Function pointer type for EBA operations.
 *
 * This function pointer is used by each op_entry to reference an operation
 * function. Every EBA operation function must accept a pointer to a raw
 * arguments buffer, the length of the arguments, and the destination MAC address.
 * @iid:         32-bit Invocation ID.
 * @args    Pointer to the raw arguments buffer.
 * @arg_len Length of the arguments in bytes.
 * @node_id:    Target node id.
 * @src_mac   MAC address of the packet sender.
 *
 * @Returns 0 on success, or a negative error code on failure.
 */
typedef int (*ebp_op_t)(uint32_t iid, const void *args,uint64_t arg_len, uint16_t node_id, const unsigned char src_mac[6]);

/**
 * struct op_entry - Represents an operation entry in the EBA protocol.
 * @op_id:   Unique operation identifier.
 * @op_ptr:  Function pointer to the operation implementation.
 *
 * This structure maps operation IDs to the corresponding function
 * implementations that process remote EBA operations.
 */
struct op_entry {
    uint32_t   op_id;
    ebp_op_t  op_ptr;
    
};

/**
 * node_info_array_init - Initialize an array of node_info structures.
 *
 * This function initializes each element in the node_info array by setting:
 * - id to 0,
 * - mtu to 0,
 * - mac to zero,
 * - node_specs to 0.
 *
 * @return 0 on success, or -EINVAL if the provided array pointer is NULL.
 */
int node_info_array_init(void);

/**
 * invoke_tracker_array_init - Initialize an array of invoke_tracker structures.
 *
 * This function initializes each element in the invoke_tracker array by setting:
 * - iid to 0,
 * - pid to 0,
 * - done to false,
 * - status to INVOKE_DEFAULT.
 *
 * @return 0 on success, or -EINVAL if the provided array pointer is NULL.
 */
int invoke_tracker_array_init(void);

/**
 * op_entry_array_init - Initialize an array of op_entry structures.
 *
 * This function initializes each element in the op_entry array by setting:
 * - op_id to 0,
 * - op_ptr to NULL.
 *
 * @return 0 on success, or -EINVAL if the provided array pointer is NULL.
 */
int op_entry_array_init(void);

/* EBA Message Types */
/**
 * enum EBP_MSG - Enumerates the different message types used in the EBA protocol.
 * @EBP_MSG_DISCOVER:     Message to initiate node discovery.
 * @EBP_MSG_INVOKE:       Message to request an operation invocation.
 * @EBP_MSG_DISCOVER_ACK: Response message for a discovery request.
 * @EBP_MSG_INVOKE_ACK:   Acknowledgment message for an invocation request.
 */
enum EBP_MSG
{
    EBP_MSG_INVOKE = 0x01,
    EBP_MSG_INVOKE_ACK = 0x02
};

/**
 * enum EBP_OP_IDS - Enumerates exposed EBA operation identifiers.
 *
 * These identifiers are used to map functions to operations in the op_entry array.
 * @EBP_OP_ALLOC: Operation ID for buffer allocation.
 * @EBP_OP_READ:  Operation ID for reading from a buffer.
 * @EBP_OP_WRITE: Operation ID for writing to a buffer.
 */
enum EBP_OP_IDS {
    EBP_OP_DISCOVER,
    EBP_OP_ALLOC,
    EBP_OP_READ,
    EBP_OP_WRITE,
    EBP_OP_REGISTER_QUEUE,
    EBP_OP_ENQUEUE,
    EBP_OP_DEQUEUE
};

/**
 * struct ebp_op_write_args - Arguments for the exposed EBA "write" operation.
 * @buff_id: Identifier (virtual address) of the target buffer.
 * @offset:  Byte offset within the target buffer to begin writing.
 * @size:    Number of bytes to write.
 *
 * This structure contains the parameters required for a write operation.
 */
struct ebp_op_write_args {
    uint64_t buff_id;
    uint64_t offset; 
    uint64_t size;  
}__attribute__((packed));


/**
 * struct ebp_op_alloc_args - Arguments for the exposed EBA "alloc" operation.
 * @size:       Number of bytes to allocate.
 * @life_time:  Lifetime for the allocation (in a defined time unit).
 * @buffer_id:  Field that the target node will fill with the allocated buffer's unique identifier.
 *
 * These parameters are provided by a remote node to request a buffer allocation.
 */
struct ebp_op_alloc_args {
    uint64_t size;      
    uint64_t life_time; 
    uint64_t buffer_id;  
} __attribute__((packed));


/**
 * struct ebp_op_read_args - Arguments for the exposed EBA "read" operation.
 * @dst_buffer_id: Identifier (virtual address) of the destination buffer where data will be copied.
 * @src_buffer_id: Identifier (virtual address) of the source buffer to read from.
 * @dst_offset:    Byte offset within the destination buffer.
 * @src_offset:    Byte offset within the source buffer to start reading.
 * @size:          Number of bytes to read.
 *
 * This structure specifies the parameters for a remote read operation.
 */
struct ebp_op_read_args {
    uint64_t dst_buffer_id;
    uint64_t src_buffer_id;
    uint64_t dst_offset;
    uint64_t src_offset;
    uint64_t size;
} __attribute__((packed));


/**
 * struct ebp_op_enqueue_args - Arguments for the exposed EBA "enqueue" operation.
 * @buffer_id: Identifier (virtual address) of the target buffer.
 * @size:      Number of bytes to enqueue.
 */
struct ebp_op_enqueue_args {
    uint64_t buffer_id;
    uint64_t size;
} __attribute__((packed));


/**
 * struct ebp_op_dequeue_args - Arguments for the exposed EBA "dequeue" operation.
 * @src_buffer_id: Identifier of the source buffer.
 * @dst_buffer_id: Identifier of the destination buffer.
 * @dst_offset:    Byte offset within the destination buffer.
 * @size:      Number of bytes to dequeue.
 *
 * This structure specifies the parameters for a remote dequeue operation.
 */
struct ebp_op_dequeue_args {
    uint64_t src_buffer_id;
    uint64_t dst_buffer_id;
    uint64_t dst_offset;
    uint64_t size;
} __attribute__((packed));


/**
 * struct ebp_op_register_queue_args - Arguments for the exposed EBA "register queue" operation.
 * @buffer_id: Identifier (virtual address) of the target buffer to register as a queue.
 */
struct ebp_op_register_queue_args {
    uint64_t buffer_id;
} __attribute__((packed));



/**
 * struct ebp_header - Minimal header for EBA protocol messages.
 * @msgType: Indicates the type of EBA message (see enum EBP_MSG).
 *
 * This header is placed immediately after the Ethernet header in an EBA frame.
 * It allows nodes to determine the processing logic based on the message type.
 */
struct ebp_header {
    uint8_t msgType;
} __attribute__((packed));

/**
 * @mtu:    Maximum Transmission Unit of the sender.
 */
struct ebp_op_discover_args {
    uint16_t mtu;
} __attribute__((packed));

/**
 * struct ebp_discover_ack - Structure for an EBA Discover Acknowledgment message.
 * @header:    Header containing the message type (should be set to EBP_MSG_DISCOVER_ACK).
 * @buffer_id: Identifier of the buffer containing node information.
 *
 * The receiving node sends this message in response to a discovery request to provide
 * the sender with the necessary node information.
 */
struct ebp_discover_ack {
    struct ebp_header header;
    uint64_t buffer_id;
} __attribute__((packed));

/**
 * struct ebp_invoke_req - Structure for an EBA Invoke Request message.
 * @header:   Header containing the message type.
 * @iid:      32-bit Invocation ID.
 * @opid:     32-bit Operation ID indicating the requested operation.
 * @args_len: 64-bit length of the arguments blob.
 * @args:     Flexible array member containing the raw arguments.
 *
 * This message is used to remotely invoke an operation on another node.
 * The variable-length arguments are parsed within the handler function.
 */
struct ebp_invoke_req {
    struct ebp_header header;
    uint32_t iid;
    uint32_t opid;
    uint64_t args_len;
    uint8_t args[];  
} __attribute__((packed));

/**
 * struct ebp_invoke_ack - Structure for an EBA Invoke Acknowledgment message.
 * @header: Header containing the message type (should be set to EBP_MSG_INVOKE_ACK).
 * @iid:      32-bit Invocation ID.
 * @status: Status code indicating the result of the invocation.
 * @data  : Operation-specific value.
 * This message is used to acknowledge that an Invoke Request has been processed,
 * reflecting whether the operation was queued, completed, or failed.
 */
struct ebp_invoke_ack {
    struct ebp_header header;
    uint32_t iid;
    uint8_t status;
    uint64_t data;
} __attribute__((packed));


/**
 * ebp_init - Register the EBA packet handler with the Linux networking stack and all other struct arrays related to ebp.
 * @return 0 on success, or a negative error code on failure.
 */
void ebp_init(void);

/**
 * ebp_exit - Unregister the EBA packet handler from the Linux networking stack.
 */
void ebp_exit(void);

/**
 * ebp_op_alloc - Handle a remote buffer allocation request.
 * @iid:      32-bit Invocation ID.
 * @args:    Pointer to the argument provided in the invoke packet.
 * @arg_len: Length of the argument.
 * @node_id:    Target node id.
 *
 * This function processes a remote allocation request by parsing the provided
 * arguments and attempting to allocate a buffer accordingly.
 *
 * @return 0 on success, or 1 on failure.
 */
int ebp_op_alloc(uint32_t iid,const void *args, uint64_t arg_len,uint16_t node_id, const unsigned char src_mac[6]);

/**
 * ebp_op_write - Handle a remote write operation request.
 * @iid:     32-bit Invocation ID.
 * @args:    Pointer to the argument from the invoke packet.
 * @arg_len: Length of the argument data.
 * @node_id: Target node id.
 *
 * This function processes a request to write data to a remote buffer.
 *
 * @return 0 on success, or 1 on failure.
 */
int ebp_op_write(uint32_t iid, const void *args, uint64_t arg_len,uint16_t node_id, const unsigned char src_mac[6]);
/**
 * ebp_op_read - Handle a remote read operation request.
 * @iid:      32-bit Invocation ID.
 * @args:    Pointer to the argument blob from the invoke packet.
 * @arg_len: Length of the argument data.
 * @node_id:    Target node id.
 *
 * This function processes a request to read data from a remote buffer.
 *
 * @return 0 on success, or 1 on failure.
 */
int ebp_op_read(uint32_t iid ,const void *args, uint64_t arg_len, uint16_t node_id, const unsigned char src_mac[6]);

/**
 * ebp_register_op - Register an EBA operation in the global op_entries array.
 * @op_id: Operation identifier.
 * @fn:    Function pointer to the operation implementation.
 *
 * This function associates a given operation ID with its corresponding function in the
 * op_entries array to allow for dynamic invocation.
 *
 * @return 0 on success, or a negative error code on failure.
 */
int ebp_register_op(uint32_t op_id, ebp_op_t fn);

/**
 * ebp_invoke_op - Invoke a registered EBA operation.
 * @iid:      32-bit Invocation ID.
 * @op_id:   Operation identifier to look up.
 * @args:    Pointer to the raw argument data.
 * @arg_len: Length of the argument data in bytes.
 * @node_id:    Target node id.
 *
 * This function searches for the operation associated with the provided op_id and
 * calls the corresponding function.
 *
 * @return The return value of the invoked operation, or a negative error code if not found.
 */
int ebp_invoke_op(uint32_t iid, uint32_t op_id, const void *args, uint64_t arg_len, uint16_t node_id, const unsigned char src_mac[6]);

/**
 * ebp_ops_init - Initialize and register the default EBA operations.
 *
 * This function registers the default set of EBA operations (alloc, read, write)
 * into the global op_entries array.
 *
 * @return 0 on success, or a negative error code on failure.
 */
int ebp_ops_init(void);

/**
 * print_op_entries - Display the current list of operation entries.
 *
 * This utility function prints the registered operation entries for debugging and verification.
 */
void print_op_entries(void);

 /**
 * ebp_op_discover() - new implementation of node discovery
 * @iid:      32-bit Invocation ID
 * @args:     Pointer to ebp_op_discover_args (sender MTU in network order)
 * @arg_len:  Length of @args
 * @node_id:  Not used (node possibly unknown)
 * @src_mac:  MAC address of the sender (needed for registration/ACK)
 *
 * Allocates or re-uses a node-specs buffer, registers the peer, then
 * replies with an INVOKE_ACK whose 8-byte @data field carries the buffer-ID.
 *
 * Return: 0 on success or a negative errno.
 */
int ebp_op_discover(uint32_t iid,const void *args, uint64_t arg_len,
    uint16_t node_id, const unsigned char src_mac[6]);

/**
 * ebp_remote_alloc - Request a buffer allocation from a remote node.
 * @size:          Size of the buffer to allocate.
 * @life_time:     Lifetime of the buffer allocation.
 * @local_buff_id: Local buffer identifier where the allocated buffer's ID will be stored.
 * @node_id:       Target node id, 0 for broadcast.
 * @iid_out:       The invocation id of that call.
 * This function sends a remote allocation request to a distant node.
 *
 * @return 0 on success, or a negative error code on failure.
 */
int ebp_remote_alloc(uint64_t size, uint64_t life_time, uint64_t local_buff_id,uint16_t node_id, uint32_t *iid_out);

/**
 * ebp_remote_write - Write data to a remote pre-allocated buffer.
 * @buff_id: Remote buffer identifier.
 * @offset:  Byte offset in the remote buffer where writing should begin.
 * @size:    Size of the data payload to write.
 * @payload: Pointer to the data payload.
 * @node_id:    Target node id, 0 for broadcast.
 * @iid_out:       The invocation id of that call.
 * This function sends a request to write data to a remote node's buffer.
 *
 * @return 0 on success, or a negative error code on failure.
 */
int ebp_remote_write(uint64_t buff_id, uint64_t offset, uint64_t size,const char* payload ,uint16_t node_id,uint32_t *iid_out);

/**
 * ebp_remote_read - Read data from a remote pre-allocated buffer into a local buffer.
 * @dst_buffer_id: Local buffer identifier where the data will be stored.
 * @src_buffer_id: Remote buffer identifier to read data from.
 * @dst_offset:    Byte offset within the local buffer.
 * @src_offset:    Byte offset within the remote buffer where reading should start.
 * @size:          Number of bytes to read.
 * @node_id:       Target node id, 0 for broadcast.
 * @iid_out:       The invocation id of that call.
 * This function sends a request to read data from a remote node's buffer into a local buffer.
 *
 * @return 0 on success, or a negative error code on failure.
 */
int ebp_remote_read(uint64_t dst_buffer_id, uint64_t src_buffer_id, uint64_t dst_offset,uint64_t src_offset ,uint64_t size,uint16_t node_id,uint32_t *iid_out);

/**
 * ebp_register_node - Register a new node in the global node_infos array.
 * @mtu:        Maximum Transmission Unit (MTU) for the node.
 * @mac:        MAC address of the node.
 * @node_specs: Identifier for the buffer containing node specification details.
 *
 * This function checks if a node with the specified MAC address is already registered.
 * If not, it adds the node to the node_infos array, increments the global node count, and returns 0.
 * If the node is already registered, it returns -EEXIST. If there is no space available, it returns -ENOSPC.
 *
 * @return 0 on success, or a negative error code on failure.
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
        TODO finish the discovery
*/

/**
 * print_node_infos - prints the node info array.
 */
void print_node_infos(void);

/**
 * ebp_get_node_id_from_mac - Look up a node ID by its MAC address
 * @mac:  Pointer to a 6-byte MAC address array
 *
 * Searches the global node_infos array for a node whose MAC address matches
 * @mac. If found, returns its node ID. Otherwise, returns -1.
 *
 * Return: Node ID on success, or -1 if not found
 */
int ebp_get_node_id_from_mac(const char mac[6]);

/**
 * ebp_get_mac_from_node_id - Retrieve MAC address based on node ID
 * @node_id:  Node ID to search for
 *
 * Searches node_infos for the entry whose .id matches @node_id. If found,
 * returns a pointer to the 6-byte MAC address. Otherwise, returns NULL.
 *
 * Return: Pointer to MAC address on success, or NULL if not found
 */
const unsigned char *ebp_get_mac_from_node_id(uint16_t node_id);

/**
 * ebp_get_mtu_from_node_id - Fetch MTU for a given node ID
 * @node_id:  Node ID to look up
 *
 * Searches node_infos for the entry whose .id matches @node_id. If found,
 * returns its MTU. Otherwise, returns 0.
 *
 * Return: MTU (uint16_t) on success, or 0 if not found
 */
uint16_t ebp_get_mtu_from_node_id(int node_id);

/**
 * ebp_get_specs_from_node_id - Return the node_specs field for a node ID
 * @node_id:  Node ID to look up
 *
 * Searches node_infos for the entry whose .id matches @node_id. If found,
 * returns its node_specs field. Otherwise, returns 0.
 *
 * Return: node_specs (uint64_t) on success, or 0 if not found
 */
uint64_t ebp_get_specs_from_node_id(int node_id);

/**
 * ebp_get_specs_from_node_mac - Return the node_specs field for a node ID
 * @mac_address:  mac to look up
 *
 * Searches node_infos for the entry whose .mac matches @mac_address. If found,
 * returns its node_specs field. Otherwise, returns 0.
 *
 * Return: node_specs (uint64_t) on success, or 0 if not found
 */
uint64_t ebp_get_specs_from_node_mac(const char* mac_address);

/**
 * ebp_remote_write_mtu - Write a remote buffer in segments not exceeding the node's MTU.
 * @node_id:    The ID of the remote node.
 * @buff_id:    The remote buffer identifier to write into.
 * @total_size: The total number of bytes of the data to be written.
 * @payload:    Pointer to the data to be written.
 *
 * This function retrieves the MTU for the node identified by @node_id, and then
 * breaks the provided payload into segments of size at most equal to the MTU.
 * For each segment, it invokes ebp_remote_write() with the current offset.
 *
 * Returns 0 on success or a negative error code on failure.
 */
int ebp_remote_write_mtu(int node_id, uint64_t buff_id, uint64_t total_size, const char *payload);

int ebp_discover(void);
/**
 * ebp_remote_write_fixed_mtu_mac() - Send data in MTU-bounded chunks using an explicit destination MAC.
 * @dest_mac:    The mac of the remote node.
 * @forced_mtu:  The caller-specified MTU to use for chunking.
 * @buff_id:     The remote buffer identifier to write into.
 * @total_size:  The total number of bytes in @payload.
 * @payload:     Pointer to the data to send.
 *
 * This function does not register the node nor look up any existing MTU in
 * node_infos[].  It simply uses @forced_mtu as the maximum segment size.
 *
 * Returns 0 on success, or a negative error code if any chunk fails.
 */
int ebp_remote_write_fixed_mtu_mac(const unsigned char dest_mac[6],uint16_t forced_mtu,
                                    uint64_t buff_id,uint64_t total_size,const char *payload);


/*==================================================*/
/*                  Workqueue                       */
/*==================================================*/

/**
 * struct ebp_work - workqueue item for deferred packet processing
 * @work:     kernel work_struct
 * @skb:      cloned sk_buff to process
 * @dev:      net_device on which skb was received
 */
struct ebp_work {
    struct work_struct work;
    struct sk_buff    *skb;
    struct net_device *dev;
};

/**
 * ebp_work_handler() - actual worker that runs in process context
 * @work:   generic work_struct pointer, container_of() gives ebp_work
 *
 * Logs entry/exit, calls ebp_process_skb(), then frees the work item.
 */
void ebp_work_handler(struct work_struct *work);

/**
 * ebp_handle_packets() - packet_type callback invoked in softirq
 * @skb:      incoming socket buffer
 * @dev:      net_device that received it
 * @pt:       packet_type descriptor
 * @orig_dev: original device pointer
 *
 * Instead of doing all processing here (softirq), we clone the skb,
 * queue it into our workqueue, free the original skb, and return.
 */
int ebp_handle_packets(struct sk_buff *skb, struct net_device *dev, struct packet_type *pt, struct net_device *orig_dev);


/**
 * ebp_process_skb - entry point for deferred packet processing
 * @skb: cloned socket buffer to process
 * @dev: net_device on which it arrived
 *
 * Checks lengths, parses headers, and dispatches to the
 * appropriate packet‑type handler above.
 *
 * Return: NET_RX_SUCCESS or NET_RX_DROP as returned by the handler.
 */
int ebp_process_skb(struct sk_buff *skb,struct net_device *dev);

/**
 * ebp_handle_invoke_ack - handle an incoming EBP_MSG_INVOKE_ACK
 * @skb: the socket buffer containing the packet
 * @dev: the net_device on which this packet arrived
 * @eth: parsed Ethernet header from @skb
 * @hdr: pointer to the start of the EBP header in @skb->data
 *
 * Logs the status field.  Always frees @skb before returning.
 *
 * Return: NET_RX_SUCCESS
 */
int ebp_handle_invoke_ack(struct sk_buff *skb,struct net_device *dev,struct ethhdr *eth,struct ebp_header *hdr);

/**
 * ebp_handle_invoke - handle an incoming EBP_MSG_INVOKE request
 * @skb: the socket buffer containing the packet
 * @dev: the net_device on which this packet arrived
 * @eth: parsed Ethernet header from @skb
 * @hdr: pointer to the start of the EBP header in @skb->data
 *
 * Extracts IID/OpID/args from the request and calls ebp_invoke_op().
 * Always frees @skb before returning.
 *
 * Return: NET_RX_SUCCESS if the operation was queued/succeeded,
 *         NET_RX_DROP on error.
 */
int ebp_handle_invoke(struct sk_buff *skb,struct net_device *dev,struct ethhdr *eth,struct ebp_header *hdr);



/* ===================================================================== */
/*                       INVOCATION–WAIT SUPPORT                         */
/* ===================================================================== */

/**
 * struct iid_waiter - one sleeping task waiting for an Invoke-ACK
 * @iid:            Invocation-ID of interest (0 == slot unused)
 * @wanted_status:  ACK status that will wake the task
 * @task:           pointer to the task_struct that is sleeping
 * @done:           set to 1 by the waker before *task* is readied
 * @rc:             return code copied back to user (-ETIMEDOUT, 0, …)
 */
struct iid_waiter {
    uint32_t           iid;
    uint8_t            wanted_status;
    struct task_struct *task;
    int                done;
    int                rc;
};

#define MAX_WAITERS   128          /* static table – small on purpose   */
/**
 * iid_waiter_alloc() - reserve a slot in iid_waiters[]
 *
 * @iid:           Invocation-ID that the future waiter will watch
 * @wanted_stat:   status byte that will wake it
 * @tsk:           sleeping task_struct (may be NULL for pre-registration)
 *
 * Return: pointer to the freshly initialised slot, or NULL if the table is
 *         full.  Caller must hold waiter_lock.
 */
struct iid_waiter * iid_waiter_alloc(u32 iid, u8 wanted_stat, struct task_struct *tsk);

/* ===================================================================== */
/*                       BUFFER–WAIT SUPPORT                             */
/* ===================================================================== */

/**
 * struct buffer_waiter - Represents a task waiting for a specific buffer.
 * @buffer_id: The unique identifier of the buffer being waited on.
 * @task:      Pointer to the task_struct representing the waiting task.
 * @done:      Set to 1 when the buffer operation is complete.
 * @rc:        Return code indicating the result of the buffer operation.
 *
 * This structure is used to manage tasks that are waiting for a specific
 * buffer operation (e.g., write completion) to complete. It allows for
 * synchronization and signaling between the buffer operation and the
 * waiting task.
 */
struct buffer_waiter {
    uint64_t buffer_id;
    struct task_struct *task;
    int done;
    int rc;
};

#define MAX_BUFFER_WAITERS   128          /* static table – small on purpose   */
/**
 * buffer_waiter_alloc - Allocates a buffer waiter structure.
 * @buffer_id: The unique identifier of the buffer.
 * @tsk: Pointer to the task_struct representing the task associated with the buffer waiter.
 *
 * This function creates and initializes a buffer waiter structure for the specified
 * buffer ID and task. It is typically used to manage tasks waiting on a specific buffer.
 *
 * Return: Pointer to the allocated buffer_waiter structure, or NULL on failure.
 */
struct buffer_waiter * buffer_waiter_alloc(uint64_t buffer_id, struct task_struct *tsk);

/**
 * dump_iid_waiters - Dumps information about all IID waiters.
 *
 * This function outputs debugging information about all the IID waiters
 * currently being tracked. It is useful for debugging and monitoring
 * the state of IID waiters in the system.
 */
void dump_iid_waiters(void);

/**
 * dump_buffer_waiters - Dumps information about all buffer waiters.
 *
 * This function outputs debugging information about all the buffer waiters
 * currently being tracked. It is useful for debugging and monitoring
 * the state of buffer waiters in the system.
 */
void dump_buffer_waiters(void);


int ebp_op_enqueue(uint32_t iid, const void *args, uint64_t arg_len,uint16_t node_id, const unsigned char src_mac[6]);

int ebp_remote_enqueue(uint64_t buff_id, uint64_t size, const char *payload, uint16_t node_id,uint32_t *iid_out);

int ebp_op_dequeue(uint32_t iid,const void *args, uint64_t arg_len,uint16_t node_id, const unsigned char src_mac[6]);


int ebp_remote_dequeue(uint64_t dst_buffer_id, uint64_t src_buffer_id, uint64_t dst_offset, uint64_t size, uint16_t node_id,uint32_t *iid_out);
int ebp_remote_register_queue(uint64_t buff_id, uint16_t node_id,uint32_t *iid_out);
int ebp_op_register_queue(uint32_t iid,const void *args, uint64_t arg_len,uint16_t node_id, const unsigned char src_mac[6]);

#endif