#ifndef EBP_H
#define EBP_H
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/if_ether.h>
#include <linux/errno.h>
#include <linux/types.h> 
#define EBA_ETHERTYPE 0xEBA0

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
 * struct op_entry - Represents an operation entry in the EBA protocol.
 * @param op_id:  Operation identifier.
 * @param op_ptr: Pointer to the corresponding operation function.
 */
struct op_entry {
    uint16_t op_id;
    void* op_ptr;
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
enum EBA_MSG
{
    EBA_MSG_DISCOVER = 0x01,
    EBA_MSG_INVOKE,
    EBA_MSG_DISCOVER_ACK,
    EBA_MSG_INVOKE_ACK
};

/**
 * struct eba_header - Minimal EBA protocol header.
 * @param msgType: Indicates the type of EBA message.
 *
 * This header is placed immediately after the Ethernet header.
 */
struct eba_header {
    uint8_t msgType;
} __attribute__((packed));

/**
 * struct eba_discover_req - Structure for the EBA Discover Request message.
 * @param msgType: Message type, should be set to EBA_MSG_DISCOVER.
 * @param mtu: The maximum transmission unit (2 bytes) of the sender.
 *
 * This message is broadcast by a node on startup to initiate neighbor discovery.
 */
struct eba_discover_req {
    struct eba_header header;
    uint16_t mtu;
} __attribute__((packed));

/**
 * struct eba_discover_ack - Structure for the EBA Discover Acknowledgment message.
 * @param buffer_id: A 64-bits identifier of the buffer containing node information.
 *
 * The receiving node uses this message to respond to a discovery request.
 */
struct eba_discover_ack {
    struct eba_header header;
    uint64_t buffer_id;
} __attribute__((packed));

/**
 * struct eba_invoke_req - Structure for the EBA Invoke Request message.
 * @param iid:  A 32-bit Invocation ID.
 * @param opid: A 32-bit Operation ID identifying the requested operation.
 * @param args_len: A 64-bit size of args.
 * @param args: A flexible array member for variable-length arguments.
 *
 * This message is used to remotely invoke operations on another node.
 */
struct eba_invoke_req {
    struct eba_header header;
    uint32_t iid;
    uint32_t opid;
    uint64_t args_len;
    uint8_t args[];  /* Inline variable-length argument data */
} __attribute__((packed));

/**
 * struct eba_invoke_ack - Structure for the EBA Invoke Acknowledgment message.
 * @param status: A status code indicating whether the invocation has been queued,
 *          completed, or encountered an error.
 *
 * This message acknowledges a previously received Invoke Request.
 */
struct eba_invoke_ack {
    struct eba_header header;
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
 * that matches the registered protocol. You can inspect the packet contents (e.g., the payload,
 * Ethernet header) and process it as required.
 *
 * @returns: NET_RX_SUCCESS if the packet was processed successfully,or an appropriate drop code (e.g., NET_RX_DROP) on error.
 */
int ebp_handle_packets(struct sk_buff *skb, struct net_device *dev, struct packet_type *pt, struct net_device *orig_dev);

char *build_discover_req_packet(uint16_t mtu, size_t *out_len);
char *build_discover_ack_packet(uint64_t buffer_id, size_t *out_len);
char *build_invoke_req_packet(uint32_t iid, uint32_t opid,
    const char *args, uint64_t args_len,
    size_t *out_len);
char *build_invoke_ack_packet(uint8_t status, size_t *out_len);















/**
*
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
#endif