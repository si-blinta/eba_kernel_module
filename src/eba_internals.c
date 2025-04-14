#include "eba_internals.h"
#include "eba.h"
/* Global list of currently allocated buffers */
LIST_HEAD(eba_buffer_list);
/* Global lock to protect the eba_buffer_list */
spinlock_t eba_buffer_list_lock;

/* The genpool instance used for memory allocation */
struct gen_pool *eba_pool = NULL;
/* The raw memory backing the pool */
void *eba_pool_mem = NULL;
/**
 * @brief Initializes the memory pool for the EBA module.
 *
 * This function creates a generic memory pool with an 8-byte alignment,
 * allocates a contiguous block of memory (using kmalloc) to back that pool,
 * registers the allocated memory block with the pool, and initializes the
 * spinlock protecting the global tracking list.
 *
 * @return 0 if the memory pool is successfully initialized, 1 otherwise.
 */
int eba_internals_mempool_init(void)
{
    unsigned long align = 8;    /* Set allocation alignment to 8 bytes */
    unsigned long phys_addr;    /* Dummy physical address for vmalloc'd memory */

    /* Create the generic memory pool.
     * The second parameter (-1) specifies flags; here, no special flags are needed.
     */
    eba_pool = gen_pool_create(align, -1);
    if (!eba_pool) {
         EBA_ERR("EBA: Failed to create memory pool\n");
         return 1;
    }
    
    /* Allocate a contiguous block of virtual memory using vmalloc.
     * This method supports very large allocations (subject to system limits) but
     * the returned memory is not physically contiguous.
     */
    eba_pool_mem = vmalloc(MEMORY_POOL_SIZE);
    if (!eba_pool_mem) {
         EBA_ERR("EBA: Failed to allocate memory for the pool\n");
         gen_pool_destroy(eba_pool);
         return 1;
    }
    
    /* Since vmalloc'd memory is not physically contiguous,
     * we cannot reliably compute a physical base address.
     * We pass a dummy value (e.g., 0) to gen_pool_add_virt().
     */
    phys_addr = 0;
    gen_pool_add_virt(eba_pool, (unsigned long)eba_pool_mem, phys_addr, MEMORY_POOL_SIZE, -1);
    
    /* Initialize the spinlock to protect the global tracking list */
    spin_lock_init(&eba_buffer_list_lock);
    
    EBA_INFO("EBA: Memory pool initialized successfully\n");
    return 0;
}
/**
 * @brief Allocates a block of memory from the EBA memory pool.
 *
 * This function allocates a block of memory from the generic memory pool,
 * creates a tracking structure to record the allocation information (including
 * the allocated pointer, size, and lifetime), and adds the tracking structure
 * to a global list.
 *
 * @param size The number of bytes to allocate.
 * @param life_time The lifetime of the allocation in seconds.
 *
 * @return A pointer to the allocated memory on success, or NULL if the allocation fails.
 */
void *eba_internals_malloc(uint64_t size, uint64_t life_time)
{
    unsigned long addr;
    void *ptr;
    struct eba_buffer *buf;

    /* Check if the memory pool is initialized */
    if (!eba_pool)
         return NULL;

    /* Allocate a block of memory from the gen_pool */
    addr = gen_pool_alloc(eba_pool, size);
    if (!addr) {
         EBA_ERR("EBA: Allocation failed for size %llu\n", size);
         return NULL;
    }
    ptr = (void *)addr;
     /* Zero out the allocated memory */
     memset(ptr, 0, size);
    /* Allocate and initialize the tracking structure */
    buf = kmalloc(sizeof(*buf), GFP_KERNEL);
    if (!buf) {
         EBA_ERR("EBA: Tracking structure allocation failed\n");
         gen_pool_free(eba_pool, addr, size);
         return NULL;
    }

    spin_lock_init(&buf->lock);
    buf->type = 0;    /* Set default type (customize if needed) */
    
    /* Store the allocated pointer */
    buf->address = (unsigned long)ptr;
    buf->size = size;
    
    /* Set the expiration time using the provided lifetime (in seconds) */
     if (life_time != 0)
          buf->expires = ktime_get_real_seconds() + life_time;
     else
          buf->expires = 0;
    /* Initialize the list head for tracking */
    INIT_LIST_HEAD(&buf->node);

    /* Add the tracking structure to the global list in a thread-safe manner */
    spin_lock(&eba_buffer_list_lock);
    list_add(&buf->node, &eba_buffer_list);
    spin_unlock(&eba_buffer_list_lock);

    EBA_DBG("EBA: Allocated memory at %llu, size: %llu bytes, lifetime: %llu sec\n",
            (uint64_t)ptr, size, life_time);
    
    return ptr;
}

