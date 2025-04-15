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
 * This command uses the struct eba_rw_data structure to specify write operations.
 */
#define EBA_IOCTL_WRITE _IOW(EBA_IOC_MAGIC, 2, struct eba_rw_data)

/**
 * EBA_IOCTL_READ - IOCTL command for reading data from an allocated buffer.
 *
 * This command uses the struct eba_rw_data structure to specify read operations.
 */
#define EBA_IOCTL_READ  _IOR(EBA_IOC_MAGIC, 3, struct eba_rw_data)


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

/** Maximum number of EBA IOCTL commands supported. */
#define EBA_IOC_MAXNR 6

/* Logging Macros */

/** Log an informational message for the EBA driver. */
#define EBA_INFO(fmt, ...)  pr_info("EBA: " fmt, ##__VA_ARGS__)
/** Log an error message for the EBA driver. */
#define EBA_ERR(fmt, ...)   pr_err("EBA: " fmt, ##__VA_ARGS__)
/** Log a warning message for the EBA driver. */
#define EBA_WARN(fmt, ...)  pr_warn("EBA: " fmt, ##__VA_ARGS__)
/** Log a debug message for the EBA driver. */
#define EBA_DBG(fmt, ...)   pr_debug("EBA: " fmt, ##__VA_ARGS__)

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
 * struct eba_rw_data - Structure for read and write operations via IOCTL.
 * @buff_id:   Buffer identifier (virtual address returned by an allocation call).
 * @off:       Offset within the buffer where the operation begins.
 * @size:      Number of bytes to read or write.
 * @user_addr: User-space pointer (represented as a 64-bit value) for data transfer.
 *
 * This structure is used to perform read and write operations on a previously allocated buffer.
 * It contains the necessary parameters to identify the buffer location and the data size,
 * as well as the user-space address for transferring data.
 */
struct eba_rw_data {
    __u64 buff_id;   
    __u64 off;     
    __u64 size;      
    __u64 user_addr; 
};

/**
 * struct eba_remote_alloc - Structure for remote buffer allocation via IOCTL.
 * @size:       Number of bytes to allocate on the remote node.
 * @life_time:  Lifetime for the remote buffer allocation in seconds.
 * @buffer_id:  Buffer id that will store the remotely allocated buffer.
 * @mac:        MAC address of the remote node.
 *
 * This structure is used to request a buffer allocation on a remote node.
 * The MAC address field identifies the target node. 
 * 
 * @note TODO: Modify this field to use a node ID instead of a MAC address.
 */
struct eba_remote_alloc{
    __u64 size;      
    __u64 life_time; 
    __u64 buffer_id;
    const char mac[6];
};

/**
 * struct eba_remote_write - Structure for remote buffer write operations via IOCTL.
 * @buff_id:  Remote buffer identifier.
 * @offset:   Offset within the remote buffer where the write operation should begin.
 * @size:     Number of bytes to write.
 * @payload:  Pointer to the data payload to be written.
 * @mac:      MAC address of the remote node.
 *
 * This structure is used to send a write request to a remote node by specifying the target
 * buffer, the offset within that buffer, and the data to be written.
 * 
 * @note TODO: Modify the MAC address field to use a node ID instead.
 */
struct eba_remote_write{
    __u64 buff_id;      
    __u64 offset; 
    __u64 size;
    char* payload;
    const char mac[6];
};

/**
 * struct eba_remote_read - Structure for remote buffer read operations via IOCTL.
 * @dst_buffer_id: Identifier for the destination (local) buffer where data will be stored.
 * @src_buffer_id: Identifier for the source (remote) buffer from which data is to be read.
 * @dst_offset:    Offset within the destination buffer where the data should be written.
 * @src_offset:    Offset within the source buffer where the reading should begin.
 * @size:          Number of bytes to read.
 * @mac:           MAC address of the remote node.
 *
 * This structure is used to request a read operation from a remote node. It specifies
 * both the source (remote) buffer and the destination (local) buffer along with the respective
 * offsets and the size of data to be transferred.
 *
 * @note TODO: Modify the MAC address field to use a node ID instead.
 */
struct eba_remote_read {
    __u64 dst_buffer_id;
    __u64 src_buffer_id;
    __u64 dst_offset;
    __u64 src_offset;
    __u64 size;
    const char mac[6];
};


#endif /* _EBA_H */
