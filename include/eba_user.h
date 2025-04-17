/**
 * @file eba_user.h
 * @brief EBA User API Header.
 *
 * This header declares the user-space API functions for local and remote buffer
 * operations within the EBA system. The API includes interfaces for buffer allocation,
 * read and write operations on locally allocated buffers, and remote operations executed
 * via IOCTL. These functions abstract the lower-level details from user-space applications.
 */
#ifndef EBA_USER_H
#define EBA_USER_H
#include "eba.h"    /* Public IOCTL definitions and structures for the kernel module */
#include <fcntl.h>  /* open() */
#include <stdio.h>  /* printf(), perror() */
#include <stdlib.h> /* exit() */
#include <stdint.h>
#include <sys/ioctl.h> /* ioctl() */
#include <unistd.h>    /* close() */
#include <string.h>    /* memset() */

/* WARNING , if we update the informations on ebp.h they must be updated here !*/

#define MAX_NODE_COUNT    10 
/**
 * eba_alloc - Request a buffer from the local node.
 * @size:      The size (in bytes) for the buffer allocation.
 * @life_time: The duration of the allocation in seconds; 0 indicates "infinite".
 * @type:      The type of memory requested (depends on node specifications).
 *
 * Return: The allocated buffer ID on success, or 0 on failure.
 */
uint64_t eba_alloc(uint64_t size, uint64_t life_time,uint8_t type);

/**
 * eba_write - Write data to a locally allocated buffer.
 * @data:    Pointer to the data to be written.
 * @buff_id: Identifier of the target local buffer.
 * @off:     The offset (in bytes) within the buffer to begin writing.
 * @size:    The number of bytes to write.
 *
 * Return: 0 on success, or 1 on failure.
 */
int eba_write(const void* data, uint64_t buff_id, uint64_t off, uint64_t size);

/**
 * eba_read - Read data from a locally allocated buffer.
 * @data_out: Pointer to the destination where data should be copied.
 * @buff_id:  Identifier of the target local buffer.
 * @off:      The offset (in bytes) within the buffer to begin reading.
 * @size:     The number of bytes to read.
 *
 * Return: 0 on success, or 1 on failure.
 */
int eba_read(void* data_out, uint64_t buff_id, uint64_t off, uint64_t size);

/*
 * The following remote API functions use IOCTL calls to communicate with
 * kernel-space routines. The MAC address parameters should eventually be
 * replaced with node IDs.
 */

/**
 * eba_remote_alloc - Request a buffer allocation on a remote node via IOCTL.
 * @size:          The size (in bytes) of the requested buffer.
 * @life_time:     The life time for the allocation in seconds; 0 indicates "infinite".
 * @local_buff_id: The local buffer identifier where the remote buffer ID will be stored.
 * @mac:           The MAC address of the remote node.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int eba_remote_alloc(uint64_t size, uint64_t life_time, uint64_t local_buff_id,const char mac[6]/* TODO modify it to be come node*/);

/**
 * eba_remote_write - Write data to a remote allocated buffer via IOCTL.
 * @buff_id: The identifier of the remote buffer.
 * @offset:  The offset (in bytes) within the remote buffer where writing should begin.
 * @size:    The number of bytes of the payload to write.
 * @payload: Pointer to the data payload.
 * @mac:     The MAC address of the remote node.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int eba_remote_write(uint64_t buff_id, uint64_t offset, uint64_t size,const char* payload ,const char mac[6]/* TODO modify it to be come node*/);

/**
 * eba_remote_read - Read data from a remote allocated buffer via IOCTL.
 * @dst_buffer_id: Local destination buffer identifier.
 * @src_buffer_id: Remote source buffer identifier.
 * @dst_offset:    The offset (in bytes) within the destination buffer where data should be written.
 * @src_offset:    The offset (in bytes) within the source buffer where reading begins.
 * @size:          The number of bytes to read.
 * @mac:           The MAC address of the remote node.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int eba_remote_read(uint64_t dst_buffer_id, uint64_t src_buffer_id, uint64_t dst_offset,uint64_t src_offset ,uint64_t size,const char mac[6]/* TODO modify it to be come node*/);

/**
 * eba_discover - Initiate remote node discovery from user space.
 *
 * This function issues an IOCTL command (EBA_IOCTL_DISCOVER) to the EBA driver,
 * causing the kernel module to broadcast a Discover Request packet.
 *
 * @return 0 on success, or a negative error code on failure.
 */
int eba_discover(void);

/**
 * eba_export_node_specs - Exports each node's node_specs buffer to a file.
 *
 * This function iterates through the global node_infos array and, for each valid node,
 * locates the corresponding memory buffer for node_specs and dumps its content to a file
 * with a name like "/var/lib/eba/node_<node_id>_specs".
 *
 * @return 0 on success, or a negative error code on failure.
 */
int eba_export_node_specs(void);

/**
 * eba_export_node_specs - Export node specification buffers to files.
 *
 * This function issues an IOCTL command (EBA_IOCTL_EXPORT_NODE_SPECS) to the EBA driver,
 * causing the kernel module to dump each registered node's node_specs buffer to a file.
 *
 * @return 0 on success, or a negative error code on failure.
 */
int eba_export_node_specs(void);

/**
 * eba_get_node_infos() - fetch the list of registered nodes from kernel
 * @out:       pre‐allocated array of struct eba_node_info (size = max)
 * @out_count: actual number of entries returned
 *
 * Returns 0 on success, ‑1 on error.
 */
int eba_get_node_infos(struct eba_node_info *out,uint64_t *out_count);

#endif