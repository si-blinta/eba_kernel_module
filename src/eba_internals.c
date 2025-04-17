#include "eba_internals.h"
#include "eba.h"

/* Global list of currently allocated buffers */
LIST_HEAD(eba_buffer_list);

/* Spinlock protecting the global buffer list */
spinlock_t eba_buffer_list_lock;

/* genpool instance and its backing memory */
struct gen_pool *eba_pool = NULL;
void *eba_pool_mem = NULL;

/*========================================================================*/
/*                       Memory‑pool Initialization                       */
/*========================================================================*/

int eba_internals_mempool_init(void)
{
     /* 1) Create a generic pool aligned to 8 bytes.
      */
     eba_pool = gen_pool_create(8, -1);
     if (!eba_pool)
     {
          EBA_ERR("EBA: Failed to create memory pool\n");
          return 1;
     }

     /* 2) Back the pool with vmalloc() rather than kmalloc().
      *    vmalloc gives us a large virtually contiguous region even if
      *    physical pages aren’t contiguous, ideal for big pools.
      *    kmalloc would require physically contiguous pages and likely fail
      *    for large MEMORY_POOL_SIZE.
      */
     eba_pool_mem = vmalloc(MEMORY_POOL_SIZE);
     if (!eba_pool_mem)
     {
          EBA_ERR("EBA: Failed to allocate memory for the pool\n");
          gen_pool_destroy(eba_pool);
          return 1;
     }

     /* 3) Register the vmalloc’d region with the genpool.
      *    We pass phys_addr=0 because vmalloc memory has no single
      *    linear physical base.
      */

     gen_pool_add_virt(eba_pool, (unsigned long)eba_pool_mem, 0, MEMORY_POOL_SIZE, -1);

     /* 4) Initialize the spinlock for tracking list operations */

     spin_lock_init(&eba_buffer_list_lock);

     EBA_INFO("EBA: Memory pool initialized successfully\n");
     return 0;
}

/*========================================================================*/
/*                         Allocate from the Pool                         */
/*========================================================================*/

void *eba_internals_malloc(uint64_t size, uint64_t life_time)
{
     unsigned long addr;
     void *ptr;
     struct eba_buffer *buf;

     /* Prevent allocation if pool isn't ready */
     if (!eba_pool)
          return NULL;

     /* Grab 'size' bytes from the genpool */
     addr = gen_pool_alloc(eba_pool, size);
     if (!addr)
     {
          EBA_ERR("EBA: Allocation failed for size %llu\n", size);
          return NULL;
     }
     ptr = (void *)addr;
     /* Zero out new region for safety */
     memset(ptr, 0, size);

     /* Allocate and initialize the tracking structure */
     buf = kmalloc(sizeof(*buf), GFP_KERNEL);
     if (!buf)
     {
          EBA_ERR("EBA: Tracking structure allocation failed\n");
          gen_pool_free(eba_pool, addr, size);
          return NULL;
     }
     memset(buf, 0, sizeof(*buf));

     /* Per‑buffer lock to serialize reads/writes */
     spin_lock_init(&buf->lock);

     buf->type = 0; /* Set default type (todo add types) */

     /* Store the allocated pointer */
     buf->address = (unsigned long)ptr;
     buf->size = size;

     /* Compute expiration (0 means “never expire”) */
     if (life_time != 0)
          buf->expires = ktime_get_real_seconds() + life_time;
     else
          buf->expires = 0;

     /* Link into the global list under lock */
     INIT_LIST_HEAD(&buf->node);
     spin_lock(&eba_buffer_list_lock);
     list_add(&buf->node, &eba_buffer_list);
     spin_unlock(&eba_buffer_list_lock);

     EBA_INFO("EBA: Allocated memory at %llu, size: %llu bytes, lifetime: %llu sec\n",
              (uint64_t)ptr, size, life_time);

     return ptr;
}

/*========================================================================*/
/*                          Free a Single Buffer                          */
/*========================================================================*/

int eba_internals_free(void *ptr)
{
     struct eba_buffer *buf;
     int found = 0;

     /* Reject NULL or uninitialized pool */
     if (!eba_pool || !ptr)
          return -1;

     /* Find matching tracking entry */
     spin_lock(&eba_buffer_list_lock);
     list_for_each_entry(buf, &eba_buffer_list, node)
     {
          if (buf->address == (unsigned long)ptr)
          {
               list_del(&buf->node);
               found = 1;
               break;
          }
     }
     spin_unlock(&eba_buffer_list_lock);

     /* If no tracking structure is found, log an error and return -1 */
     if (!found)
     {
          EBA_ERR("EBA: Attempt to free unknown memory at %llu\n", (uint64_t)ptr);
          return -1;
     }

     /* Return memory to the pool */
     gen_pool_free(eba_pool, (unsigned long)ptr, buf->size);

     /* Free the tracking structure */
     kfree(buf);

     EBA_INFO("EBA: Freed memory at %llu, size: %llu bytes\n",
              (uint64_t)ptr, buf->size);
     return 0;
}