/**
 * @brief Frees a previously allocated memory block from the EBA memory pool.
 *
 * This function looks up the tracking structure associated with the provided pointer,
 * removes it from the global tracking list, frees the memory block from the gen_pool,
 * and then frees the tracking structure.
 *
 * @param ptr The pointer to the memory block to free.
 *
 * @return 0 if the memory was successfully freed, or -1 if the pointer is invalid or not found.
 */
int eba_internals_free(void *ptr)
{
    struct eba_buffer *buf;
    int found = 0;

    /* Check if the memory pool is initialized and if the pointer is valid */
    if (!eba_pool || !ptr)
         return -1;

    /* Lock the global list before searching for the tracking structure */
    spin_lock(&eba_buffer_list_lock);
    list_for_each_entry(buf, &eba_buffer_list, node) {
         if (buf->address == (unsigned long)ptr) {
              list_del(&buf->node);
              found = 1;
              break;
         }
    }
    spin_unlock(&eba_buffer_list_lock);

    /* If no tracking structure is found, log an error and return -1 */
    if (!found) {
         EBA_ERR("EBA: Attempt to free unknown memory at %llu\n", (uint64_t)ptr);
         return -1;
    }

    /* Free the memory from the gen_pool using the stored size */
    gen_pool_free(eba_pool, (unsigned long)ptr, buf->size);

    /* Free the tracking structure */
    kfree(buf);

    EBA_DBG("EBA: Freed memory at %llu, size: %llu bytes\n", (uint64_t)ptr, buf->size);
    return 0;
}

/**
 * @brief Frees the entire memory pool for the EBA module.
 *
 * This function cleans up by removing and freeing all tracking structures in the global list,
 * destroying the generic memory pool, and releasing the backing memory that was allocated via vmalloc.
 */
void eba_internals_mempool_free(void)
{
    struct eba_buffer *buf, *tmp;
    int outstanding = 0;

    // Count outstanding allocations
    spin_lock(&eba_buffer_list_lock);
    list_for_each_entry(buf, &eba_buffer_list, node) {
         outstanding++;
    }
    EBA_DBG("EBA: %d outstanding buffers before cleanup\n", outstanding);
    if (outstanding > 0) {
         list_for_each_entry_safe(buf, tmp, &eba_buffer_list, node) {
              EBA_DBG("EBA: Forcing free of buffer at %llu, size: %llu bytes\n",
                      buf->address, buf->size);
              list_del(&buf->node);
              gen_pool_free(eba_pool, buf->address, buf->size);
              kfree(buf);
         }
    }

    // Now destroy the gen_pool and free the backing memory
    if (eba_pool) {
         gen_pool_destroy(eba_pool);
         eba_pool = NULL;
    }
    if (eba_pool_mem) {
         vfree(eba_pool_mem);
         eba_pool_mem = NULL;
    }
    spin_unlock(&eba_buffer_list_lock);
    EBA_INFO("EBA: Memory pool freed successfully\n");
}

/**
 * @brief Performs a stress test on the EBA memory pool.
 *
 * This function repeatedly allocates and frees memory blocks using
 * eba_internals_malloc() and eba_internals_free() to verify that the memory pool
 * is robust and that allocations/deallocations work correctly.
 *
 * @return 0 on success, or a negative error code on failure.
 */
int eba_internals_mem_stress_test(void)
{
    const int num_allocs = 100;  /* Number of allocations for the stress test */
    void **allocations;          /* Array to store pointers to allocated memory */
    int *sizes;                  /* Array to store the sizes for each allocation */
    int i, ret;

    /* Dynamically allocate arrays for tracking allocations and their sizes */
    allocations = kmalloc_array(num_allocs, sizeof(void *), GFP_KERNEL);
    if (!allocations)
         return -ENOMEM;

    sizes = kmalloc_array(num_allocs, sizeof(int), GFP_KERNEL);
    if (!sizes) {
         kfree(allocations);
         return -ENOMEM;
    }

    /* Initialize sizes and set each allocation pointer to NULL */
    for (i = 0; i < num_allocs; i++) {
         sizes[i] = 16 + ((i * 31) % 1024);  /* Example size: between 16 and ~1040 bytes */
         allocations[i] = NULL;
    }

    /* Allocation Phase: Allocate memory chunks and verify each allocation */
    for (i = 0; i < num_allocs; i++) {
         allocations[i] = eba_internals_malloc(sizes[i], 10);  /* Lifetime set to 10 sec */
         if (!allocations[i]) {
              EBA_ERR("EBA-STRESS: Allocation %d of size %d failed\n", i, sizes[i]);
              ret = -ENOMEM;
              goto cleanup;
         }
    }
    EBA_INFO("EBA-STRESS: Allocation phase completed successfully\n");

    /* De-allocation Phase: Free all allocated memory chunks */
    for (i = 0; i < num_allocs; i++) {
         ret = eba_internals_free(allocations[i]);
         if (ret != 0) {
              EBA_ERR("EBA-STRESS: Freeing allocation %d failed\n", i);
              goto cleanup;
         }
    }
    EBA_INFO("EBA-STRESS: De-allocation phase completed successfully\n");

    ret = 0;  /* If all allocations and frees succeed, return success */

cleanup:
    /* Free the tracking arrays */
    kfree(allocations);
    kfree(sizes);
    return ret;
}

