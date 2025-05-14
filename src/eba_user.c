#include "eba_user.h"
#include <errno.h>

/*
 * open_eba_device() — open /dev/eba and return fd
 *
 * Returns:
 *   ≥0 : valid file descriptor
 *   -1 : open() failed (errno printed)
 */
static int open_eba_device(void)
{
    /* O_RDWR so we can both read and write via IOCTLs */
    int fd = open("/dev/eba", O_RDWR);
    if (fd < 0)
    {
        perror("open(/dev/eba)");
    }
    return fd;
}

uint64_t eba_alloc(uint64_t size, uint64_t life_time, uint8_t type)
{
    int fd, ret;
    struct eba_alloc_data alloc;

    /* Zero out structure, then fill fields */
    memset(&alloc, 0, sizeof(alloc));
    alloc.size = size;
    alloc.life_time = life_time;
    alloc.buff_id = 0; /* kernel will fill this on success */

    /* Open the device char */
    fd = open_eba_device();
    if (fd < 0)
        return 0;

    /* Issue the IOCTL; fills alloc.buff_id */
    ret = ioctl(fd, EBA_IOCTL_ALLOC, &alloc);

    /* Close regardless of success or failure */
    close(fd);

    if (ret < 0)
    {
        perror("ioctl(EBA_IOCTL_ALLOC)");
        return 0;
    }

    return alloc.buff_id;
}

int eba_write(const void *data, uint64_t buf_id, uint64_t off, uint64_t size)
{
    struct eba_write wr;
    /* Zero out structure, then fill fields */
    memset(&wr, 0, sizeof(wr));
    wr.buff_id = buf_id;
    wr.offset = off;
    wr.size = size;
    wr.payload = (char *)data;

    int fd = open_eba_device();
    if (fd < 0)
        return 1;
    if (ioctl(fd, EBA_IOCTL_WRITE, &wr) < 0)
    {
        perror("ioctl(EBA_IOCTL_WRITE)");
        close(fd);
        return 1;
    }
    close(fd);
    return 0;
}

int eba_read(void *data_out, uint64_t buf_id, uint64_t off, uint64_t size)
{
    struct eba_read rd;
    /* Zero out structure, then fill fields */
    memset(&rd, 0, sizeof(rd));
    rd.buffer_id = buf_id;
    rd.offset = off;
    rd.size = size;
    rd.user_addr = (uint64_t)data_out;  /* kernel will copy into here */

    int fd = open_eba_device();
    if (fd < 0)
        return 1;
    if (ioctl(fd, EBA_IOCTL_READ, &rd) < 0)
    {
        perror("ioctl(EBA_IOCTL_READ)");
        close(fd);
        return 1;
    }
    close(fd);

    return 0;
}


int eba_remote_alloc(uint64_t size, uint64_t life_time,
                     uint64_t local_buff_id, uint16_t node_id)
{
    int fd, ret;
    struct eba_remote_alloc remote;

    /* Zero out structure, then fill fields */
    memset(&remote, 0, sizeof(remote));
    remote.size = size;
    remote.life_time = life_time;
    remote.buffer_id = local_buff_id;
    remote.node_id = node_id;

    fd = open_eba_device();
    if (fd < 0)
        return 0;
    ret = ioctl(fd, EBA_IOCTL_REMOTE_ALLOC, &remote);
    close(fd);

    if (ret < 0)
    {
        perror("ioctl(EBA_IOCTL_REMOTE_ALLOC)");
        return 0;
    }
    return remote.iid;
}

int eba_remote_write(uint64_t buff_id, uint64_t offset, uint64_t size,
                     const char *payload, uint16_t node_id)
{
    int fd, ret;
    struct eba_remote_write remote;

    /* Zero out structure, then fill fields */
    memset(&remote, 0, sizeof(remote));
    remote.buff_id = buff_id;
    remote.offset = offset;
    remote.size = size;
    remote.payload = (char*) payload;
    remote.node_id = node_id;
    /* copy the payload into the struct and the mac address */
    memcpy((void *)remote.payload, payload, remote.size);

    fd = open_eba_device();
    if (fd < 0)
        return 0;
    ret = ioctl(fd, EBA_IOCTL_REMOTE_WRITE, &remote);
    close(fd);

    if (ret < 0)
    {
        perror("ioctl(EBA_IOCTL_REMOTE_WRITE)");
        return 0;
    }
    return remote.iid;
}

int eba_remote_read(uint64_t dst_buffer_id, uint64_t src_buffer_id, uint64_t dst_offset, uint64_t src_offset, uint64_t size,uint16_t node_id )
{
    int fd, ret;
    struct eba_remote_read remote;

    /* Zero out structure, then fill fields */
    memset(&remote, 0, sizeof(remote));
    remote.dst_buffer_id = dst_buffer_id;
    remote.src_buffer_id = src_buffer_id;
    remote.dst_offset = dst_offset;
    remote.src_offset = src_offset;
    remote.size = size;
    remote.node_id = node_id;

    fd = open_eba_device(); 
    if (fd < 0)
        return 0;
    ret = ioctl(fd, EBA_IOCTL_REMOTE_READ, &remote);
    close(fd);

    if (ret < 0)
    {
        perror("ioctl(EBA_IOCTL_REMOTE_READ)");
        return 0;
    }
    return remote.iid;
}