/*========================================================================*/
/*                         Freeing the  Entire Pool                       */
/*========================================================================*/

void eba_internals_mempool_free(void)
{
     struct eba_buffer *buf, *tmp;
     int outstanding = 0;

     /* Count and report how many bufs are still in use (debug) */
     spin_lock(&eba_buffer_list_lock);
     list_for_each_entry(buf, &eba_buffer_list, node)
     {
          outstanding++;
     }
     EBA_INFO("EBA: %d outstanding buffers before cleanup\n", outstanding);
     /* Force‑free any remaining buffers */
     if (outstanding > 0)
     {
          list_for_each_entry_safe(buf, tmp, &eba_buffer_list, node)
          {
               EBA_INFO("EBA: Forcing free of buffer at %llu, size: %llu bytes\n",
                        buf->address, buf->size);
               list_del(&buf->node);
               gen_pool_free(eba_pool, buf->address, buf->size);
               kfree(buf);
          }
     }

     /* Destroy pool and release backing storage */
     if (eba_pool)
     {
          gen_pool_destroy(eba_pool);
          eba_pool = NULL;
     }
     if (eba_pool_mem)
     {
          vfree(eba_pool_mem);
          eba_pool_mem = NULL;
     }
     spin_unlock(&eba_buffer_list_lock);
     EBA_INFO("EBA: Memory pool freed successfully\n");
}

/*========================================================================*/
/*                      Simple malloc/free Stress Test                    */
/*========================================================================*/

int eba_internals_mem_stress_test(void)
{
     const int num_allocs = 100; /* Number of allocations for the stress test */
     void **allocations;         /* Array to store pointers to allocated memory */
     int *sizes;                 /* Array to store the sizes for each allocation */
     int i, ret;

     /* Dynamically allocate arrays for tracking allocations and their sizes */
     allocations = kmalloc_array(num_allocs, sizeof(void *), GFP_KERNEL);
     if (!allocations)
          return -ENOMEM;

     sizes = kmalloc_array(num_allocs, sizeof(int), GFP_KERNEL);
     if (!sizes)
     {
          kfree(allocations);
          return -ENOMEM;
     }

     /* Initialize sizes and set each allocation pointer to NULL */
     for (i = 0; i < num_allocs; i++)
     {
          sizes[i] = 16 + ((i * 31) % 1024);
          allocations[i] = NULL;
     }

     /* Allocation Phase: Allocate memory chunks and verify each allocation */
     for (i = 0; i < num_allocs; i++)
     {
          allocations[i] = eba_internals_malloc(sizes[i], 10); /* Lifetime set to 10 sec */
          if (!allocations[i])
          {
               EBA_ERR("EBA-STRESS: Allocation %d of size %d failed\n", i, sizes[i]);
               ret = -ENOMEM;
               goto cleanup;
          }
     }
     EBA_INFO("EBA-STRESS: Allocation phase completed successfully\n");

     /* De-allocation Phase: Free all allocated memory chunks */
     for (i = 0; i < num_allocs; i++)
     {
          ret = eba_internals_free(allocations[i]);
          if (ret != 0)
          {
               EBA_ERR("EBA-STRESS: Freeing allocation %d failed\n", i);
               goto cleanup;
          }
     }
     EBA_INFO("EBA-STRESS: De-allocation phase completed successfully\n");

     ret = 0; /* If all allocations and frees succeed, return success */

cleanup:

     kfree(allocations);
     kfree(sizes);
     return ret;
}

/*========================================================================*/
/*                      Buffer Write and Read APIs                        */
/*========================================================================*/

int eba_internals_write(const void *data, uint64_t buff_id, uint64_t off, uint64_t size)
{
     struct eba_buffer *buf;
     int found = 0;

     if (!data)
     {
          EBA_ERR("eba_internals_write: data pointer is NULL\n");
          return -EINVAL;
     }

     /* Lookup buffer by its id (virtual address) */
     spin_lock(&eba_buffer_list_lock);
     list_for_each_entry(buf, &eba_buffer_list, node)
     {
          if (buf->address == buff_id)
          {
               found = 1;
               break;
          }
     }
     spin_unlock(&eba_buffer_list_lock);

     if (!found)
     {
          EBA_ERR("eba_internals_write: Buffer with id %llu not found\n", buff_id);
          return -EINVAL; /* Buffer not found */
     }
     /* Protect concurrent writes/reads */
     spin_lock(&buf->lock);
     /* Bounds check to prevent overflows */
     if (off + size > buf->size)
     {
          EBA_ERR("eba_internals_write: Write out of bounds: off(%llu) + size(%llu) > buf->size(%llu)\n",
                  off, size, buf->size);
          spin_unlock(&buf->lock);
          return -EINVAL;
     }

     /* Copy the data into the allocated buffer at the specified offset */
     memcpy((void *)(buf->address + off), data, size);
     spin_unlock(&buf->lock);

     EBA_INFO("EBA: Written %llu bytes at offset %llu on buf %llu\n", size, off, buff_id);
     return 0;
}

