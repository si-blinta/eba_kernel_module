#ifndef _EBA_H
#define _EBA_H

#include <linux/ioctl.h>
#include <linux/types.h>
#define EBA_IOC_MAGIC 'E'
#define EBA_CLEAN_BUFFER_CALLBACK_TIMER 60000// 1 min
#define EBA_INFO(fmt, ...)  pr_info("EBA: " fmt, ##__VA_ARGS__)
#define EBA_ERR(fmt, ...)   pr_err("EBA: " fmt, ##__VA_ARGS__)
#define EBA_WARN(fmt, ...)  pr_warn("EBA: " fmt, ##__VA_ARGS__)
#define EBA_DBG(fmt, ...)   pr_debug("EBA: " fmt, ##__VA_ARGS__)

/**
 * @brief Structure used for buffer allocation via IOCTL.
 *
 * The user provides the desired size and lifetime (in seconds) and receives
 * a buffer identifier (the allocated virtual address).
 */
struct eba_alloc_data {
    __u64 size;       /**< Number of bytes to allocate. */
    __u64 life_time;  /**< Lifetime of the allocation in seconds. */
    __u64 buff_id;    /**< Returned buffer identifier (virtual address). */
};

/**
 * @brief Structure used for read and write operations via IOCTL.
 *
 * The user provides the buffer identifier, offset in the allocated buffer,
 * the number of bytes to read/write, and a pointer (passed as a 64-bit value)
 * to the user buffer for data transfer.
 */
struct eba_rw_data {
    __u64 buff_id;   /**< Buffer identifier (virtual address returned by allocation). */
    __u64 off;       /**< Offset within the buffer where the operation begins. */
    __u64 size;      /**< Number of bytes to read/write. */
    __u64 user_addr; /**< User-space pointer for data transfer. */
};

struct eba_remote_alloc{
    __u64 size;      
    __u64 life_time; 
    __u64 buffer_id;
    const char mac[6];  //TODO node id 
};

struct eba_remote_write{
    __u64 buff_id;      
    __u64 offset; 
    __u64 size;
    char* payload;
    const char mac[6];  //TODO node id 
};

struct eba_remote_read{
    __u64 buff_id;      
    __u64 offset; 
    __u64 size;
    char* payload;
    const char mac[6];  //TODO node id 
};

/* IOCTL command definitions */
#define EBA_IOCTL_ALLOC _IOWR(EBA_IOC_MAGIC, 1, struct eba_alloc_data)
#define EBA_IOCTL_WRITE _IOW(EBA_IOC_MAGIC, 2, struct eba_rw_data)
#define EBA_IOCTL_READ  _IOR(EBA_IOC_MAGIC, 3, struct eba_rw_data)
#define EBA_IOCTL_REMOTE_ALLOC _IOWR(EBA_IOC_MAGIC, 4, struct eba_remote_alloc)
#define EBA_IOCTL_REMOTE_WRITE _IOWR(EBA_IOC_MAGIC, 5, struct eba_remote_write)

#define EBA_IOC_MAXNR 5

#endif /* _EBA_H */
