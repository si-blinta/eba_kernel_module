/*
 * ---------------------------------------------------------------------------
 *  EBA Echo-Client
 * --------------------------------------------------------------------------
 *  1.Prompt for the server’s 8-byte connection buffer ID (the “port”).
 *  2.Allocate an 8-byte handshake buffer, then publish its ID to the server.
 *  3.Wait until the server writes back the 512-byte echo buffer ID into our handshake buffer.
 *  4.Enter the loop:
 *       – read one line,
 *       – send that line (zero-padded to 512 bytes) to the echo buffer
 *       – if the line starts with “exit”, break and quit.
 * ---------------------------------------------------------------------------
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>  
#include "eba_user.h" 

int main(void)
{

    /* 1.Ask for the server’s 8-byte connection buffer ID.*/

    uint64_t server_cn_buf_id = 0;
    printf("Enter server connection buffer id: ");
    if (scanf("%lu", &server_cn_buf_id) != 1 || server_cn_buf_id == 0)
    {
        fprintf(stderr, "Invalid buffer id\n");
        return EXIT_FAILURE;
    }
    /* consume the newline still in stdin after scanf() */
    getchar();

    /* 2.Allocate our 8-byte “handshake” buffer. The server will later write the echo buffer ID here.*/

    uint64_t client_buf_id = eba_alloc(8, 0, 0);
    if (client_buf_id == 0)
    {
        fprintf(stderr, "eba_alloc() failed for client buffer\n");
        return EXIT_FAILURE;
    }
    printf("client handshake buffer = %lu\n", client_buf_id);

    /* Publish our buffer ID to the server’s connection buffer */
    int ret = eba_remote_write(server_cn_buf_id, 0, 8, (const char *)&client_buf_id, 0, 5000);
    if (ret < 0)
    {
        fprintf(stderr, "error writing to server buffer (rc=%d)\n", ret);
        return EXIT_FAILURE;
    }


    /*3. Wait for the server to drop the 512-byte echo buffer ID.*/   
    uint64_t echo_buf_id = 0;
    while (echo_buf_id == 0)
    {
        if (eba_read(&echo_buf_id, client_buf_id, 0, 8) < 0)
        {
            fprintf(stderr, "eba_read() failed while waiting for echo buffer\n");
            return EXIT_FAILURE;
        }
        sleep(1);
    }
    printf("received echo buffer id = %lu\n", echo_buf_id);

    /* 4. Read a line from stdin and send it.*/
    for (;;) {
    
        char msg[512] = {0};
        printf("Type a message to echo and press <Enter>:\n");
        if (fgets(msg, sizeof(msg), stdin) == NULL)
        {
            fprintf(stderr, "failed to read message from stdin\n");
            return EXIT_FAILURE;
        }
        /* strip trailing newline for cleaner server output */
        size_t len = strlen(msg);
        if (len && msg[len - 1] == '\n')
            msg[len - 1] = '\0';
    
        ret = eba_remote_write(echo_buf_id, 0, sizeof(msg), msg, 0, 5000);
        if (ret < 0)
        {
            fprintf(stderr, "error writing echo message (rc=%d)\n", ret);
            return EXIT_FAILURE;
        }
    
        printf("message sent check the server console for the echo!\n");
        if (strncmp((char *)msg,"exit",4) == 0) {
            printf("bye!\n");
            break;
        }
    }
    
    return EXIT_SUCCESS;
}

