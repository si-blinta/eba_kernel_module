#include "eba_internals.h"
#include "eba.h"

/* Global list of currently allocated buffers */
LIST_HEAD(eba_buffer_list);

/* Spinlock protecting the global buffer list */
spinlock_t eba_buffer_list_lock;

/* genpool instance and its backing memory */
struct gen_pool *eba_pool = NULL;
void *eba_pool_mem = NULL;
uint64_t current_id = EBA_MAX_SERVICES; /* Global variable to keep track of the unique ID */
/* This ID will be incremented for each allocation */
struct spinlock eba_id_lock; /* Spinlock to protect the ID increment operation */

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
     spin_lock_init(&eba_id_lock);
     EBA_INFO("%s: vmalloc pool %zu bytes -> genpool OK\n", __func__, (size_t)MEMORY_POOL_SIZE);
     return 0;
}

/*========================================================================*/
/*                         Allocate from the Pool                         */
/*========================================================================*/

/*I want instead of returning the virtual address, i want to return a unique ID*/
uint64_t eba_internals_malloc(uint64_t size, uint64_t life_time)
{
     unsigned long addr;
     void *ptr;
     struct eba_buffer *buf;

     /* Prevent allocation if pool isn't ready */
     if (!eba_pool)
          return 0;

     /* Grab 'size' bytes from the genpool */
     addr = gen_pool_alloc(eba_pool, size);
     if (!addr)
     {
          EBA_ERR("%s: gen_pool_alloc(%llu) failed\n", __func__, size);
          return 0;
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
          return 0;
     }
     memset(buf, 0, sizeof(*buf));

     /* Per‑buffer lock to serialize reads/writes */
     spin_lock_init(&buf->lock);

     buf->type = 0; /* Set default type (todo add types) */

     /* Store the allocated pointer */
     buf->address = (unsigned long)ptr;
     buf->size = size;
     spin_lock(&eba_id_lock);
     buf->id = current_id++; /* Increment the ID for the next allocation */
     spin_unlock(&eba_id_lock);
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

     EBA_DBG("%s: id =%llu size=%llu life=%llus\n",__func__, buf->id,size, life_time);

     return buf->id;
}

/*========================================================================*/
/*                          Free a Single Buffer                          */
/*========================================================================*/

