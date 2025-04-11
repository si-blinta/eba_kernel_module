#ifndef _EBA_H
#define _EBA_H

#include <linux/ioctl.h>
#include <linux/types.h>
#define EBA_IOC_MAGIC 'E'
#define EBA_CLEAN_BUFFER_CALLBACK_TIMER 60000// 1 min
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

/* IOCTL command definitions */
#define EBA_IOCTL_ALLOC _IOWR(EBA_IOC_MAGIC, 1, struct eba_alloc_data)
#define EBA_IOCTL_WRITE _IOW(EBA_IOC_MAGIC, 2, struct eba_rw_data)
#define EBA_IOCTL_READ  _IOR(EBA_IOC_MAGIC, 3, struct eba_rw_data)

#define EBA_IOC_MAXNR 3

#endif /* _EBA_H */
