/**
 * @file eba.h
 * @brief EBA IOCTL interface header.
 *
 * This header defines the data structures, macros, and IOCTL command
 * definitions for interacting with the EBA driver. It provides the interface
 * for user-space applications to perform buffer allocation, reading,
 * writing, and remote operations via IOCTL calls.
 */
#ifndef _EBA_H
#define _EBA_H
#include <linux/ioctl.h>
#include <linux/types.h>
#include <stdbool.h>
/* IOCTL Magic Number */

/** EBA IOCTL magic number used to create unique IOCTL command codes. */
#define EBA_IOC_MAGIC 'E'


/* IOCTL Command Definitions */

/**
 * EBA_IOCTL_ALLOC - IOCTL command for buffer allocation.
 *
 * This command uses the struct eba_alloc_data structure for both input and output.
 */
#define EBA_IOCTL_ALLOC _IOWR(EBA_IOC_MAGIC, 1, struct eba_alloc_data)

/**
 * EBA_IOCTL_WRITE - IOCTL command for writing data to an allocated buffer.
 *
 * This command uses the struct eba_write structure to specify write operations.
 */
#define EBA_IOCTL_WRITE _IOW(EBA_IOC_MAGIC, 2, struct eba_write)

/**
 * EBA_IOCTL_READ - IOCTL command for reading data from an allocated buffer.
 *
 * This command uses the struct eba_read structure to specify read operations.
 */
#define EBA_IOCTL_READ  _IOR(EBA_IOC_MAGIC, 3, struct eba_read)


/**
 * EBA_IOCTL_REMOTE_ALLOC - IOCTL command for remote buffer allocation.
 *
 * This command uses the struct eba_remote_alloc structure to request and obtain
 * a buffer allocation on a remote node.
 */
#define EBA_IOCTL_REMOTE_ALLOC _IOWR(EBA_IOC_MAGIC, 4, struct eba_remote_alloc)

/**
 * EBA_IOCTL_REMOTE_WRITE - IOCTL command for writing to a remote buffer.
 *
 * This command uses the struct eba_remote_write structure to perform a remote write operation.
 */
#define EBA_IOCTL_REMOTE_WRITE _IOWR(EBA_IOC_MAGIC, 5, struct eba_remote_write)

/**
 * EBA_IOCTL_REMOTE_READ - IOCTL command for reading from a remote buffer.
 *
 * This command uses the struct eba_remote_read structure to perform a remote read operation.
 */
#define EBA_IOCTL_REMOTE_READ _IOWR(EBA_IOC_MAGIC, 6, struct eba_remote_read)


/**
 * EBA_IOCTL_DISCOVER - IOCTL command to trigger a node discovery.
 *
 * This command does not require an argument and simply invokes the
 * ebp_discover() function to broadcast a discovery request.
 */
#define EBA_IOCTL_DISCOVER _IO(EBA_IOC_MAGIC, 7)

/**
 * EBA_IOCTL_EXPORT_NODE_SPECS - IOCTL command to export all node_specs buffers to files.
 *
 * This command triggers the eba_export_node_specs() function, which dumps each node's
 * node_specs buffer to a corresponding file.
 */
#define EBA_IOCTL_EXPORT_NODE_SPECS _IO(EBA_IOC_MAGIC, 8)

/**
 * EBA_IOCTL_GET_NODE_INFOS - IOCTL command to copy all known nodes .
 *
 */
#define EBA_IOCTL_GET_NODE_INFOS _IOWR(EBA_IOC_MAGIC, 9, struct eba_node_info)


#define EBA_IOCTL_WAIT_IID  _IOWR(EBA_IOC_MAGIC, 10, struct eba_wait_iid)

#define EBA_IOCTL_WAIT_BUFFER  _IOWR(EBA_IOC_MAGIC, 11, struct eba_wait_buffer)

#define EBA_IOCTL_REGISTER_SERVICE _IOWR(EBA_IOC_MAGIC, 12, struct eba_register_service)

#define EBA_IOCTL_REGISTER_QUEUE _IOWR(EBA_IOC_MAGIC, 13, struct eba_register_queue)

