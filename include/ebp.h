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
#define MAX_OP_COUNT      5
#define MTU_OVERHEAD 38
#define MINIMAL_MTU 512

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
    INVOKE_IN_PROGRESS,    
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
 */
struct invoke_tracker {
    uint32_t iid;
    pid_t pid;
    bool done;
    enum INVOKE_STATUS status;
};

/**
 * @brief Function pointer type for EBA operations.
 *
 * This function pointer is used by each op_entry to reference an operation
 * function. Every EBA operation function must accept a pointer to a raw
 * arguments buffer, the length of the arguments, and the destination MAC address.
 *
 * @args    Pointer to the raw arguments buffer.
 * @arg_len Length of the arguments in bytes.
 * @mac     Destination MAC address.
 *
 * @Returns 0 on success, or a negative error code on failure.
 */
typedef int (*ebp_op_t)(const void *args, uint64_t arg_len, const char mac[6]);

/**
 * struct op_entry - Represents an operation entry in the EBA protocol.
 * @op_id:   Unique operation identifier.
 * @op_ptr:  Function pointer to the operation implementation.
 *
 * This structure maps operation IDs to the corresponding function
 * implementations that process remote EBA operations.
 */
struct op_entry {
    uint32_t          op_id;
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
    EBP_MSG_DISCOVER = 0x01,
    EBP_MSG_INVOKE,
    EBP_MSG_DISCOVER_ACK,
    EBP_MSG_INVOKE_ACK
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
    EBP_OP_ALLOC = 1,
    EBP_OP_READ,
    EBP_OP_WRITE
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
 * struct ebp_discover_req - Structure for an EBA Discover Request message.
 * @header: Header containing the message type (should be set to EBP_MSG_DISCOVER).
 * @mtu:    Maximum Transmission Unit of the sender.
 *
 * This message is broadcast by a node during initialization to discover neighboring nodes.
 * It can be exposed as an API also.
 */
struct ebp_discover_req {
    struct ebp_header header;
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
 * @status: Status code indicating the result of the invocation.
 *
 * This message is used to acknowledge that an Invoke Request has been processed,
 * reflecting whether the operation was queued, completed, or failed.
 */
struct ebp_invoke_ack {
    struct ebp_header header;
    uint8_t status;
} __attribute__((packed));


/**
 * ebp_handle_packets - Callback for processing received Ethernet frames.
 * @skb:      Pointer to the socket buffer containing the received packet.
 * @dev:      Network device that received the packet.
 * @pt:       Pointer to the packet_type structure associated with the packet.
 * @orig_dev: Original network device.
 *
 * This function is registered with the networking stack to handle Ethernet frames
 * with the EBP_ETHERTYPE protocol. It processes the received packet and returns an
 * appropriate status code.
 *
 * @return NET_RX_SUCCESS on successful processing, or an error code if processing fails.
 */
int ebp_handle_packets(struct sk_buff *skb, struct net_device *dev, struct packet_type *pt, struct net_device *orig_dev);

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
 * @args:    Pointer to the argument provided in the invoke packet.
 * @arg_len: Length of the argument.
 * @mac:     Destination MAC address of the invoking node.
 *
 * This function processes a remote allocation request by parsing the provided
 * arguments and attempting to allocate a buffer accordingly.
 *
 * @return 0 on success, or 1 on failure.
 */
int ebp_op_alloc(const void *args, uint64_t arg_len, const char mac[6]);

/**
 * ebp_op_write - Handle a remote write operation request.
 * @args:    Pointer to the argument from the invoke packet.
 * @arg_len: Length of the argument data.
 * @mac:     Destination MAC address of the invoking node.
 *
 * This function processes a request to write data to a remote buffer.
 *
 * @return 0 on success, or 1 on failure.
 */
int ebp_op_write(const void *args, uint64_t arg_len, const char mac[6]);

/**
 * ebp_op_read - Handle a remote read operation request.
 * @args:    Pointer to the argument blob from the invoke packet.
 * @arg_len: Length of the argument data.
 * @mac:     Destination MAC address of the invoking node.
 *
 * This function processes a request to read data from a remote buffer.
 *
 * @return 0 on success, or 1 on failure.
 */
int ebp_op_read(const void *args, uint64_t arg_len, const char mac[6]);

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
 * @op_id:   Operation identifier to look up.
 * @args:    Pointer to the raw argument data.
 * @arg_len: Length of the argument data in bytes.
 * @mac:     Destination MAC address of the invoking node.
 *
 * This function searches for the operation associated with the provided op_id and
 * calls the corresponding function.
 *
 * @return The return value of the invoked operation, or a negative error code if not found.
 */
int ebp_invoke_op(uint32_t op_id, const void *args, uint64_t arg_len, const char mac[6]);

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
 * ebp_remote_alloc - Request a buffer allocation from a remote node.
 * @size:          Size of the buffer to allocate.
 * @life_time:     Lifetime of the buffer allocation.
 * @local_buff_id: Local buffer identifier where the allocated buffer's ID will be stored.
 * @mac:           MAC address of the remote node (to be modified to a node ID in the future).
 *
 * This function sends a remote allocation request to a distant node.
 *
 * @return 0 on success, or a negative error code on failure.
 */
int ebp_remote_alloc(uint64_t size, uint64_t life_time, uint64_t local_buff_id,const char mac[6]/* TODO modify it to be come node*/);

/**
 * ebp_remote_write - Write data to a remote pre-allocated buffer.
 * @buff_id: Remote buffer identifier.
 * @offset:  Byte offset in the remote buffer where writing should begin.
 * @size:    Size of the data payload to write.
 * @payload: Pointer to the data payload.
 * @mac:     MAC address of the remote node (to be modified to a node ID in the future).
 *
 * This function sends a request to write data to a remote node's buffer.
 *
 * @return 0 on success, or a negative error code on failure.
 */
int ebp_remote_write(uint64_t buff_id, uint64_t offset, uint64_t size,const char* payload ,const char mac[6]/* TODO modify it to be come node*/);

/**
 * ebp_remote_read - Read data from a remote pre-allocated buffer into a local buffer.
 * @dst_buffer_id: Local buffer identifier where the data will be stored.
 * @src_buffer_id: Remote buffer identifier to read data from.
 * @dst_offset:    Byte offset within the local buffer.
 * @src_offset:    Byte offset within the remote buffer where reading should start.
 * @size:          Number of bytes to read.
 * @mac:           MAC address of the remote node (to be modified to a node ID in the future).
 *
 * This function sends a request to read data from a remote node's buffer into a local buffer.
 *
 * @return 0 on success, or a negative error code on failure.
 */
int ebp_remote_read(uint64_t dst_buffer_id, uint64_t src_buffer_id, uint64_t dst_offset,uint64_t src_offset ,uint64_t size,const char mac[6]/* TODO modify it to be come node*/);

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
const unsigned char *ebp_get_mac_from_node_id(int node_id);

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
 * ebp_remote_write_fixed_mtu() - Send data in chunks constrained by a given MTU.
 * @mac:         The remote node's MAC address (does not need to be registered).
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
int ebp_remote_write_fixed_mtu(const unsigned char *mac,uint16_t forced_mtu,uint64_t buff_id,uint64_t total_size,const char *payload);
#endif