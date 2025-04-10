#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/genalloc.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/ktime.h>
#include <linux/io.h>       /* for virt_to_phys() */
#include <linux/types.h>
#define MEMORY_POOL_SIZE 1024 * 1024
/* Tracking structure for each allocated memory chunk */
struct eba_buffer {
  /* A spinlock if buffer-specific locking is ever required.
   * (For now, we only initialize it.)
   */
  spinlock_t lock;
  uint8_t type;              /* Buffer type (customize as needed) */
  uint64_t physical_address; /* Physical address of allocated block */
  uint64_t size;             /* Requested allocation size in bytes */
  ktime_t expires;           /* Time when this allocation expires */
  struct list_head node;     /* Linked list node to chain all allocations */
};

/**
 * @brief eba_buffer_internals_lookup_buf - Look up a tracking buffer by physical address.
 * Searches the global allocation list for a buffer whose physical address matches
 * the provided value.
 * @param physical_addr: The physical address to search for.
 * @returns: Pointer to the corresponding eba_buffer if found; NULL otherwise.
 */
struct eba_buffer* eba_buffer_internals_lookup_buf(uint64_t physical_addr);

/**
 * @brief eba_internals_mempool_init - Initialize the memory pool.
 *
 * Allocates a fixed-size memory block (here = MEMORY_POOL_SIZE) and adds it to the genpool.
 * Initializes the global tracking list and associated locks.
 *
 * @returns: 0 on success, 1 on failure.
 */
int eba_internals_mempool_init(void);

/**
 * eba_internals_malloc - Allocate a memory chunk from the pool.
 * @size: The requested allocation size (in bytes).
 *
 * Allocates memory from the genpool. Also allocates and initializes a tracking
 * structure, adds it to the global list, and returns a pointer to the
 * allocated memory.
 *
 * Return: Pointer to allocated memory on success; NULL on failure.
 */
void *eba_internals_malloc(uint64_t size);

/**
 * eba_internals_free - Free an allocated memory chunk.
 * @ptr: Pointer to the memory block to free.
 *
 * This function looks up the tracking structure for the allocation (by comparing
 * physical addresses), frees the memory using genpool_free, and then removes and
 * destroys the tracking structure.
 *
 * Return: 0 on success; -1 if the pointer was not found or an error occurred.
 */
int eba_internals_free(void *ptr);


/**
 * eba_internals_mempool_cleanup - Clean up the memory pool and tracking structures.
 *
 * Walks through the global list of tracking structures, frees them, and then
 * deallocates the genpool along with the backing memory.
 */
void eba_internals_mempool_cleanup(void);


/**
 * @brief eba_internals_memory_stress - Stress test the EBA memory allocation system.
 *
 * This function performs a series of memory allocations followed by lookups
 * and deallocations. It allocates a number of memory chunks (with varying sizes),
 * verifies that each allocated chunk appears in the tracking list via lookup,
 * and then frees each one. This test helps detect any issues with allocation,
 * tracking, or deallocation in the memory pool.
 *
 * @returns: 0 on success; -1 on failure.
 */
int eba_internals_memory_stress(void);