#define EBA_IOCTL_ENQUEUE _IOWR(EBA_IOC_MAGIC, 14, struct eba_enqueue)

#define EBA_IOCTL_DEQUEUE _IOWR(EBA_IOC_MAGIC, 15, struct eba_dequeue)

/* Logging Macros */

#define EBA_PR(fmt, lvl, ...) printk(KERN_##lvl "EBA-" #lvl ": " fmt, ##__VA_ARGS__)
/** Log an informational message for the EBA driver. */
#define EBA_INFO(fmt, ...) EBA_PR(fmt, INFO,   ##__VA_ARGS__)
/** Log an error message for the EBA driver. */
#define EBA_ERR(fmt, ...)  EBA_PR(fmt, ERR,    ##__VA_ARGS__)
/** Log a warning message for the EBA driver. */
#define EBA_WARN(fmt, ...) EBA_PR(fmt, WARNING,##__VA_ARGS__)
extern bool eba_debug;
/** Log a debug message for the EBA driver. */
#define EBA_DBG(fmt, ...)                         \
     do                                           \
     {                                            \
          if (eba_debug)                          \
               EBA_PR(fmt, DEBUG, ##__VA_ARGS__); \
     } while (0)


/* Timer Settings */
/** Timer period for the buffer clean-up callback in milliseconds (60000 ms = 1 minute). */
#define EBA_CLEAN_BUFFER_CALLBACK_TIMER 60000// 1 min

/**
 * struct eba_alloc_data - Structure for buffer allocation via IOCTL.
 * @size:       Number of bytes to allocate.
 * @life_time:  Lifetime for the buffer allocation in seconds.
 * @buff_id:    Returned buffer identifier (virtual address of the allocated buffer).
 *
 * This structure is used by user-space applications to request a buffer allocation.
 * The user specifies the desired size and lifetime; upon success, the driver returns
 * a buffer identifier that represents a virtual memory address.
 */
struct eba_alloc_data {
    __u64 size;  
    __u64 life_time;
    __u64 buff_id;  
};

/**
 * struct eba_rw_data - Structure for read operation via IOCTL.
 * @buff_id:   Buffer identifier (virtual address returned by an allocation call).
 * @off:       Offset within the buffer where the read begins.
 * @size:      Number of bytes to read.
 * @user_addr: Pointer to user‑space destination buffer.
 */
struct eba_read {
    __u64 buffer_id;
    __u64 offset;
    __u64 size;
    __u64 user_addr;
};

/**
 * struct eba_rw_data - Structure for write operation via IOCTL.
 * @buff_id:   Buffer identifier (virtual address returned by an allocation call).
 * @off:       Offset within the buffer where the write begins.
 * @size:      Number of bytes to write.
 * @payload:   Pointer to the data payload to be written.
 */
struct eba_write {
    __u64 buff_id;
    __u64 offset;
    __u64 size;
    char *payload;
};

/**
 * struct eba_remote_alloc - Structure for remote buffer allocation via IOCTL.
 * @size:       Number of bytes to allocate on the remote node.
 * @life_time:  Lifetime for the remote buffer allocation in seconds.
 * @buffer_id:  Buffer id that will store the remotely allocated buffer.
 * @node_id:    Target node id .
 * @iid:        The invocation id that will be returned.
 * This structure is used to request a buffer allocation on a remote node.
 */
struct eba_remote_alloc{
    __u64 size;      
    __u64 life_time; 
    __u64 buffer_id;
    __u16 node_id;
    __u32 iid;
};

/**
 * struct eba_remote_write - Structure for remote buffer write operations via IOCTL.
 * @buff_id:  Remote buffer identifier.
 * @offset:   Offset within the remote buffer where the write operation should begin.
 * @size:     Number of bytes to write.
 * @payload:  Pointer to the data payload to be written.
 * @node_id:  Target node id .
 * @iid:      The invocation id that will be returned.
 * This structure is used to send a write request to a remote node by specifying the target
 * buffer, the offset within that buffer, and the data to be written.
 */
struct eba_remote_write{
    __u64 buff_id;      
    __u64 offset; 
    __u64 size;
    char* payload;
    __u16 node_id;
    __u32 iid;
    
};

/**
 * struct eba_remote_read - Structure for remote buffer read operations via IOCTL.
 * @dst_buffer_id: Identifier for the destination (local) buffer where data will be stored.
 * @src_buffer_id: Identifier for the source (remote) buffer from which data is to be read.
 * @dst_offset:    Offset within the destination buffer where the data should be written.
 * @src_offset:    Offset within the source buffer where the reading should begin.
 * @size:          Number of bytes to read.
 * @node_id:       Target node id .
 * @iid:           The invocation id that will be returned.
 * This structure is used to request a read operation from a remote node. It specifies
 * both the source (remote) buffer and the destination (local) buffer along with the respective
 * offsets and the size of data to be transferred.
 */
struct eba_remote_read {
    __u64 dst_buffer_id;
    __u64 src_buffer_id;
    __u64 dst_offset;
    __u64 src_offset;
    __u64 size;
    __u16 node_id;
    __u32 iid;
    
};

/**
 * struct eba_node_info - user‐visible copy of kernel's node_info
 * @id:          Node ID (same as kernel->node_infos[i].id)
 * @mtu:         MTU advertised by that node
 * @mac:         6‐byte MAC address
 * @node_specs:  pointer/handle to that node's specs buffer
 */
struct eba_node_info {
    __u16        id;
    __u16        mtu;
    unsigned char mac[6];
    __u64        node_specs;
};

/**
 * struct eba_wait_iid - userspace argument for EBA_IOCTL_WAIT_IID
 * @iid:           Invocation-ID to wait for
 * @wanted_status: value of ebp_invoke_ack::status that shall wake us
 * @timeout_ms:    maximum time to sleep (0 == infinite)
 * @rc:            filled by the driver (0, -ETIMEDOUT, -ENOSPC, …)
 * @timed_out      0 = woke-up because of ACK, 1 = woke-up because of timeout
 */
struct eba_wait_iid {
    __u32  iid;
    __u8   wanted_status;
    __u32  timeout_ms;
    __s32  rc;
    __u8   timed_out;
};


/**
 * struct eba_wait_buffer - userspace argument for EBA_IOCTL_WAIT_BUFFER
 * @buff_id:       Buffer-ID to wait for
 * @timeout_ms:    maximum time to sleep (0 == infinite)
 * @rc:            filled by the driver (0, -ETIMEDOUT, -ENOSPC, …)
 * @timed_out      0 = woke-up because of a write , 1 = woke-up because of timeout
 */
struct eba_wait_buffer {
    __u64  buff_id;
    __u32  timeout_ms;
    __s32  rc;
    __u8   timed_out;
};

/**
 * struct eba_register_service - userspace argument for EBA_IOCTL_REGISTER_SERVICE
 * @buff_id:       Buffer-ID to register as a service
 * @new_id:        New ID to register the service with
 */
struct eba_register_service
{
    __u64  buff_id;
    __u64  new_id;
};

/**
 * struct eba_register_queue - userspace argument for EBA_IOCTL_REGISTER_QUEUE
 * @buff_id:       Buffer-ID to register as a queue
 */
struct eba_register_queue
{
    __u64  buff_id;
};

/**
 * struct eba_enqueue - userspace argument for EBA_IOCTL_ENQUEUE
 * @buff_id:       Buffer-ID to enqueue data into
 * @data:         Pointer to the data to enqueue
 * @size:         Number of bytes to enqueue
 */
struct eba_enqueue
{
    __u64  buff_id;
    __u64  data;
    __u64  size;
};

/**
 * struct eba_dequeue - userspace argument for EBA_IOCTL_DEQUEUE
 * @buff_id:       Buffer-ID to dequeue data from
 * @data:         Pointer to the data to dequeue
 * @size:         Number of bytes to dequeue
 */
struct eba_dequeue
{
    __u64  buff_id;
    __u64  data;
    __u64  size;
};

#endif /* _EBA_H */
