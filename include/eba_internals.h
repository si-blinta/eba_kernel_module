/**
 * @file eba_internals.h
 * @brief EBA Internal Memory Management Header.
 *
 * This header provides the internal mechanisms for managing memory within the EBA module.
 * It includes functions to initialize and manage a generic memory pool, allocate and free
 * buffers from that pool, perform stress tests on memory operations, and handle timed
 * expiration of allocated buffers. This file is intended for internal use and is critical
 * for the dynamic memory handling of the EBA framework.
 */
#ifndef EBA_INTERNALS_H
#define EBA_INTERNALS_H
#include <linux/slab.h>          
#include <linux/genalloc.h>      
#include <linux/printk.h>        
#include <linux/spinlock.h>     
#include <linux/list.h>         
#include <linux/ktime.h>         
#include <asm/io.h>             
#include <linux/vmalloc.h>    
#include <linux/string.h>      
#include <linux/errno.h>       
/** Size of the memory pool in bytes. */
#define MEMORY_POOL_SIZE (1024 * 1024)*1000
/*
 * EBA_MAX_SERVICES - upper bound for service/port IDs registered via
 * eba_register_service().  Dynamic buffer IDs begin at EBA_MAX_SERVICES so
 * they never collide with service IDs.
 *
 * The value 65536 covers the full TCP/UDP port range (1-65535) while keeping
 * dynamic buffer IDs starting at 65536.
 */
#define EBA_MAX_SERVICES 65536
/**
 * struct eba_buffer - Tracking structure for each allocated memory chunk.
 * @lock:    Spinlock to protect this buffer's data.
 * @type:    Buffer type.
 * @address: Address of the allocated memory block.
 * @size:    Requested allocation size in bytes.
 * @expires: Time when the allocation expires.
 * @node:    List node to chain all allocations.
 * @id:      Unique identifier for the buffer.
 *
 * This structure tracks each memory allocation made from the EBA memory pool.
 */
struct eba_buffer {
  spinlock_t lock;
  uint8_t type;             
  uint64_t address;         
  uint64_t size;             
  ktime_t expires;           
  struct list_head node; 
  uint64_t id;    
  //Add permissions 
};

/**
 * eba_internals_mempool_init - Initialize the memory pool for the EBA module.
 *
 * This function creates a generic memory pool with 8-byte alignment, allocates a
 * contiguous block of DMA-coherent memory to back that pool, registers the allocated
 * memory block with the pool, and initializes the spinlock protecting the global
 * tracking list.
 *
 * Return: 0 if the memory pool is successfully initialized, 1 otherwise.
 */
int eba_internals_mempool_init(void);

/**
 * eba_internals_malloc - Allocate a block of memory from the EBA memory pool.
 * @size:      Number of bytes to allocate.
 * @life_time: Lifetime of the allocation in seconds; 0 indicates infinite.
 *
 * This function allocates a memory block from the generic memory pool, creates a
 * tracking structure to record allocation details (pointer, size, lifetime), and
 * adds this structure to a global list for tracking.
 *
 * Return: Allocated buffer ID or 0 on fail.
 */
uint64_t eba_internals_malloc(uint64_t size, uint64_t life_time);

/**
 * eba_internals_free - Free a previously allocated block of memory from the EBA memory pool.
 * @ptr: Pointer to the memory block to free.
 *
 * This function locates the allocation corresponding to the given pointer in the
 * global tracking list, removes it, frees the memory block from the generic memory pool,
 * and then frees the associated tracking structure.
 *
 * Return: 0 if the memory was successfully freed, or -1 if the pointer is invalid or not found.
 */
int eba_internals_free(uint64_t id);

/**
 * eba_internals_mempool_free - Free the entire memory pool for the EBA module.
 *
 * This function cleans up all memory allocations by freeing all tracking structures
 * in the global list, destroying the generic memory pool, and releasing the backing memory
 * allocated using the DMA API.
 */
void eba_internals_mempool_free(void);

/**
 * eba_internals_mem_stress_test - Perform a stress test on the EBA memory pool.
 *
 * This function repeatedly allocates and frees memory blocks using eba_internals_malloc()
 * and eba_internals_free(). It verifies that both allocation and de-allocation phases complete
 * successfully.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int eba_internals_mem_stress_test(void);

/**
 * eba_internals_write - Write data into an allocated buffer.
 * @data:    Pointer to the data to write.
 * @buff_id: Identifier of the buffer (returned by eba_internals_malloc).
 * @off:     Offset (in bytes) within the buffer at which writing should begin.
 * @size:    Number of bytes to write.
 *
 * This function locates the allocated buffer corresponding to @buff_id, verifies that the
 * write operation (starting at offset @off for @size bytes) is within bounds, and copies the
 * data into the buffer.
 *
 * Return: 0 on success, or a negative error code (e.g., -EINVAL) if the operation fails.
 */
