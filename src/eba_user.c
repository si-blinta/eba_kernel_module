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
 
 uint64_t eba_alloc(uint64_t size, uint64_t life_time, uint8_t type)
 {
     int fd, ret;
     struct eba_alloc_data alloc;
 
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
 

int eba_remote_alloc(uint64_t size, uint64_t life_time, uint64_t local_buff_id,const char mac[6]/* TODO modify it to be come node*/)
{
    int fd, ret;
    struct eba_remote_alloc remote;

    /* Fill in the remote_alloc structure */
    memset(&remote, 0, sizeof(remote));
    remote.size = size;
    remote.life_time = life_time;
    remote.buffer_id = local_buff_id;
    memcpy((void *)remote.mac, mac, 6);

    fd = open("/dev/eba", O_RDWR);
    if (fd < 0) {
        perror("open(/dev/eba)");
        return 1;
    }

    ret = ioctl(fd, EBA_IOCTL_REMOTE_ALLOC, &remote);
    close(fd);

    if (ret < 0) {
        perror("ioctl(EBA_IOCTL_REMOTE_ALLOC)");
        return 1;
    }
    return 0;
}

int eba_remote_write(uint64_t buff_id, uint64_t offset, uint64_t size,const char* payload ,const char mac[6]/* TODO modify it to be come node*/)
{
    int fd, ret;
    struct eba_remote_write remote;

    /* Fill in the remote_alloc structure */
    remote.buff_id = buff_id;
    remote.offset = offset;
    remote.size = size;
    remote.payload = malloc(remote.size);
    if(remote.payload == 0)
    {
        perror("malloc failed");
        return 1;
    }
    memcpy((void *)remote.payload, payload, remote.size);
    memcpy((void *)remote.mac, mac, 6);

    fd = open("/dev/eba", O_RDWR);
    if (fd < 0) {
        free(remote.payload);
        perror("open(/dev/eba)");
        return 1;
    }

    ret = ioctl(fd, EBA_IOCTL_REMOTE_WRITE, &remote);
    close(fd);

    if (ret < 0) {
        free(remote.payload);
        perror("ioctl(EBA_IOCTL_REMOTE_WRITE)");
        return 1;
    }
    free(remote.payload);
    return 0;
}

int eba_remote_read(uint64_t dst_buffer_id, uint64_t src_buffer_id, uint64_t dst_offset,uint64_t src_offset ,uint64_t size,const char mac[6]/* TODO modify it to be come node*/)
{
    int fd, ret;
    struct eba_remote_read remote;

    /* Fill in the remote_alloc structure */
    remote.dst_buffer_id = dst_buffer_id;
    remote.src_buffer_id= src_buffer_id;
    remote.dst_offset = dst_offset;
    remote.src_offset = src_offset;
    remote.size = size;
    memcpy((void *)remote.mac, mac, 6);

    fd = open("/dev/eba", O_RDWR);
    if (fd < 0) {
        perror("open(/dev/eba)");
        return 1;
    }

    ret = ioctl(fd, EBA_IOCTL_REMOTE_READ, &remote);
    close(fd);

    if (ret < 0) {
        perror("ioctl(EBA_IOCTL_REMOTE_READ)");
        return 1;
    }
    return 0;
}