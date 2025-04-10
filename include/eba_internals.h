struct eba_buffer {
    //spinlock todo read how it is implemented in linux
    uin8_t type;
    uint64_t physical_address;
    uint64_t size;       // requested size in bytes
    ktime_t expires;         // time when it expires
    struct list_head node;   // linked-list node
  };

/**
 * @brief this function looks up a
 */
struct eba_buffer* eba_buffer_internals_lookup_buf( uint64_t physical_addr );




/**
 * @brief This function initiate the memory pool for eba.
 * @returns 0 on success and 1 on failure.
 */
int eba_internals_mempool_init();

/**
 * @brief This function allocates memory from the eba memory pool. 
 * @param size Size of the allocation.
 * @returns a pointer to the allocated chunk. 
 */
void* eba_internals_malloc(uint64_t size);

/**
 * @brief This function frees a previously allocated chunk of memory.
 * @param ptr pointer to free.
 * @returns 0 on succes and -1 on fail.
 */
int eba_internals_free(const void* ptr); 