#include "eba_utils.h"

extern struct node_info node_infos[MAX_NODE_COUNT];
extern int nodes_count;

int eba_utils_file_to_buf(const char *filepath, uint64_t buff_id)
{
    struct file *filp;
    loff_t pos = 0;
    loff_t file_size;
    char *kbuf = NULL;
    ssize_t bytes_read;
    int ret = 1; /* default to error */

    /* Open the file read-only */
    filp = filp_open(filepath, O_RDONLY, 0);
    if (IS_ERR(filp))
    {
        EBA_ERR("Failed to open file %s, err=%ld\n", filepath, PTR_ERR(filp));
        return 1;
    }

    /* Get file size via i_size_read() */
    file_size = i_size_read(file_inode(filp));
    if (file_size <= 0)
    {
        EBA_ERR("File %s is empty or error reading size\n", filepath);
        goto out_close;
    }

    /* Allocate a kernel buffer to read the file contents into */
    kbuf = kmalloc(file_size, GFP_KERNEL);
    if (!kbuf)
    {
        EBA_ERR("Failed to allocate temp buffer\n");
        goto out_close;
    }

    /* Read the entire file into kbuf */
    bytes_read = kernel_read(filp, kbuf, file_size, &pos);
    if (bytes_read < 0 || bytes_read != file_size)
    {
        EBA_ERR("Failed to read entire file %s, bytes_read=%zd\n",
                filepath, bytes_read);
        goto out_free;
    }

    /* Write that data into the EBA buffer buff_id at offset=0 */
    if (eba_internals_write(kbuf, buff_id, 0, file_size) < 0)
    {
        EBA_ERR("eba_internals_write() failed for file %s\n", filepath);
        goto out_free;
    }

    EBA_INFO("Read %lld bytes from file %s into EBA buffer %llu\n",
             file_size, filepath, buff_id);
    ret = 0; /* success */

out_free:
    kfree(kbuf);
out_close:
    filp_close(filp, NULL);
    return ret;
}

int eba_utils_buf_to_file(uint64_t buff_id, uint64_t size, const char *filepath)
{
    struct file *filp;
    loff_t pos = 0;
    char *kbuf = NULL;
    ssize_t bytes_written;
    int ret;

    if (size == 0)
    {
        EBA_ERR("eba_utils_buf_to_file: zero size, nothing to write\n");
        return -EINVAL;
    }

    /* Allocate a kernel buffer of 'size' to read from EBA */
    kbuf = kmalloc(size, GFP_KERNEL);
    if (!kbuf)
        return -ENOMEM;

    /* Read from EBA buffer into kbuf */
    ret = eba_internals_read(kbuf, buff_id, 0, size);
    if (ret < 0)
    {
        EBA_ERR("eba_internals_read() failed on buff_id %llu\n", buff_id);
        goto out_free;
    }

    /* Open or create the file (truncate if exists) */
    filp = filp_open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (IS_ERR(filp))
    {
        EBA_ERR("Failed to open file %s for writing, err=%ld\n",
                filepath, PTR_ERR(filp));
        ret = PTR_ERR(filp);
        filp = NULL;
        goto out_free;
    }

    /* Write kbuf to file */
    bytes_written = kernel_write(filp, kbuf, size, &pos);
    if (bytes_written < 0 || bytes_written != size)
    {
        EBA_ERR("Failed to write entire buffer to file %s, bytes_written=%zd\n",
                filepath, bytes_written);
        if (bytes_written >= 0)
            ret = -EIO;
        else
            ret = bytes_written;
        goto out_close;
    }

    EBA_INFO("Wrote %llu bytes from EBA buffer %llu to file %s\n",
             size, buff_id, filepath);
    ret = 0; /* success */
    print_hex_dump(KERN_INFO, "EBA BUF DUMP: ", DUMP_PREFIX_OFFSET, 16, 1, kbuf, size, true);
out_close:
    if (filp)
        filp_close(filp, NULL);
out_free:
    kfree(kbuf);
    return ret;
}

int eba_export_node_specs(void)
{
    char filepath[256];
    EBA_INFO("Exporting node_specs buffers to files...\n");

    for (int i = 0; i < MAX_NODE_COUNT; i++)
    {
        if (node_infos[i].id != -1 && node_infos[i].node_specs != 0)
        {
            /* Build the path like "/var/lib/eba/node_2_specs" */
            snprintf(filepath, sizeof(filepath), "/var/lib/eba/node_%hu_specs", node_infos[i].id);
            /* Dump the entire buffer to that file */
            if (eba_utils_buf_to_file(node_infos[i].node_specs, EBP_NODE_SPECS_MAX_SIZE, filepath) < 0)
            {
                EBA_ERR("eba_export_node_specs: eba_utils_buf_to_file failed, iteration : %d\n", i);
                return -1;
            }
        }
    }
    return 0;
}

