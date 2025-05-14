/*------------------------------------------------------------------------------
 *  EBA remote shell Client
 *------------------------------------------------------------------------------
 *  Workflow
 *    1. Prompt the user for the server’s 8‑byte connection buffer ID (the port).
 *    2. Allocate an 8‑byte handshake buffer and publish its ID to the server
 *       (offset 0). The server will later store its cmd buffer ID here.
 *    3. Allocate a client output buffer (OUTPUT_SIZE) and send its ID to the
 *       server (offset 8) so we can receive command output.
 *    4. Wait for the server to write back the cmd buffer ID into the handshake
 *       buffer.
 *    5. REPL loop
 *         – read one line from stdin
 *         – send the zero‑padded line to the cmd buffer
 *         – print the server’s response from the output buffer
 *         – break on "exit"
 *------------------------------------------------------------------------------
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
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

int main(void)
{

    /* Get server connection buffer ID*/
    uint64_t server_conn_id = EBA_SERVICE_REMOTE_SHELL;
    uint64_t client_cmd_id_holder = eba_alloc(8, 0, 0);
    if (client_cmd_id_holder == 0)
    {
        fprintf(stderr, "eba_alloc() failed for handshake buffer\n");
        return EXIT_FAILURE;
    }
    printf("client cmd buffer id holder = %lu (the server will send the id of CMD buffer here)\n", client_cmd_id_holder);

    /* Allocate output buffer*/
    uint64_t client_out_id = eba_alloc(OUTPUT_SIZE, 0, 0);
    if (client_out_id == 0)
    {
        fprintf(stderr, "eba_alloc() failed for output buffer\n");
        return EXIT_FAILURE;
    }
    printf("client output buffer id = %lu (The server will send the output of the commands executed here)\n", client_out_id);

    /*publish both ids*/
    //combine the two ids into a single buffer
    char buf[16] = {0};
    memcpy(buf, &client_cmd_id_holder, 8);
    memcpy(buf + 8, &client_out_id, 8);
    //send the buffer to the server
    int iid = eba_remote_write(server_conn_id, 0, sizeof(buf), (const char *)buf, 0);
    if (iid == 0)
    {
        fprintf(stderr, "eba_remote_write (handshake) returned 0\n");
        return EXIT_FAILURE;
    }
    /* Wait for server to write its cmd buffer ID */
    uint64_t server_cmd_buf_id = 0;
    eba_wait_buffer(client_cmd_id_holder, 0);
     if (eba_read(&server_cmd_buf_id, client_cmd_id_holder, 0, 8) != 0)
        {
            fprintf(stderr, "eba_read() failed while waiting for cmd buffer id\n");
            return EXIT_FAILURE;
        }
    printf("received server cmd buffer id = %lu\n", server_cmd_buf_id);

    for (;;)
    {
        char line[CMD_SIZE] = {0};
        printf("$ ");
        if (fgets(line, sizeof(line), stdin) == NULL)
        {
            fprintf(stderr, "failed to read from stdin\n");
            return EXIT_FAILURE;
        }

        /* remove trailing newline, if any */
        size_t len = strlen(line);
        if (len && line[len - 1] == '\n')
            line[len - 1] = '\0';

        /* Send the line to the cmd buffer */
        iid = eba_remote_write(server_cmd_buf_id, 0, sizeof(line), line, 0);
        if (iid == 0)
        {
            fprintf(stderr, "eba_remote_write (cmd) returned 0\n");
            return EXIT_FAILURE;
        }
        eba_wait_buffer(client_out_id,0);
        /* Exit shortcut */
        if (strncmp(line, "exit", 4) == 0)
        {
            puts("bye!");
            break;
        }
        /* Read and print captured output */
        char cmd_output[OUTPUT_SIZE] = {0};
        if (eba_read(cmd_output, client_out_id, 0, sizeof(cmd_output)) != 0)
        {
            fprintf(stderr, "eba_read() failed while reading command output\n");
            return EXIT_FAILURE;
        }
        printf("%s", cmd_output);
    }

    return EXIT_SUCCESS;
}
