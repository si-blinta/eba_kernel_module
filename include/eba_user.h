/**
 * @brief This function requests a buffer from the local node.
 * @param size size of the allocation.
 * @param life_time duration of the allocation 0 = "infinite".
 * @param type type of memory ( depending on the node specifications ).
 * @returns 0 on success and 1 if it fails.
 */
int eba_alloc(uint64_t size, uint64_t life_time,uint8_t type);

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
int eba_remote_alloc(uint16_t node_id,uint64_t size, uint64_t life_time,uint8_t type);

/**
 * @brief This function writes data to a local allocated buffer.
 * @param data pointer to the data.
 * @param buf_id target local buffer id.
 * @param off offset.
 * @param size size of the data.
 * @returns 0 on success and 1 if it fails.
 */
int eba_remote_write(uint16_t node_id, const void* data, uint64_t buff_id, uint64_t off, uint64_t size);

/**
 * @brief This function reads data from a distant allocated buffer.
 * @param data pointer to the data out.
 * @param buf_id target local buffer id.
 * @param off offset.
 * @param size size of the data.
 * @returns 0 on success and 1 if it fails.
 */
int eba_remote_read(uint64_t dst_buf_id,uint64_t dst_off, uint64_t src_buf_id, uint64_t src_off, uint64_t size);