int test_eba_utils_file_to_buf(void)
{
    void *buff_id;
    int ret;
    char tmpbuf[64]; /* small stack buffer to read/print a piece of data */

    EBA_INFO("=== test_eba_utils_file_to_buf: START ===\n");

    /* 1. Allocate a buffer of 512 bytes with no expiration (lifetime=0). */
    buff_id = eba_internals_malloc(512, 0);
    if (!buff_id)
    {
        EBA_ERR("Failed to allocate EBA buffer in test_eba_utils_file_to_buf\n");
        return 1;
    }

    /* 2. Read from /tmp/test_input into this buffer.
     *    Make sure /tmp/test_input exists and has <=512 bytes.
     */
    ret = eba_utils_file_to_buf("/tmp/test_input", (uint64_t)buff_id);
    if (ret != 0)
    {
        EBA_ERR("eba_utils_file_to_buf failed (ret=%d)\n", ret);
        eba_internals_free(buff_id);
        return 1;
    }

    /* 3. Read back a small portion of data from the buffer and print it */
    memset(tmpbuf, 0, sizeof(tmpbuf));
    if (eba_internals_read(tmpbuf, (uint64_t)buff_id, 0, sizeof(tmpbuf) - 1) == 0)
    {
        EBA_INFO("First 63 bytes from EBA buffer: \"%s\"\n", tmpbuf);
    }
    else
    {
        EBA_ERR("Failed to read back from EBA buffer\n");
    }

    /* 4. Free the EBA buffer */
    eba_internals_free(buff_id);

    EBA_INFO("=== test_eba_utils_file_to_buf: END ===\n");
    return 0; /* success */
}

int test_eba_utils_buf_to_file(void)
{
    void *buff_id;
    char pattern[512];
    int i, ret;

    EBA_INFO("=== test_eba_utils_buf_to_file: START ===\n");

    /* 1. Allocate a 512-byte buffer */
    buff_id = eba_internals_malloc(512, 0);
    if (!buff_id)
    {
        EBA_ERR("Failed to allocate EBA buffer in test_eba_utils_buf_to_file\n");
        return 1;
    }

    /* 2. Fill local array 'pattern' with some data to demonstrate writing */
    for (i = 0; i < (int)sizeof(pattern); i++)
    {
        pattern[i] = (char)('A' + (i % 26)); /* 'A'..'Z' repeated */
    }

    /* 3. Write this pattern to the EBA buffer */
    ret = eba_internals_write(pattern, (uint64_t)buff_id, 0, sizeof(pattern));
    if (ret < 0)
    {
        EBA_ERR("Failed to write pattern to EBA buffer\n");
        eba_internals_free(buff_id);
        return 1;
    }

    /* 4. Now write the EBA buffer to a file: /tmp/test_output */
    ret = eba_utils_buf_to_file((uint64_t)buff_id, sizeof(pattern), "/tmp/test_output");
    if (ret < 0)
    {
        EBA_ERR("eba_utils_buf_to_file failed, ret=%d\n", ret);
        eba_internals_free(buff_id);
        return 1;
    }

    EBA_INFO("Check /tmp/test_output to see if the 512-byte pattern is there.\n");

    /* 5. Free the buffer */
    eba_internals_free(buff_id);

    EBA_INFO("=== test_eba_utils_buf_to_file: END ===\n");
    return 0; /* success */
}

int test_eba_export_node_specs(void)
{
    int node_index = 0; /* We'll pick node 0 for the test */
    void *buff_id;
    char test_data[] = "Hello from node_0_specs!\n";
    int ret;

    EBA_INFO("=== test_eba_export_node_specs: START ===\n");

    /* 1. Mark node_infos[node_index] as valid */
    node_infos[node_index].id = node_index;

    /* 2. Allocate a buffer to hold the specs data */
    buff_id = eba_internals_malloc(1024, 100); /* 1KB for demonstration */
    if (!buff_id)
    {
        EBA_ERR("Failed to allocate node_specs buffer for node %d\n", node_index);
        return 1;
    }

    /* 3. Write some data into that buffer */
    ret = eba_internals_write(test_data, (uint64_t)buff_id, 0, strlen(test_data));
    if (ret < 0)
    {
        EBA_ERR("Failed to write test data into node_specs buffer\n");
        eba_internals_free(buff_id);
        return 1;
    }

    /* 4. Store this buffer in node_infos[node_index].node_specs */
    node_infos[node_index].node_specs = (uint64_t)buff_id;

    /*
     * 5. Call eba_export_node_specs()
     *    This should write the entire buffer content to /var/lib/eba/node_0_specs.
     */
    ret = eba_export_node_specs();
    if (ret != 0)
    {
        EBA_ERR("eba_export_node_specs returned error %d\n", ret);
        return 1;
    }

    EBA_INFO("Check /var/lib/eba/node_0_specs to see if it contains: \"%s\"\n", test_data);
    EBA_INFO("=== test_eba_export_node_specs: END ===\n");
    return 0; /* success */
}
