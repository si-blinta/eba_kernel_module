#include "eba_internals.h"
/* Global list of currently allocated buffers */
static LIST_HEAD(eba_buffer_list);
/* Global lock to protect the eba_buffer_list */
static spinlock_t eba_buffer_list_lock;

/* The genpool instance used for memory allocation */
static struct gen_pool *eba_pool = NULL;
/* The raw memory backing the pool */
static void *eba_pool_mem = NULL;

int eba_internals_mempool_init(void)
{
    unsigned long align = 8;          /* 8-byte alignment for allocations */
    /* Create the generic memory pool */
    eba_pool = gen_pool_create(align, -1);
    if (!eba_pool) {
         pr_err("EBA: Failed to create memory pool\n");
         return 1;
    }
    
    /* Allocate a contiguous block to back our genpool.*/
    eba_pool_mem = kmalloc(MEMORY_POOL_SIZE, GFP_KERNEL);
    if (!eba_pool_mem) {
         pr_err("EBA: Failed to allocate memory for the pool\n");
         gen_pool_destroy(eba_pool);
         return 1;
    }

    /* Add the allocated block to the genpool.
     * This call registers the virtual address along with its physical address.
     */
    gen_pool_add_virt(eba_pool, (unsigned long)eba_pool_mem,
                      virt_to_phys(eba_pool_mem), MEMORY_POOL_SIZE, -1);

    /* Initialize the spinlock used to protect the global buffer list */
    spin_lock_init(&eba_buffer_list_lock);

    pr_info("EBA: Memory pool initialized successfully\n");
    return 0;
}

void *eba_internals_malloc(uint64_t size)
{
    unsigned long addr;
    void *ptr;
    struct eba_buffer *buf;

    if (!eba_pool)
         return NULL;

    addr = gen_pool_alloc(eba_pool, size);
    if (!addr) {
         pr_err("EBA: Allocation failed for size %llu\n", size);
         return NULL;
    }
    ptr = (void *)addr;

    /* Allocate and initialize the tracking structure */
    buf = kmalloc(sizeof(*buf), GFP_KERNEL);
    if (!buf) {
         pr_err("EBA: Tracking structure allocation failed\n");
         gen_pool_free(eba_pool, addr, size);
         return NULL;
    }
    spin_lock_init(&buf->lock);
    buf->type = 0;    /* Set default type (customize if needed) */
    buf->physical_address = virt_to_phys(ptr);
    buf->size = size;
    /* For demonstration, we set expires to zero time. Adjust as needed. */
    buf->expires = ktime_set(0, 0);
    INIT_LIST_HEAD(&buf->node);

    /* Add tracking structure to the global list */
    spin_lock(&eba_buffer_list_lock);
    list_add(&buf->node, &eba_buffer_list);
    spin_unlock(&eba_buffer_list_lock);

    pr_info("EBA: Allocated memory at %p (phys: %llx), size: %llu bytes\n",
            ptr, buf->physical_address, size);
    return ptr;
}

int eba_internals_free(void *ptr)
{
    struct eba_buffer *buf;
    uint64_t phys;
    int found = 0;

    if (!eba_pool || !ptr)
         return -1;

    phys = virt_to_phys(ptr);

    /* Find the tracking structure associated with this allocation */
    spin_lock(&eba_buffer_list_lock);
    list_for_each_entry(buf, &eba_buffer_list, node) {
         if (buf->physical_address == phys) {
              list_del(&buf->node);
              found = 1;
              break;
         }
    }
    spin_unlock(&eba_buffer_list_lock);

    if (!found) {
         pr_err("EBA: Attempt to free unknown memory at %p (phys: %llx)\n",
                ptr, phys);
         return -1;
    }

    /* Free the memory from the pool */
    gen_pool_free(eba_pool, (unsigned long)ptr, buf->size);
    kfree(buf);

    pr_info("EBA: Freed memory at %p (phys: %llx), size: %llu bytes\n",
            ptr, phys, buf->size);
    return 0;
}

struct eba_buffer* eba_buffer_internals_lookup_buf(uint64_t physical_addr)
{
    struct eba_buffer *buf;

    spin_lock(&eba_buffer_list_lock);
    list_for_each_entry(buf, &eba_buffer_list, node) {
         if (buf->physical_address == physical_addr) {
              spin_unlock(&eba_buffer_list_lock);
              return buf;
         }
    }
    spin_unlock(&eba_buffer_list_lock);
    return NULL;
}

void eba_internals_mempool_cleanup(void)
{
    struct eba_buffer *buf, *tmp;

    /* Delete and free all tracking buffers */
    spin_lock(&eba_buffer_list_lock);
    list_for_each_entry_safe(buf, tmp, &eba_buffer_list, node) {
         list_del(&buf->node);
         kfree(buf);
    }
    spin_unlock(&eba_buffer_list_lock);

    if (eba_pool) {
         if (eba_pool_mem) {
              kfree(eba_pool_mem);
              eba_pool_mem = NULL;
         }
         gen_pool_destroy(eba_pool);
         eba_pool = NULL;
    }
    pr_info("EBA: Memory pool and tracking structures cleaned up\n");
}
int eba_internals_memory_stress(void)
{
    const int num_allocs = 100;  /* Number of allocations in the stress test */
    void **allocations;
    int *sizes;
    int i, ret;
    struct eba_buffer *buf;

    /* Dynamically allocate the arrays to avoid a large stack frame */
    allocations = kmalloc_array(num_allocs, sizeof(*allocations), GFP_KERNEL);
    if (!allocations)
        return -ENOMEM;

    sizes = kmalloc_array(num_allocs, sizeof(*sizes), GFP_KERNEL);
    if (!sizes) {
        kfree(allocations);
        return -ENOMEM;
    }

    /* Initialize sizes and set allocation pointers to NULL */
    for (i = 0; i < num_allocs; i++) {
        sizes[i] = 16 + ((i * 31) % 1024);
        allocations[i] = NULL;
    }

    /* Allocation phase: allocate memory chunks and verify tracking info */
    for (i = 0; i < num_allocs; i++) {
        allocations[i] = eba_internals_malloc(sizes[i]);
        if (!allocations[i]) {
            pr_err("EBA-STRESS: Allocation %d of size %d failed\n", i, sizes[i]);
            goto cleanup;
        }

        /* Lookup the tracking buffer using the physical address */
        buf = eba_buffer_internals_lookup_buf(virt_to_phys(allocations[i]));
        if (!buf) {
            pr_err("EBA-STRESS: Lookup for allocation %d (size %d) failed\n", i, sizes[i]);
            goto cleanup;
        }
    }
    pr_info("EBA-STRESS: Allocation phase completed successfully\n");

    /* De-allocation phase: free all allocated memory chunks */
    for (i = 0; i < num_allocs; i++) {
        ret = eba_internals_free(allocations[i]);
        if (ret != 0) {
            pr_err("EBA-STRESS: Freeing allocation %d failed\n", i);
            goto cleanup;
        }
    }
    pr_info("EBA-STRESS: De-allocation phase completed successfully\n");

    kfree(allocations);
    kfree(sizes);
    return 0;

cleanup:
    /*
     * In the event of a failure, free any allocations that succeeded.
     * Then free the dynamically allocated arrays.
     */
    for (i = 0; i < num_allocs; i++) {
        if (allocations[i])
            eba_internals_free(allocations[i]);
    }
    kfree(allocations);
    kfree(sizes);
    return -1;
}