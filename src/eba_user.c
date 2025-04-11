/*
 * eba_user.c - User-space API for EBA Kernel Module IOCTL Interface
 *
 * This file implements simple wrapper functions (eba_alloc, eba_write, eba_read)
 * so that user applications do not have to build IOCTL request structures manually.
 */

 #include "eba_user.h"
 #include "eba.h"         /* Public IOCTL definitions and structures for the kernel module */
 #include <fcntl.h>       /* open() */
 #include <stdio.h>       /* printf(), perror() */
 #include <stdlib.h>      /* exit() */
 #include <stdint.h>
 #include <sys/ioctl.h>   /* ioctl() */
 #include <unistd.h>      /* close() */
 #include <string.h>      /* memset() */
 
 /*
  * Internal helper: open the device and return its file descriptor.
  * Returns a valid file descriptor on success or -1 on error.
  */
 static int open_eba_device(void)
 {
     int fd = open("/dev/eba", O_RDWR);
     if (fd < 0) {
         perror("open(/dev/eba)");
     }
     return fd;
 }
 
 /*
  * eba_alloc - Allocates a buffer from the EBA kernel module.
  *
  * This function wraps the IOCTL_ALLOC command.
  */
 uint64_t eba_alloc(uint64_t size, uint64_t life_time, uint8_t type)
 {
     int fd, ret;
     struct eba_alloc_data alloc;
 
     /* For now, we ignore the 'type' parameter if not used by the kernel module */
     alloc.size = size;
     alloc.life_time = life_time;
     alloc.buff_id = 0;
 
     fd = open_eba_device();
     if (fd < 0)
         return 0;
 
     ret = ioctl(fd, EBA_IOCTL_ALLOC, &alloc);
     close(fd);
 
     if (ret < 0) {
         perror("ioctl(EBA_IOCTL_ALLOC)");
         return 0;
     }
 
     return alloc.buff_id;
 }
 
 /*
  * eba_write - Writes data to a locally allocated buffer.
  *
  * This function wraps the IOCTL_WRITE command.
  */
 int eba_write(const void *data, uint64_t buf_id, uint64_t off, uint64_t size)
 {
     int fd, ret;
     struct eba_rw_data rw;
 
     rw.buff_id = buf_id;
     rw.off = off;
     rw.size = size;
     rw.user_addr = (uint64_t)data;
 
     fd = open_eba_device();
     if (fd < 0)
         return 1;
 
     ret = ioctl(fd, EBA_IOCTL_WRITE, &rw);
     close(fd);
 
     if (ret < 0) {
         perror("ioctl(EBA_IOCTL_WRITE)");
         return 1;
     }
     return 0;
 }
 
 /*
  * eba_read - Reads data from a locally allocated buffer.
  *
  * This function wraps the IOCTL_READ command.
  */
 int eba_read(void *data_out, uint64_t buf_id, uint64_t off, uint64_t size)
 {
     int fd, ret;
     struct eba_rw_data rw;
 
     rw.buff_id = buf_id;
     rw.off = off;
     rw.size = size;
     rw.user_addr = (uint64_t)data_out;
 
     fd = open_eba_device();
     if (fd < 0)
         return 1;
 
     ret = ioctl(fd, EBA_IOCTL_READ, &rw);
     close(fd);
 
     if (ret < 0) {
         perror("ioctl(EBA_IOCTL_READ)");
         return 1;
     }
     return 0;
 }
 