int eba_internals_free(uint64_t id)
{
     struct eba_buffer *buf;
     int found = 0;

     /* Reject NULL or uninitialized pool */
     if (!eba_pool)
          return -1;

     /* Find matching tracking entry */
     spin_lock(&eba_buffer_list_lock);
     list_for_each_entry(buf, &eba_buffer_list, node)
     {
          if (buf->id == id)
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
          EBA_WARN("%s: free of unknown id=%llu\n", __func__, id);
          return -1;
     }

     /* Return memory to the pool */
     gen_pool_free(eba_pool,buf->address, buf->size);

     /* Free the tracking structure */
     kfree(buf);

     EBA_INFO("%s: freed buf=%llu size=%llu\n", __func__, buf->id, buf->size);
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
     EBA_INFO("%s: %d buffers remain, forcing cleanup\n",__func__, outstanding);
     /* Force‑free any remaining buffers */
     if (outstanding > 0)
     {
          list_for_each_entry_safe(buf, tmp, &eba_buffer_list, node)
          {
               EBA_DBG("%s: force-free buf=%llu size=%llu\n",__func__, buf->id, buf->size);
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
     EBA_INFO("%s: pool destroyed\n", __func__);
}

/*========================================================================*/
/*                      Simple malloc/free Stress Test                    */
/*========================================================================*/

int eba_internals_mem_stress_test(void)
{
     const int num_allocs = 100; /* Number of allocations for the stress test */
     uint64_t *allocations;         /* Array to store pointers to allocated memory */
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

     /* Initialize sizes and set each allocation pointer to 0 */
     for (i = 0; i < num_allocs; i++)
     {
          sizes[i] = 16 + ((i * 31) % 1024);
          allocations[i] = 0;
     }

     /* Allocation Phase: Allocate memory chunks and verify each allocation */
     for (i = 0; i < num_allocs; i++)
     {
          allocations[i] = eba_internals_malloc(sizes[i], 10); /* Lifetime set to 10 sec */
          if (!allocations[i])
          {
               EBA_ERR("%s: Allocation %d of size %d failed\n",__func__, i, sizes[i]);
               ret = -ENOMEM;
               goto cleanup;
          }
     }
     EBA_DBG("%s: alloc phase (%d buffers) OK\n", __func__, num_allocs);

     /* De-allocation Phase: Free all allocated memory chunks */
     for (i = 0; i < num_allocs; i++)
     {
          ret = eba_internals_free(allocations[i]);
          if (ret != 0)
          {
               EBA_ERR("%s: Freeing allocation %d failed\n",__func__, i);
               goto cleanup;
          }
     }
     EBA_DBG("%s: free phase (%d buffers) OK\n", __func__, num_allocs);

     EBA_INFO("%s: PASS\n", __func__);
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
          EBA_ERR("%s: data pointer is NULL\n",__func__);
          return -EINVAL;
     }

     /* Lookup buffer by its id (virtual address) */
     spin_lock(&eba_buffer_list_lock);
     list_for_each_entry(buf, &eba_buffer_list, node)
     {
          if (buf->id == buff_id)
          {
               found = 1;
               break;
          }
     }
     spin_unlock(&eba_buffer_list_lock);

     if (!found)
     {
          EBA_ERR("%s: Buffer with id %llu not found\n", __func__,buff_id);
          return -EINVAL; /* Buffer not found */
     }
     /* Protect concurrent writes/reads */
     spin_lock(&buf->lock);
     /* Bounds check to prevent overflows */
     if (off + size > buf->size)
     {
          EBA_ERR("%s: Write out of bounds: off(%llu) + size(%llu) > buf->size(%llu)\n",__func__,
               off, size, buf->size);
          spin_unlock(&buf->lock);
          return -EINVAL;
     }

     /* Copy the data into the allocated buffer at the specified offset */
     memcpy((void *)(buf->address + off), data, size);
     EBA_DBG("%s: id=%llu off=%llu size=%llu\n",__func__, buff_id, off, size);
     spin_unlock(&buf->lock);
     /* Wake the process that is waiting on that buffer */

     return 0;
}

int eba_internals_read(void *data_out, uint64_t buff_id, uint64_t off, uint64_t size)
{
     struct eba_buffer *buf;
     int found = 0;

     if (!data_out)
     {
          EBA_ERR("%s: data pointer is NULL\n",__func__);
          return -EINVAL;
     }

     /* Lookup buffer by its id (virtual address) */
     spin_lock(&eba_buffer_list_lock);
     list_for_each_entry(buf, &eba_buffer_list, node)
     {
          if (buf->id == buff_id)
          {
               found = 1;
               break;
          }
     }
     spin_unlock(&eba_buffer_list_lock);

     if (!found)
     {
          EBA_ERR("%s: Buffer with id %llu not found\n",__func__, buff_id);
          return -EINVAL; /* Buffer not found */
     }

     /* Protect concurrent writes/reads */
     spin_lock(&buf->lock);

     /* Bounds check to prevent overflows */
     if (off + size > buf->size)
     {
          EBA_ERR("%s: Read out of bounds: off(%llu) + size(%llu) > buf->size(%llu)\n",__func__, off, size, buf->size);
          spin_unlock(&buf->lock);
          return -EINVAL;
     }

     /* Copy data from the allocated buffer at the specified offset into data_out */
     memcpy(data_out, (void *)(buf->address + off), size);
     spin_unlock(&buf->lock);

     EBA_DBG("%s: id=%llu off=%llu size=%llu\n",__func__, buff_id, off, size);
     return 0;
}

/*========================================================================*/
/*                   Read/Write Stress‑Test Utility                       */
/*========================================================================*/

int eba_internals_rw_stress_test(void)
{
     const int num_allocs = 100; /* Number of buffers to allocate for the test */
     uint64_t *allocations;         /* Array to store allocated buffer pointers */
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
          allocations[i] = 0;
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

          EBA_DBG("%s: buf[%d]=%llu size=%d verified\n",__func__, i, allocations[i], sizes[i]);
     }

     EBA_INFO("%s: rw-stress PASS\n", __func__);
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
               EBA_INFO("%s: Buffer at %llu expired (current time %lld >= expires %lld)\n",__func__,buf->address, now, buf->expires);
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


/*========================================================================*/
/*                      Registering Buffers as Services                   */
/*========================================================================*/

int register_service(uint64_t buff_id, uint64_t new_id)
{
     if(new_id > EBA_MAX_SERVICES || new_id == 0)
     {
          EBA_ERR("%s: new_id %llu is out of range\n",__func__, new_id);
          return -EINVAL; /* new_id out of range */
     }
     struct eba_buffer *buf;
     int found = 0;

     /* Lookup buffer by its id */
     spin_lock(&eba_buffer_list_lock);
     list_for_each_entry(buf, &eba_buffer_list, node)
     {
          if (buf->id == buff_id)
          {
               found = 1;
               break;
          }
     }
     spin_unlock(&eba_buffer_list_lock);

     if (!found)
     {
          EBA_ERR("%s: Buffer with id %llu not found\n",__func__, buff_id);
          return -EINVAL; /* Buffer not found */
     }
     spin_lock(&buf->lock);
     buf->id = new_id; /* Set the id to the one given by the client */
     spin_unlock(&buf->lock);
     return 0;
}

//*========================================================================*/
/*                      Registering buffers as queue                       */
/*========================================================================*/

int register_queue(uint64_t buff_id)
{
     struct eba_buffer *buf;
     int found = 0;

     /* Lookup buffer by its id */
     spin_lock(&eba_buffer_list_lock);
     list_for_each_entry(buf, &eba_buffer_list, node)
     {
          if (buf->id == buff_id)
          {
               found = 1;
               break;
          }
     }
     spin_unlock(&eba_buffer_list_lock);

     if (!found)
     {
          EBA_ERR("%s: Buffer with id %llu not found\n",__func__, buff_id);
          return -EINVAL; /* Buffer not found */
     }    
     /*make the buffer like a queue, first two 8 bytes will hold the head and tail and the rest will be the data*/
     spin_lock(&buf->lock);
     buf->address = (unsigned long)buf->address + sizeof(struct queue);
     buf->size = buf->size - sizeof(struct queue);
     spin_unlock(&buf->lock);
     /*initialize the head and tail*/
     struct queue *q = (struct queue *)buf->address;
     q->head = 0;
     q->tail = 0;
     EBA_DBG("%s: id=%llu head=%llu tail=%llu\n",__func__, buff_id, q->head, q->tail);
     return 0;
}

int eba_internals_enqueue(uint64_t buff_id, void *data, uint64_t size)
{
     struct eba_buffer *buf;
     int found = 0;

     /* Lookup buffer by its id */
     spin_lock(&eba_buffer_list_lock);
     list_for_each_entry(buf, &eba_buffer_list, node)
     {
          if (buf->id == buff_id)
          {
               found = 1;
               break;
          }
     }
     spin_unlock(&eba_buffer_list_lock);

     if (!found)
     {
          EBA_ERR("%s: Buffer with id %llu not found\n", __func__, buff_id);
          return -EINVAL; /* Buffer not found */
     }    
     spin_lock(&buf->lock);
     struct queue *q = (struct queue *)buf->address;

     /* Check if the queue is full using both head and tail.*/
     if ((q->tail + 1) % buf->size == q->head)
     {
          EBA_ERR("%s: Queue is full\n", __func__);
          spin_unlock(&buf->lock);
          return -ENOMEM; /* Queue is full */
     }

     /* Enqueue the data at the current tail index; account for queue header offset */
     memcpy((void *)(buf->address + sizeof(struct queue) + q->tail), data, size);

     /* Update tail pointer by the size of the enqueued data */
     q->tail = (q->tail + size) % buf->size;

     EBA_DBG("%s: id=%llu head=%llu tail=%llu\n", __func__, buff_id, q->head, q->tail);
     spin_unlock(&buf->lock);
     return 0;
}

int eba_internals_dequeue(uint64_t buff_id, void *data_out, uint64_t size)
{
     struct eba_buffer *buf;
     int found = 0;

     /* Lookup buffer by its id */
     spin_lock(&eba_buffer_list_lock);
     list_for_each_entry(buf, &eba_buffer_list, node)
     {
          if (buf->id == buff_id)
          {
               found = 1;
               break;
          }
     }
     spin_unlock(&eba_buffer_list_lock);

     if (!found)
     {
          EBA_ERR("%s: Buffer with id %llu not found\n", __func__, buff_id);
          return -EINVAL; /* Buffer not found */
     }    
     spin_lock(&buf->lock);
     struct queue *q = (struct queue *)buf->address;

     /* Check if the queue is empty using both head and tail.*/
     if (q->head == q->tail)
     {
          EBA_ERR("%s: Queue is empty\n", __func__);
          spin_unlock(&buf->lock);
          return -ENOMEM; /* Queue is empty */
     }

     /* Dequeue the data_out from the current head index; account for queue header offset */
     memcpy(data_out, (void *)(buf->address + sizeof(struct queue) + q->head), size);

     /* Update head pointer by the size of the dequeued data */
     q->head = (q->head + size) % buf->size;

     EBA_DBG("%s: id=%llu head=%llu tail=%llu\n", __func__, buff_id, q->head, q->tail);
     spin_unlock(&buf->lock);
     return 0;
}

int eba_internals_queue_stress_test(void)
{
     int ret;
     uint64_t buff_id;
     int queue_buffer_size = 128;   /* Total size including the queue control block */
     int num_items = 10;            /* Number of items to test enqueue/dequeue */
     int i;
     int data, out_data;

     /* Allocate a buffer that will be used as a queue.
        Note: The buffer must be large enough to hold the struct queue
        and the actual data. */
     buff_id = eba_internals_malloc(queue_buffer_size, 0); /* Infinite lifetime */
     if (!buff_id)
     {
          EBA_ERR("FAIL: Queue stress test: allocation failed\n");
          return -ENOMEM;
     }
     else {
          EBA_DBG("SUCCESS: Allocated queue buffer with buff_id = %llu\n", buff_id);
     }

     /* Register the buffer as a queue.
        This adjusts the buffer pointer and size appropriately. */
     ret = register_queue(buff_id);
     if (ret != 0)
     {
          EBA_ERR("FAIL: Queue stress test: register queue failed\n");
          eba_internals_free(buff_id);
          return ret;
     }
     else
     {
          EBA_DBG("SUCCESS: Registered buffer as queue, buff_id = %llu\n", buff_id);
     }

     /* Enqueue phase: enqueue a few integer items */
     for (i = 0; i < num_items; i++)
     {
          data = i;
          ret = eba_internals_enqueue(buff_id, &data, sizeof(data));
          if (ret != 0)
          {
               EBA_ERR("FAIL: Queue stress test: enqueue failed at index %d\n", i);
               goto cleanup;
          }
          else
          {
               EBA_DBG("SUCCESS: Enqueued item %d at index %d\n", data, i);
          }
     }

     /* Dequeue phase: remove items from the queue and verify */
     for (i = 0; i < num_items; i++)
     {
          ret = eba_internals_dequeue(buff_id, &out_data, sizeof(out_data));
          if (ret != 0)
          {
               EBA_ERR("FAIL: Queue stress test: dequeue failed at index %d\n", i);
               goto cleanup;
          }
          else if (out_data != i)
          {
               EBA_ERR("FAIL: Queue stress test: data mismatch at index %d, expected %d but got %d\n", i, i, out_data);
               ret = -EINVAL;
               goto cleanup;
          }
          else
          {
               EBA_DBG("SUCCESS: Dequeued item %d at index %d\n", out_data, i);
          }
     }

     EBA_INFO("SUCCESS: Queue stress test passed\n");
     ret = 0;

cleanup:
     eba_internals_free(buff_id);
     if(ret != 0)
          EBA_DBG("FAIL: Queue stress test encountered errors, ret = %d\n", ret);
     return ret;
}

struct eba_buffer *get_buffer_by_id(uint64_t id)
{
     struct eba_buffer *buf;
     int found = 0;

     /* Lookup buffer by its id */
     spin_lock(&eba_buffer_list_lock);
     list_for_each_entry(buf, &eba_buffer_list, node)
     {
          if (buf->id == id)
          {
               found = 1;
               break;
          }
     }
     spin_unlock(&eba_buffer_list_lock);

     if (!found)
     {
          EBA_ERR("%s: Buffer with id %llu not found\n", __func__, id);
          return NULL; /* Buffer not found */
     }
     return buf;
}