int eba_internals_write(const void *data, uint64_t buff_id, uint64_t off, uint64_t size)
{
    struct eba_buffer *buf;
    int found = 0;

    if (!data)
    {
          EBA_ERR("eba_internals_write: data pointer is NULL\n");
         return -EINVAL;
    }

    /* Locate the tracking structure using buff_id (the allocated virtual address) */
    spin_lock(&eba_buffer_list_lock);
    list_for_each_entry(buf, &eba_buffer_list, node) {
         if (buf->address == buff_id) {
              found = 1;
              break;
         }
    }
    spin_unlock(&eba_buffer_list_lock);

    if (!found)
    {
          EBA_ERR("eba_internals_write: Buffer with id 0x%llx not found\n", buff_id);
         return -EINVAL;  /* Buffer not found */
    }
     spin_lock(&buf->lock);
    /* Check if the write operation is within bounds */
    if (off + size > buf->size)
    {
          EBA_ERR("eba_internals_write: Write out of bounds: off(%llu) + size(%llu) > buf->size(%llu)\n", off, size, buf->size);     
          spin_unlock(&buf->lock);
         return -EINVAL;
    }

    /* Copy the data into the allocated buffer at the specified offset */
    memcpy((void *)(buf->address + off), data, size);
    spin_unlock(&buf->lock);
    EBA_INFO("EBA: Written %llu bytes at offset %llu on buf %llu\n",size,off,buff_id);
    return 0;
}

/*
 * eba_internals_read - Reads data from an allocated buffer.
 */
int eba_internals_read(void *data_out, uint64_t buff_id, uint64_t off, uint64_t size)
{
    struct eba_buffer *buf;
    int found = 0;

    if (!data_out)
    {
     EBA_ERR("eba_internals_read: data pointer is NULL\n");
     return -EINVAL;
    }

    /* Locate the tracking structure using buff_id (the allocated virtual address) */
    spin_lock(&eba_buffer_list_lock);
    list_for_each_entry(buf, &eba_buffer_list, node) {
         if (buf->address == buff_id) {
              found = 1;
              break;
         }
    }
    spin_unlock(&eba_buffer_list_lock);

    if (!found)
    {
          EBA_ERR("eba_internals_read: Buffer with id 0x%llx not found\n", buff_id);
          return -EINVAL;  /* Buffer not found */
    }

    spin_lock(&buf->lock);
         /* Check if the read operation is within bounds */
    if (off + size > buf->size)
    {
          EBA_ERR("eba_internals_read: Read out of bounds: off(%llu) + size(%llu) > buf->size(%llu)\n", off, size, buf->size); 
          spin_unlock(&buf->lock);
          return -EINVAL;
    }

    /* Copy data from the allocated buffer at the specified offset into data_out */
    memcpy(data_out, (void *)(buf->address + off), size);
    spin_unlock(&buf->lock);
    EBA_INFO("EBA: Read %llu bytes at offset %llu on buf %llu\n",size,off,buff_id);
    return 0;
}

/*
 * eba_internals_rw_stress_test - Performs a stress test for read/write operations.
 *
 * For each allocation in the test, this function:
 *   1. Allocates a buffer using eba_internals_malloc() with a given size and lifetime.
 *   2. Creates a test pattern.
 *   3. Uses eba_internals_write() to write the pattern into the allocated buffer.
 *   4. Reads back the data using eba_internals_read() into a temporary buffer.
 *   5. Compares the read data with the original pattern to verify correctness.
 *   6. Frees the allocated buffer with eba_internals_free().
 *
 * The test is performed for a predetermined number of iterations.
 */
