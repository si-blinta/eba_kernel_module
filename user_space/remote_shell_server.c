/*------------------------------------------------------------------------------
 *  Remote Shell Server
 *------------------------------------------------------------------------------
 *  Workflow
 *    1. Allocate a 16‑byte “connection information” buffer and print its ID.
 *    2. Wait until the client writes back two buffer IDs:
 *         – offset 0: where the server will later write the CMD buffer ID
 *         – offset 8: client’s output buffer ID
 *    3. Allocate the CMD_SIZE command buffer and write its ID to the client.
 *    4. Loop forever:
 *         – busy‑wait on byte 0 of CMD buffer
 *         – read command, execute via popen()
 *         – write stdout to the client’s output buffer
 *         – clear flag, exit on “exit”
 *------------------------------------------------------------------------------
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "eba_user.h"

#define CMD_SIZE 512
#define OUTPUT_SIZE 512

enum INVOKE_STATUS
{
    INVOKE_QUEUED = 0,
    INVOKE_COMPLETED,
    INVOKE_FAILED,
    INVOKE_DEFAULT
};

/* Execute a shell command and capture its stdout */
static void execute_cmd(const char *cmd, char out[OUTPUT_SIZE])
{    
    
    if (strncmp(cmd, "cd", 2) == 0)
    {
        const char *path = cmd + 3;
        if (chdir(path) != 0)
        {
            perror("chdir failed");
            exit(EXIT_FAILURE);
        }
        printf("cd command received: '%s'\n", cmd);
        sprintf(out,"changed directory\n");
        return;
    }

    FILE *fp = popen(cmd, "r");
    if (!fp)
    {
        perror("popen");
        return;
    }
    size_t pos = 0;
    char buf[256];
    while (fgets(buf, sizeof(buf), fp))
    {
        size_t l = strlen(buf);
        if (pos + l >= OUTPUT_SIZE - 1)
            break;
        memcpy(out + pos, buf, l);
        pos += l;
    }
    out[pos] = '\0';
    pclose(fp);
}

int main(void)
{
    uint64_t conn_buf_id = eba_alloc(16, 0, 0);
    if (conn_buf_id == 0)
    {
        fprintf(stderr, "eba_alloc() failed for connection buffer\n");
        return EXIT_FAILURE;
    }
    printf("server connection buffer id = %lu\n", conn_buf_id);

    /* Wait for client to publish handshake + output buffer IDs */
    uint64_t client_cmd_holder_id = 0; /* where we will store CMD buf ID */
    uint64_t client_output_id = 0; /* where we will write command output */

    while (client_cmd_holder_id == 0)
    {
        if (eba_read(&client_cmd_holder_id, conn_buf_id, 0, 8) != 0)
        {
            fprintf(stderr, "eba_read() failed while waiting for holder id\n");
            return EXIT_FAILURE;
        }
        if (client_cmd_holder_id == 0)
            usleep(100);
    }
    printf("client_cmd_holder_id = %lu (Client buffer id which will hold our CMD buffer)\n", client_cmd_holder_id);

    while (client_output_id == 0)
    {
        if (eba_read(&client_output_id, conn_buf_id, 8, 8) != 0)
        {
            fprintf(stderr, "eba_read() failed while waiting for output id\n");
            return EXIT_FAILURE;
        }
        if (client_output_id == 0)
            usleep(100);
    }
    printf("client_output_id = %lu (Client buffer id in which we will send the output of commands)\n", client_output_id);

    /* Allocate command buffer and send its ID to the client */
    uint64_t cmd_buf_id = eba_alloc(CMD_SIZE, 0, 0);
    if (cmd_buf_id == 0)
    {
        fprintf(stderr, "eba_alloc() failed for command buffer\n");
        return EXIT_FAILURE;
    }

    int iid = eba_remote_write(client_cmd_holder_id, 0, 8,(const char *)&cmd_buf_id, 0);
    if (iid == 0)
    {
        fprintf(stderr, "eba_remote_write returned 0\n");
        return EXIT_FAILURE;
    }
    int rc = eba_wait_iid(iid, INVOKE_COMPLETED, 5000);
    if (rc < 0)
    {
        fprintf(stderr, "eba_wait_iid failed (rc=%d)\n", rc);
        return EXIT_FAILURE;
    }
    printf("cmd_buf_id sent to client = %lu (Client sends his commands here)\n", cmd_buf_id);


    uint8_t flag = 0;
    uint8_t cmd[CMD_SIZE] = {0};

    for (;;)
    {
        /* Spin until client writes to cmd buf */
        while (flag == 0)
        {
            if (eba_read(&flag, cmd_buf_id, 0, 1) != 0)
            {
                fprintf(stderr, "eba_read() failed on flag byte\n");
                return EXIT_FAILURE;
            }
            if (flag == 0)
                usleep(100);
        }

        /* Read full command */
        if (eba_read(cmd, cmd_buf_id, 0, sizeof(cmd)) != 0)
        {
            fprintf(stderr, "eba_read() failed on command\n");
            return EXIT_FAILURE;
        }
        cmd[CMD_SIZE - 1] = '\0';

        /* Execute */
        char output[OUTPUT_SIZE] = {0};
        execute_cmd((char *)cmd, output);

        /* Send output to client */
        if (eba_remote_write(client_output_id, 0, sizeof(output), output, 0) == 0)
        {
            fprintf(stderr, "eba_remote_write failed (output)\n");
            return EXIT_FAILURE;
        }

        /* Exit? */
        if (strncmp((char *)cmd, "exit", 4) == 0)
        {
            puts("client requested exit goodbye!");
            break;
        }

        /* Reset flag byte for next round */
        flag = 0;
        if (eba_write(&flag, cmd_buf_id, 0, 1) != 0)
        {
            fprintf(stderr, "eba_write() failed while clearing flag\n");
            return EXIT_FAILURE;
        }
    }
}
// Todo handle cd 