int eba_discover(void)
{
    int fd, ret;

    fd = open_eba_device();
    if (fd < 0)
        return 1;
    /* No extra data needed for discover command */
    ret = ioctl(fd, EBA_IOCTL_DISCOVER, NULL);
    close(fd);

    if (ret < 0)
    {
        perror("ioctl(EBA_IOCTL_DISCOVER)");
        return ret;
    }

    return 0;
}

int eba_export_node_specs(void)
{
    int fd, ret;

    fd = open_eba_device();
    if (fd < 0)
        return -1;

    ret = ioctl(fd, EBA_IOCTL_EXPORT_NODE_SPECS, NULL);
    close(fd);

    if (ret < 0)
        perror("ioctl(EBA_IOCTL_EXPORT_NODE_SPECS)");

    return ret;
}

int eba_get_node_infos(struct eba_node_info *out)
{
    int fd, ret;
    fd = open_eba_device();
    if (fd < 0)
        return -1;
        
    /* Do a single IOCTL, kernel will copy back N entries into our buffer */
    ret = ioctl(fd, EBA_IOCTL_GET_NODE_INFOS, out);
    if ( ret < 0) {
        perror("ioctl(EBA_IOCTL_GET_NODE_INFOS)");
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

int eba_wait_iid(uint32_t iid, uint8_t status, uint32_t timeout_ms)
{
        struct eba_wait_iid wi = {
                .iid           = iid,
                .wanted_status = status,
                .timeout_ms    = timeout_ms,
                .rc            = -EINTR,
                .timed_out     = 0
        };
        int fd = open_eba_device();
        if (fd < 0)
                return -1;

        if (ioctl(fd, EBA_IOCTL_WAIT_IID, &wi) < 0) {
                perror("ioctl(EBA_IOCTL_WAIT_IID)");
                close(fd);
                return -1;
        }
        close(fd);

        if (wi.timed_out)      
            return 1;               /* timed out */
        return wi.rc;          /* 0 on success, negative on error     */
}
int eba_wait_buffer(uint64_t buffer_id, uint32_t timeout_ms)
{   
    struct eba_wait_buffer wb = {
        .buff_id = buffer_id,
        .timeout_ms = timeout_ms,
        .rc = -EINTR,
        .timed_out = 0
    };
    int fd = open_eba_device();
    if (fd < 0)
        return -1;

    if (ioctl(fd, EBA_IOCTL_WAIT_BUFFER, &wb) < 0) {
        perror("ioctl(EBA_IOCTL_WAIT_BUFFER)");
        close(fd);
        return -1;
    }
    close(fd);

    if (wb.timed_out)
        return 1;               /* timed out */
    return wb.rc;          /* 0 on success, negative on error     */
}

int eba_register_service(uint64_t buff_id, uint64_t new_id)
{
    int fd, ret;
    struct eba_register_service reg;

    /* Zero out structure, then fill fields */
    memset(&reg, 0, sizeof(reg));
    reg.buff_id = buff_id;
    reg.new_id = new_id;

    fd = open_eba_device();
    if (fd < 0)
        return -1;
    ret = ioctl(fd, EBA_IOCTL_REGISTER_SERVICE, &reg);
    close(fd);

    if (ret < 0)
    {
        perror("ioctl(EBA_IOCTL_REGISTER_SERVICE)");
        return -1;
    }
    return 0 ;
}

int eba_register_queue(uint64_t buff_id)
{
    int fd, ret;
    struct eba_register_queue rq;

    /* Zero out structure, then fill fields */
    memset(&rq, 0, sizeof(rq));
    rq.buff_id = buff_id;

    fd = open_eba_device();
    if (fd < 0)
        return -1;
    ret = ioctl(fd, EBA_IOCTL_REGISTER_QUEUE, &rq);
    close(fd);

    if (ret < 0)
    {
        perror("ioctl(EBA_IOCTL_REGISTER_QUEUE)");
        return -1;
    }
    return 0 ;
}

int eba_enqueue(uint64_t buff_id, void *data, uint64_t size)
{
    int fd, ret;
    struct eba_enqueue enq;

    /* Zero out structure, then fill fields */
    memset(&enq, 0, sizeof(enq));
    enq.buff_id = buff_id;
    enq.data = (uint64_t)data;
    enq.size = size;

    fd = open_eba_device();
    if (fd < 0)
        return -1;
    ret = ioctl(fd, EBA_IOCTL_ENQUEUE, &enq);
    close(fd);

    if (ret < 0)
    {
        perror("ioctl(EBA_IOCTL_ENQUEUE)");
        return -1;
    }
    return 0 ;
}
int eba_dequeue(uint64_t buff_id, void *data_out, uint64_t size)
{
    int fd, ret;
    struct eba_dequeue deq;

    /* Zero out structure, then fill fields */
    memset(&deq, 0, sizeof(deq));
    deq.buff_id = buff_id;
    deq.data = (uint64_t)data_out;
    deq.size = size;

    fd = open_eba_device();
    if (fd < 0)
        return -1;
    ret = ioctl(fd, EBA_IOCTL_DEQUEUE, &deq);
    close(fd);

    if (ret < 0)
    {
        perror("ioctl(EBA_IOCTL_DEQUEUE)");
        return -1;
    }
    return 0 ;
}