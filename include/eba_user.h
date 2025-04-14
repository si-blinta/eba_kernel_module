#ifndef EBA_USER_H
#define EBA_USER_H
#include <stdint.h>
/**
 * @brief This function requests a buffer from the local node.
 * @param size size of the allocation.
 * @param life_time duration of the allocation 0 = "infinite".
 * @param type type of memory ( depending on the node specifications ).
 * @returns bufID on success and 0 if it fails.
 */
uint64_t eba_alloc(uint64_t size, uint64_t life_time,uint8_t type);

/**
 * @brief This function writes data to a local allocated buffer.
 * @param data pointer to the data.
 * @param buf_id target local buffer id.
 * @param off offset.
 * @param size size of the data.
 * @returns 0 on success and 1 if it fails.
 */
int eba_write(const void* data, uint64_t buff_id, uint64_t off, uint64_t size);

/**
 * @brief This function reads data from a local allocated buffer.
 * @param data pointer to the data out.
 * @param buf_id target local buffer id.
 * @param off offset.
 * @param size size of the data.
 * @returns 0 on success and 1 if it fails.
 */
int eba_read(void* data_out, uint64_t buff_id, uint64_t off, uint64_t size);

/**
 * @brief This function requests a buffer from a distant node.
 * @param node_id id of the target node.
 * @param size size of the allocation. 
 * @param life_time duration fo the allocation 0 = "infinite". 
 * @param type type of memory ( depending on the remote node specifications ).
 */
//int eba_remote_alloc(uint16_t node_id,uint64_t size, uint64_t life_time,uint8_t type);

/**
 * @brief This function writes data to a local allocated buffer.
 * @param data pointer to the data.
 * @param buf_id target local buffer id.
 * @param off offset.
 * @param size size of the data.
 * @returns 0 on success and 1 if it fails.
 */
//int eba_remote_write(uint16_t node_id, const void* data, uint64_t buff_id, uint64_t off, uint64_t size);




/**
 * @brief user API, it uses IOCTL to call ebp_remote_alloc. 
 * @param size Size of the buffer.
 * @param life_time Life time of the buffer.
 * @param local_buff_id The local buffer that will store the requested buffer id.
 * @param mac Remote node address. ( todo modify it to node id )
 * @returns 0 on success and negative on fail.
 */
int eba_remote_alloc(uint64_t size, uint64_t life_time, uint64_t local_buff_id,const char mac[6]/* TODO modify it to be come node*/);

/**
 * @brief user API, it uses IOCTL to call ebp_remote_write. 
 * @param buff_id Remote buffer id.
 * @param offset Offset.
 * @param size size of the payload.
 * @param mac Remote node address. ( todo modify it to node id )
 * @returns 0 on success and negative on fail.
 */
int eba_remote_write(uint64_t buff_id, uint64_t offset, uint64_t size,const char* payload ,const char mac[6]/* TODO modify it to be come node*/);

/**
 * @brief user API, it uses IOCTL to call ebp_remote_read. 
 * @param dst_buffer_id destination buffer id (local buffer).
 * @param src_buffer_id source buffer id (remote buffer).
 * @param dst_offset Offset on the destination buffer (local buffer ).
 * @param src_offset Offset on the source buffer ( remote buffer ).
 * @param size size of the data to read.
 * @param mac Remote node address. ( todo modify it to node id )
 * @returns 0 on success and negative on fail.
 */
int eba_remote_read(uint64_t dst_buffer_id, uint64_t src_buffer_id, uint64_t dst_offset,uint64_t src_offset ,uint64_t size,const char mac[6]/* TODO modify it to be come node*/);

#endif