int eba_internals_rw_stress_test(void)
{
    const int num_allocs = 100;  /* Number of buffers to allocate for the test */
    void **allocations;          /* Array to store allocated buffer pointers */
    int *sizes;                  /* Array to store the sizes for each buffer */
    int i, ret = 0;
    unsigned char *pattern = NULL;
    unsigned char *read_back = NULL;

    /* Allocate arrays to track allocated buffers and their sizes */
    allocations = kmalloc_array(num_allocs, sizeof(void *), GFP_KERNEL);
    if (!allocations)
         return -ENOMEM;

    sizes = kmalloc_array(num_allocs, sizeof(int), GFP_KERNEL);
    if (!sizes) {
         kfree(allocations);
         return -ENOMEM;
    }

    /* Initialize the sizes and set each allocation pointer to NULL */
    for (i = 0; i < num_allocs; i++) {
         sizes[i] = 16 + ((i * 31) % 1024);  /* Buffer size will vary between 16 and about 1040 bytes */
         allocations[i] = NULL;
    }

    /* Allocation and test phase */
    for (i = 0; i < num_allocs; i++) {
         /* Allocate a buffer with a lifetime of 10 seconds */
         allocations[i] = eba_internals_malloc(sizes[i], 10);
         if (!allocations[i]) {
              EBA_ERR("EBA-RW-STRESS: Allocation %d of size %d failed\n", i, sizes[i]);
              ret = -ENOMEM;
              goto cleanup;
         }

         /* Allocate temporary buffers for writing and reading */
         pattern = kmalloc(sizes[i], GFP_KERNEL);
         if (!pattern) {
              ret = -ENOMEM;
              goto cleanup;
         }
         read_back = kmalloc(sizes[i], GFP_KERNEL);
         if (!read_back) {
              kfree(pattern);
              ret = -ENOMEM;
              goto cleanup;
         }

         /*
          * Fill the pattern buffer with a test pattern.
          * For example, store a sequence of incrementing values modulo 256.
          */
         {
             int j;
             for (j = 0; j < sizes[i]; j++)
                 pattern[j] = (unsigned char)(j & 0xFF);
         }

         /* Write the test pattern into the allocated buffer */
         ret = eba_internals_write(pattern, (uint64_t)allocations[i], 0, sizes[i]);
         if (ret != 0) {
              EBA_ERR("EBA-RW-STRESS: Write failed on buffer %d\n", i);
              kfree(pattern);
              kfree(read_back);
              goto cleanup;
         }

         /* Read back the data into the read_back buffer */
         ret = eba_internals_read(read_back, (uint64_t)allocations[i], 0, sizes[i]);
         if (ret != 0) {
              EBA_ERR("EBA-RW-STRESS: Read failed on buffer %d\n", i);
              kfree(pattern);
              kfree(read_back);
              goto cleanup;
         }

         /* Compare the written pattern with the read-back data */
         if (memcmp(pattern, read_back, sizes[i]) != 0) {
              EBA_ERR("EBA-RW-STRESS: Data mismatch on buffer %d\n", i);
              ret = -EINVAL;
              kfree(pattern);
              kfree(read_back);
              goto cleanup;
         }

         /* Free the temporary buffers */
         kfree(pattern);
         kfree(read_back);

         EBA_INFO("EBA-RW-STRESS: Buffer %d (size %d) verified successfully\n", i, sizes[i]);
    }

    EBA_INFO("EBA-RW-STRESS: All read/write operations completed successfully\n");
    ret = 0;

cleanup:
    /* Free all allocated buffers */
    for (i = 0; i < num_allocs; i++) {
         if (allocations[i])
              eba_internals_free(allocations[i]);
    }
    kfree(allocations);
    kfree(sizes);
    return ret;
}

void eba_check_expired_buffers(void)
{
    struct eba_buffer *buf, *tmp;
    ktime_t now = ktime_get_real_seconds();
    // Acquire the spinlock before iterating the list.
    spin_lock(&eba_buffer_list_lock);
    list_for_each_entry_safe(buf, tmp, &eba_buffer_list, node) {
         // If expires is 0, the lifetime is infinite, so skip.
         if (buf->expires != 0 && now >= buf->expires) {
               EBA_DBG("EBA: Buffer at %llu expired (current time %lld >= expires %lld)\n",buf->address, now,buf->expires);    
               // Remove the entry from the tracking list
              list_del(&buf->node);
              // Free the allocated memory from the gen_pool
              gen_pool_free(eba_pool, buf->address, buf->size);
              // Free the tracking structure itself
              kfree(buf);
         }
    }
    spin_unlock(&eba_buffer_list_lock);
}