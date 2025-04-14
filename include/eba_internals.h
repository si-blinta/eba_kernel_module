#ifndef EBA_INTERNALS_H
#define EBA_INTERNALS_H
#include <linux/slab.h>          /* For kmalloc() and kfree() */
#include <linux/genalloc.h>      /* For gen_pool_alloc(), gen_pool_free(), gen_pool_create(), gen_pool_destroy() */
#include <linux/printk.h>        /* For pr_err(), pr_info() */
#include <linux/spinlock.h>      /* For spin_lock_init(), spin_lock(), spin_unlock() */
#include <linux/list.h>          /* For list operations */
#include <linux/ktime.h>         /* For ktime_set() */
#include <asm/io.h>              /* For virt_to_phys() */
#include <linux/vmalloc.h>       /* For vmalloc()/vfree() */
#include <linux/string.h>      /* For memcpy() and memcmp() */
#include <linux/errno.h>       /* For standard error numbers (e.g., -EINVAL) */
#define MEMORY_POOL_SIZE (1024 * 1024)*1000
/* Tracking structure for each allocated memory chunk */
struct eba_buffer {
  spinlock_t lock;
  uint8_t type;              /* Buffer type (customize as needed) */
  uint64_t address;          /* Address of allocated block */
  uint64_t size;             /* Requested allocation size in bytes */
  ktime_t expires;           /* Time when this allocation expires */
  struct list_head node;     /* Linked list node to chain all allocations */
};
/**
 * @brief Initializes the memory pool for the EBA module.
 *
 * This function creates a generic memory pool with an 8-byte alignment,
 * allocates a contiguous block of DMA-coherent memory to back that pool,
 * registers the allocated memory block with the pool, and initializes the
 * spinlock protecting the global tracking list.
 *
 * @return 0 if the memory pool is successfully initialized, 1 otherwise.
 */
int eba_internals_mempool_init(void);

/**
 * @brief Allocates a block of memory from the EBA memory pool.
 *
 * This function allocates a block of memory from the generic memory pool, 
 * creates a tracking structure to store details about the allocation (including the allocated pointer, size, and lifetime),
 * and adds the tracking structure to a global list.
 *
 * @param size The number of bytes to allocate.
 * @param life_time The lifetime of the allocation in seconds, used for tracking purposes. 0 = infinite
 *
 * @return A pointer to the allocated memory on success, or NULL if the allocation fails.
 */
void *eba_internals_malloc(uint64_t size, uint64_t life_time);

/**
 * @brief Frees a previously allocated block of memory from the EBA memory pool.
 *
 * This function searches the global tracking list for the allocation corresponding to the
 * given pointer, removes it from the list, frees the memory block in the generic memory pool,
 * and then frees the associated tracking structure.
 *
 * @param ptr The pointer to the memory block to free.
 *
 * @return 0 if the memory was successfully freed, or -1 if the pointer is invalid or not found.
 */
int eba_internals_free(void *ptr);

/**
 * @brief Frees the entire memory pool for the EBA module.
 *
 * This function cleans up all allocations by freeing all tracking structures in the global list,
 * destroying the generic memory pool, and releasing the backing memory that was allocated using
 * the DMA API.
 */
void eba_internals_mempool_free(void);

/**
 * @brief Performs a stress test on the EBA memory pool.
 *
 * This function repeatedly allocates and frees memory blocks using
 * eba_internals_malloc() and eba_internals_free(). It verifies that the
 * allocation phase and the de-allocation phase complete successfully.
 *
 * @return 0 on success, or a negative error code on failure.
 */
int eba_internals_mem_stress_test(void);

/**
 * @brief Writes data into an allocated buffer.
 *
 * This function searches for the allocated buffer whose identifier matches the
 * given buff_id (which is the virtual address returned by eba_internals_malloc),
 * verifies that the write (starting at offset off for size bytes) is within bounds,
 * and copies the provided data into that buffer.
 *
 * @param data Pointer to the data to write.
 * @param buff_id The identifier of the buffer (its allocated virtual address).
 * @param off The offset (in bytes) within the buffer where writing should begin.
 * @param size The number of bytes to write.
 *
 * @return 0 on success, or a negative error code (e.g. -EINVAL) if the operation fails.
 */
int eba_internals_write(const void *data, uint64_t buff_id, uint64_t off, uint64_t size);

/**
 * @brief Reads data from an allocated buffer.
 *
 * This function searches for the allocated buffer whose identifier matches the
 * given buff_id (which is the virtual address returned by eba_internals_malloc),
 * verifies that the read (starting at offset off for size bytes) is within bounds,
 * and copies data from that buffer into the provided output pointer.
 *
 * @param data_out Pointer to the destination where the read data should be stored.
 * @param buff_id The identifier of the buffer (its allocated virtual address).
 * @param off The offset (in bytes) within the buffer from where reading should start.
 * @param size The number of bytes to read.
 *
 * @return 0 on success, or a negative error code (e.g. -EINVAL) if the operation fails.
 */
int eba_internals_read(void *data_out, uint64_t buff_id, uint64_t off, uint64_t size);

/**
 * @brief Performs a stress test for read/write operations on the EBA memory pool.
 *
 * This function allocates multiple buffers using eba_internals_malloc(), writes
 * a known data pattern into each buffer with eba_internals_write(), reads the data
 * back using eba_internals_read(), verifies that the read data matches the written
 * data, and then deallocates the buffers using eba_internals_free().
 *
 * @return 0 on success, or a negative error code if any operation fails.
 */
int eba_internals_rw_stress_test(void);

/**
 * eba_check_expired_buffers - Frees buffers that have passed their expiration time.
 *
 * @brief This function iterates over the global tracking list of allocated buffers.
 * If a buffer’s expiration time is nonzero (meaning its lifetime is not infinite)
 * and the current real time is equal to or greater than its expiration time, then
 * the function removes the buffer from the tracking list, frees the memory from the
 * gen_pool, and frees its tracking structure.
 *
 * @note: This function should be called periodically from a timer or workqueue,
 *       so that expired buffers are reclaimed.
 */
void eba_check_expired_buffers(void);
#endif