int eba_internals_read(void *data_out, uint64_t buff_id, uint64_t off, uint64_t size)
{
     struct eba_buffer *buf;
     int found = 0;

     if (!data_out)
     {
          EBA_ERR("eba_internals_read: data pointer is NULL\n");
          return -EINVAL;
     }

     /* Lookup buffer by its id (virtual address) */
     spin_lock(&eba_buffer_list_lock);
     list_for_each_entry(buf, &eba_buffer_list, node)
     {
          if (buf->address == buff_id)
          {
               found = 1;
               break;
          }
     }
     spin_unlock(&eba_buffer_list_lock);

     if (!found)
     {
          EBA_ERR("eba_internals_read: Buffer with id %llu not found\n", buff_id);
          return -EINVAL; /* Buffer not found */
     }

     /* Protect concurrent writes/reads */
     spin_lock(&buf->lock);

     /* Bounds check to prevent overflows */
     if (off + size > buf->size)
     {
          EBA_ERR("eba_internals_read: Read out of bounds: off(%llu) + size(%llu) > buf->size(%llu)\n", off, size, buf->size);
          spin_unlock(&buf->lock);
          return -EINVAL;
     }

     /* Copy data from the allocated buffer at the specified offset into data_out */
     memcpy(data_out, (void *)(buf->address + off), size);
     spin_unlock(&buf->lock);

     EBA_INFO("EBA: Read %llu bytes at offset %llu on buf %llu\n", size, off, buff_id);
     return 0;
}

/*========================================================================*/
/*                   Read/Write Stress‑Test Utility                       */
/*========================================================================*/

int eba_internals_rw_stress_test(void)
{
     const int num_allocs = 100; /* Number of buffers to allocate for the test */
     void **allocations;         /* Array to store allocated buffer pointers */
     int *sizes;                 /* Array to store the sizes for each buffer */
     int i, ret = 0;
     unsigned char *pattern = NULL;
     unsigned char *read_back = NULL;

     /* Allocate arrays to track allocated buffers and their sizes */
     allocations = kmalloc_array(num_allocs, sizeof(void *), GFP_KERNEL);
     if (!allocations)
          return -ENOMEM;

     sizes = kmalloc_array(num_allocs, sizeof(int), GFP_KERNEL);
     if (!sizes)
     {
          kfree(allocations);
          return -ENOMEM;
     }

     /* Initialize the sizes and set each allocation pointer to NULL */
     for (i = 0; i < num_allocs; i++)
     {
          sizes[i] = 16 + ((i * 31) % 1024); /* Buffer size will vary between 16 and about 1040 bytes */
          allocations[i] = NULL;
     }

     /* Allocation and test phase */
     for (i = 0; i < num_allocs; i++)
     {
          /* Allocate a buffer with a lifetime of 10 seconds */
          allocations[i] = eba_internals_malloc(sizes[i], 10);
          if (!allocations[i])
          {
               EBA_ERR("EBA-RW-STRESS: Allocation %d of size %d failed\n", i, sizes[i]);
               ret = -ENOMEM;
               goto cleanup;
          }

          /* Allocate temporary buffers for writing and reading */
          pattern = kmalloc(sizes[i], GFP_KERNEL);
          if (!pattern)
          {
               ret = -ENOMEM;
               goto cleanup;
          }
          read_back = kmalloc(sizes[i], GFP_KERNEL);
          if (!read_back)
          {
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
          if (ret != 0)
          {
               EBA_ERR("EBA-RW-STRESS: Write failed on buffer %d\n", i);
               kfree(pattern);
               kfree(read_back);
               goto cleanup;
          }

          /* Read back the data into the read_back buffer */
          ret = eba_internals_read(read_back, (uint64_t)allocations[i], 0, sizes[i]);
          if (ret != 0)
          {
               EBA_ERR("EBA-RW-STRESS: Read failed on buffer %d\n", i);
               kfree(pattern);
               kfree(read_back);
               goto cleanup;
          }

          /* Compare the written pattern with the read-back data */
          if (memcmp(pattern, read_back, sizes[i]) != 0)
          {
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
     for (i = 0; i < num_allocs; i++)
     {
          if (allocations[i])
               eba_internals_free(allocations[i]);
     }
     kfree(allocations);
     kfree(sizes);
     return ret;
}

/*========================================================================*/
/*                  Automatic Expiration of Buffers                       */
/*========================================================================*/

void eba_check_expired_buffers(void)
{
     struct eba_buffer *buf, *tmp;
     ktime_t now = ktime_get_real_seconds();

     // Acquire the spinlock before iterating the list.
     spin_lock(&eba_buffer_list_lock);

     list_for_each_entry_safe(buf, tmp, &eba_buffer_list, node)
     {

          // If expires is 0, the lifetime is infinite, so skip.
          if (buf->expires != 0 && now >= buf->expires)
          {
               EBA_INFO("EBA: Buffer at %llu expired (current time %lld >= expires %lld)\n",
                        buf->address, now, buf->expires);
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