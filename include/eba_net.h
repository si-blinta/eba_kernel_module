#ifndef EBA_NET
#define EBA_NET
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/if_ether.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/string.h>
#include "ebp.h"
/**
 * send_raw_ethernet_packet - Send a raw Ethernet packet.
 * @param payload:    Pointer to the payload data to include in the packet.
 * @param payload_len: Length of the payload in bytes.
 * @param dst_mac:    Pointer to the destination MAC address (typically a 6-byte array).
 * @param protocol:   Ethernet protocol type (e.g., ETH_P_IP, ETH_P_ARP) in host byte order.
 * @param ifname:     Network interface name (e.g., "eth0") through which the packet should be transmitted.
 *
 * This function allocates an sk_buff, constructs the Ethernet header including setting the 
 * destination and source MAC addresses and the specified protocol, appends the provided payload, 
 * and then transmits the packet using the appropriate network device. On success, the packet is 
 * handed to the networking stack for transmission.
 *
 * @returns: 0 on success, or a negative error code on failure.
 */
int send_raw_ethernet_packet(const unsigned char *payload, size_t payload_len,const unsigned char *dst_mac,int protocol,const char *ifname);

/**
 * eba_net_get_max_mtu - Retrieves the maximum MTU supported by the specified network device.
 * @param ifname: The network device name (e.g., "eth0").
 * @param max_mtu: Pointer to an integer where the maximum MTU value will be stored.
 *
 * @returns 0 on success or a negative error code on failure.
 */
int eba_net_get_max_mtu(const char *ifname, int *max_mtu);

/**
 * eba_net_set_mtu - Changes the MTU of the specified network device.
 * @param ifname: The network device name (e.g., "eth0").
 * @param new_mtu: The new MTU value to set.
 *
 * @returns 0 on success or a negative error code on failure.
 */
int eba_net_set_mtu(const char *ifname, int new_mtu);

/** 
 * @brief Build a Discover Request packet.
 * @param mtu: the MTU value to send.
 * @param out_len: returns the total packet length. ( must be passed as a pointer init with value 0)
 * @returns Packet that needs to be freed 
 */
char *build_discover_req_packet(uint16_t mtu, uint64_t *out_len);

/**
 * @brief Build a Discover-Ack packet.
 * @param buffer_id: the 64-bit identifier.
 * @param out_len: returns the packet length. ( must be passed as a pointer init with value 0)
 * @returns Packet that needs to be freed 
 */
char *build_discover_ack_packet(uint64_t buffer_id, uint64_t *out_len);

/**
 * @brief Build an Invoke Request packet.
 *
 * This function builds an invoke request packet with the following layout:
 *
 *   [ebp_invoke_req header] | [args data] | [payload data]
 *
 * The fixed header (of type 'struct ebp_invoke_req') is followed immediately by
 * the 'args' section, and then by the 'payload' section.
 *
 * @param iid:      32-bit Invocation ID.
 * @param opid:     32-bit Operation ID.
 * @param args:     Pointer to the invoke argument data.
 * @param args_len: Length (in bytes) of the invoke argument data.
 * @param payload:  Pointer to the additional payload data.
 * @param payload_len: Length (in bytes) of the payload data.
 * @param out_len:  Returns the total packet length. (Must be passed as a pointer initialized to 0)
 *
 * @returns A pointer to the allocated packet. The caller is responsible for freeing it.
 */
char *build_invoke_req_packet(uint32_t iid, uint32_t opid,
    const char *args, uint64_t args_len,
    const char *payload, uint64_t payload_len,
    uint64_t *out_len);

/**
 * @brief Build an Invoke Request packet.
 * @param status: 8-bit status code.(enum INVOKE_STATUS)
 * @param out_len: the packet length. ( must be passed as a pointer init with value 0)
 * @returns Packet that needs to be freed !.
 */
char *build_invoke_ack_packet(uint8_t status, uint64_t *out_len);

/**
 * send_discover_req_packet() - Build & send an EBP_MSG_DISCOVER packet
 * @param mtu:         MTU to embed in the discover request
 * @param dest_mac:    MAC address of the destination node
 * @param ifname:      Name of the outgoing interface (e.g. "enp0s8")
 *
 * Returns: 0 on success, negative errno otherwise
 */
int send_discover_req_packet(uint16_t mtu, const unsigned char dest_mac[6], const char *ifname);

/**
 * send_discover_ack_packet() - Build & send an EBP_MSG_DISCOVER_ACK packet
 * @param buffer_id:   64-bit buffer_id to embed in the ACK
 * @param dest_mac:    MAC address of the destination node
 * @param ifname:      Name of the outgoing interface
 *
 * Returns: 0 on success, negative errno otherwise
 */
int send_discover_ack_packet(uint64_t buffer_id, const unsigned char dest_mac[6], const char *ifname);

/**
 * send_invoke_req_packet() - Build & send a generic EBP_MSG_INVOKE request packet
 * @param iid:         Invocation ID (caller-defined)
 * @param opid:        Operation ID (e.g., EBP_OP_WRITE, EBP_OP_READ, etc.)
 * @param args:        Pointer to the arguments buffer
 * @param args_len:    Length of the arguments buffer
 * @param payload:     Pointer to optional payload data appended after the args
 * @param payload_len: Length of the optional payload
 * @param dest_mac:    Destination MAC address
 * @param ifname:      Outgoing interface name
 *
 * Returns: 0 on success, negative errno otherwise
 *
 * Note: This is a generic sender for an Invoke Request. Specialized helper
 *       functions (like ebp_remote_alloc, ebp_remote_write, etc.) may also
 *       call build_invoke_req_packet() directly as done in the code above.
 */
int send_invoke_req_packet(uint32_t iid, uint32_t opid,
    const char *args, uint64_t args_len,
    const void *payload, uint64_t payload_len,
    const unsigned char dest_mac[6],
    const char *ifname);



/**
 * send_invoke_ack_packet() - Build & send an EBP_MSG_INVOKE_ACK response packet
 * @status:      Status code to embed in the ACK
 * @dest_mac:    Destination MAC address
 * @ifname:      Outgoing interface name
 *
 * Returns: 0 on success, negative errno otherwise
 */
int send_invoke_ack_packet(uint8_t status,
                           const unsigned char dest_mac[6],
                           const char *ifname);

#endif