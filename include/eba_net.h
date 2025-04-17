/**
 * @file eba_net.h
 * @brief EBA Networking Interface Header.
 *
 * This header defines the networking functions for the EBA system. It includes routines
 * for sending raw Ethernet packets, retrieving and setting network interface MTU values, and
 * building and transmitting various EBA protocol packets (e.g. Discover Request/ACK,
 * Invoke Request/ACK). These interfaces facilitate EBA communication over the network.
 */
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
 * @payload:     Pointer to the payload data to include in the packet.
 * @payload_len: Length of the payload in bytes.
 * @dst_mac:     Pointer to the destination MAC address (typically a 6-byte array).
 * @protocol:    Ethernet protocol type (e.g., ETH_P_IP, ETH_P_ARP) in host byte order.
 * @ifname:      Network interface name (e.g., "eth0") through which the packet is transmitted.
 *
 * This function allocates an sk_buff and constructs the Ethernet header by setting the
 * destination and source MAC addresses as well as the specified protocol. It then appends
 * the provided payload and transmits the packet using the appropriate network device.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int send_raw_ethernet_packet(const unsigned char *payload, size_t payload_len,const unsigned char *dst_mac,int protocol,const char *ifname);

/**
 * eba_net_get_max_mtu - Retrieve the maximum MTU supported by the specified network device.
 * @ifname:  Network device name (e.g., "eth0").
 * Return: max mtu on success or a negative error code on failure.
 */
int eba_net_get_max_mtu(const char *ifname);

/**
 * eba_net_get_current_mtu - Retrieve the current MTU of the specified network device.
 * @ifname:  Network device name (e.g., "eth0").
 *
 * Return: current mtu on success or a negative error code on failure.
 */
int eba_net_get_current_mtu(const char *ifname);

/**
 * eba_net_set_mtu - Change the MTU of the specified network device.
 * @ifname:  Network device name (e.g., "eth0").
 * @new_mtu: The new MTU value to set.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int eba_net_set_mtu(const char *ifname, int new_mtu);

/**
 * build_discover_req_packet - Build a Discover Request packet.
 * @mtu:     MTU value to embed in the discover request.
 * @out_len: Pointer to a variable that returns the total packet length.
 *
 * This function builds and returns a newly allocated Discover Request packet.
 * The caller is responsible for freeing the allocated packet.
 *
 * Return: Pointer to the packet on success, or NULL on failure.
 */
char *build_discover_req_packet(uint16_t mtu, uint64_t *out_len);

/**
 * build_discover_ack_packet - Build a Discover Acknowledgment packet.
 * @buffer_id: 64-bit identifier to embed in the ACK packet.
 * @out_len:   Pointer to a variable that returns the total packet length.
 *
 * This function builds and returns a newly allocated Discover-Ack packet.
 * The caller is responsible for freeing the packet.
 *
 * Return: Pointer to the allocated packet on success, or NULL on failure.
 */
char *build_discover_ack_packet(uint64_t buffer_id, uint64_t *out_len);

/**
 * build_invoke_req_packet - Build an Invoke Request packet.
 * @iid:         32-bit Invocation ID.
 * @opid:        32-bit Operation ID (e.g., EBP_OP_WRITE, EBP_OP_READ).
 * @args:        Pointer to the invoke argument data.
 * @args_len:    Length (in bytes) of the invoke argument data.
 * @payload:     Pointer to additional payload data appended after the arguments.
 * @payload_len: Length (in bytes) of the payload data.
 * @out_len:     Pointer to a variable that returns the total packet length.
 *
 * The packet is built with the following layout:
 *
 *    [ebp_invoke_req header] | [args data] | [payload data]
 *
 * The caller is responsible for freeing the returned packet.
 *
 * Return: Pointer to the allocated packet on success, or NULL on failure.
 */

char *build_invoke_req_packet(uint32_t iid, uint32_t opid,
    const char *args, uint64_t args_len,
    const char *payload, uint64_t payload_len,
    uint64_t *out_len);

/**
 * build_invoke_ack_packet - Build an Invoke Acknowledgment packet.
 * @status:  8-bit status code (enum INVOKE_STATUS) to embed in the ACK.
 * @out_len: Pointer to a variable that returns the total packet length.
 *
 * This function builds and returns a newly allocated Invoke-Ack packet. The
 * caller is responsible for freeing the allocated packet.
 *
 * Return: Pointer to the allocated packet on success, or NULL on failure.
 */
char *build_invoke_ack_packet(uint8_t status, uint64_t *out_len);

/**
 * send_discover_req_packet - Build and send an EBP_MSG_DISCOVER packet.
 * @mtu:      MTU value to embed in the discover request.
 * @dest_mac: Destination node's MAC address.
 * @ifname:   Outgoing interface name (e.g., "enp0s8").
 *
 * Return: 0 on success or a negative error code on failure.
 */
int send_discover_req_packet(uint16_t mtu, const unsigned char dest_mac[6], const char *ifname);

/**
 * send_discover_ack_packet - Build and send an EBP_MSG_DISCOVER_ACK packet.
 * @buffer_id: 64-bit buffer identifier to embed in the ACK.
 * @dest_mac:  Destination node's MAC address.
 * @ifname:    Outgoing interface name.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int send_discover_ack_packet(uint64_t buffer_id, const unsigned char dest_mac[6], const char *ifname);

/**
 * send_invoke_req_packet - Build and send an EBP_MSG_INVOKE request packet.
 * @iid:         Invocation ID (caller-defined).
 * @opid:        Operation ID (e.g., EBP_OP_WRITE, EBP_OP_READ).
 * @args:        Pointer to the arguments buffer.
 * @args_len:    Length of the arguments buffer.
 * @payload:     Pointer to optional payload data appended after the arguments.
 * @payload_len: Length of the optional payload.
 * @dest_mac:    Destination node's MAC address.
 * @ifname:      Outgoing interface name.
 *
 * This is a generic sender for an Invoke Request. Specialized helper functions,
 * such as those implementing remote allocations or writes, may also call
 * build_invoke_req_packet() directly.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int send_invoke_req_packet(uint32_t iid, uint32_t opid,
    const char *args, uint64_t args_len,
    const void *payload, uint64_t payload_len,
    const unsigned char dest_mac[6],
    const char *ifname);



/**
 * send_invoke_ack_packet - Build and send an EBP_MSG_INVOKE_ACK response packet.
 * @status:   Status code to embed in the ACK.
 * @dest_mac: Destination node's MAC address.
 * @ifname:   Outgoing interface name.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int send_invoke_ack_packet(uint8_t status,
                           const unsigned char dest_mac[6],
                           const char *ifname);

#endif