int eba_internals_write(const void *data, uint64_t buff_id, uint64_t off, uint64_t size);

/**
 * eba_internals_read - Read data from an allocated buffer.
 * @data_out: Pointer to the destination where read data should be stored.
 * @buff_id:  Identifier of the buffer (returned by eba_internals_malloc).
 * @off:      Offset (in bytes) within the buffer from which reading should start.
 * @size:     Number of bytes to read.
 *
 * This function locates the allocated buffer corresponding to @buff_id, verifies that the
 * read operation (starting at offset @off for @size bytes) is within bounds, and copies data
 * from the buffer into the provided output pointer.
 *
 * Return: 0 on success, or a negative error code (e.g., -EINVAL) if the operation fails.
 */
int eba_internals_read(void *data_out, uint64_t buff_id, uint64_t off, uint64_t size);

/**
 * eba_internals_rw_stress_test - Stress test for read/write operations on the EBA memory pool.
 *
 * This function allocates multiple buffers using eba_internals_malloc(), writes a known
 * data pattern into each buffer with eba_internals_write(), reads the data back using
 * eba_internals_read(), verifies that the read data matches the written data, and then deallocates
 * the buffers using eba_internals_free().
 *
 * Return: 0 on success, or a negative error code if any operation fails.
 */
int eba_internals_rw_stress_test(void);

/**
 * eba_check_expired_buffers - Free buffers that have passed their expiration time.
 *
 * This function iterates over the global tracking list of allocated buffers. If a buffer’s
 * expiration time is nonzero (i.e., its lifetime is not infinite) and the current time is equal
 * to or greater than the expiration time, the buffer is removed from the list, freed from the
 * generic memory pool, and its tracking structure is deallocated.
 *
 * @note: This function should be called periodically from a timer or workqueue to reclaim expired buffers.
 */
void eba_check_expired_buffers(void);

/**
 * register_service - Register a buffer as a service.
 * @buff_id: Identifier of the buffer to register.
 * @new_id:  New identifier to assign to the buffer.
 * Return: 0 on success, or a negative error code if the operation fails.
 */
int register_service(uint64_t buff_id, uint64_t new_id);

/**
 * register_queue - Register a buffer as a queue.
 * @buff_id: Identifier of the buffer to register.
 * Return: 0 on success, or a negative error code if the operation fails.
 */
struct queue
{
     uint64_t head;
     uint64_t tail;
};

/**
 * register_queue - Register a buffer as a queue.
 * @buff_id: Identifier of the buffer to register.
 * Return: 0 on success, or a negative error code if the operation fails.
 */
int register_queue(uint64_t buff_id);

/**
 * eba_internals_enqueue - Enqueue data into an allocated buffer.
 * @buff_id: Identifier of the buffer (returned by eba_internals_malloc).
 * @data:    Pointer to the data to enqueue.
 * @size:    Number of bytes to enqueue.
 *
 * This function locates the allocated buffer corresponding to @buff_id, verifies that the
 * enqueue operation is valid, and copies data into the buffer.
 *
 * Return: 0 on success, or a negative error code (e.g., -EINVAL) if the operation fails.
 */
int eba_internals_enqueue(uint64_t buff_id, void *data, uint64_t size);

/**
 * eba_internals_dequeue - Dequeue data from an allocated buffer.
 * @buff_id: Identifier of the buffer (returned by eba_internals_malloc).
 * @data_out: Pointer to the destination where dequeued data should be stored.
 * @size: Number of bytes to dequeue.
 *
 * This function locates the allocated buffer corresponding to @buff_id, verifies that the
 * dequeue operation is valid, and copies data from the buffer into the provided output pointer.
 *
 * Return: 0 on success, or a negative error code (e.g., -EINVAL) if the operation fails.
 */
int eba_internals_dequeue(uint64_t buff_id, void *data_out, uint64_t size);


/**
 * eba_internals_queue_stress_test - Stress test for queue operations.
 *
 * This function allocates a buffer, registers it as a queue, enqueues a series of
 * integer items, dequeues them, and verifies that the data matches the expected values.
 *
 * Return: 0 on success, or a negative error code if any operation fails.
 */
int eba_internals_queue_stress_test(void);

/**
 * get_buffer_by_id - Retrieve a buffer by its ID.
 * @id: Identifier of the buffer to retrieve.
 */
struct eba_buffer *get_buffer_by_id(uint64_t id);

#endif