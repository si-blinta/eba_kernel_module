/*
 * eba_httpd.c - Proof-of-concept HTTP/1.0 server running on top of the
 *               EBA TCP stack.
 *
 * Usage
 * -----
 *   1. Load the EBA kernel module on both machines.
 *   2. On both machines run the included 'user' tool once to trigger discovery:
 *        sudo ./user       (or whatever calls eba_discover())
 *   3. Start the server:
 *        sudo ./eba_httpd
 *   4. On the client machine run eba_http_client with the server's node ID:
 *        sudo ./eba_http_client 1
 *
 * The server serves a single static HTML page that demonstrates the EBA TCP
 * stack is functional.  It handles one connection at a time and loops
 * indefinitely.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "eba_tcp.h"

#define HTTP_PORT 80

/* ---- Static HTTP response --------------------------------------------- */

static const char HTTP_RESPONSE[] =
    "HTTP/1.0 200 OK\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Connection: close\r\n"
    "\r\n"
    "<!DOCTYPE html>\r\n"
    "<html>\r\n"
    "<head><title>EBA Web Server</title></head>\r\n"
    "<body>\r\n"
    "<h1>Hello from the EBA TCP Stack!</h1>\r\n"
    "<p>This page is served by a custom TCP-like protocol implemented "
    "entirely in user space on top of the "
    "<strong>Exposed Buffer Architecture (EBA)</strong> kernel module.</p>\r\n"
    "<h2>How it works</h2>\r\n"
    "<ul>\r\n"
    "  <li>EBA exposes raw memory buffers across Ethernet using custom "
    "      EtherType 0xEBA0 frames.</li>\r\n"
    "  <li>eba_tcp builds a TCP-like stream on top: 3-way handshake, "
    "      fragmentation, and graceful close.</li>\r\n"
    "  <li>Each endpoint owns a mailbox buffer; segments are written "
    "      atomically with eba_remote_write().</li>\r\n"
    "</ul>\r\n"
    "<p><em>Proof-of-concept – not for production use.</em></p>\r\n"
    "</body>\r\n"
    "</html>\r\n";

/* ----------------------------------------------------------------------- */

static void handle_connection(struct eba_tcp_conn *conn)
{
    char request[2048];
    int  rlen;
    int  ret;

    /* Receive the HTTP request */
    rlen = eba_tcp_recv(conn, request, sizeof(request) - 1, 5000);
    if (rlen < 0) {
        fprintf(stderr, "[httpd] recv error, closing connection\n");
        eba_tcp_close(conn);
        return;
    }
    request[rlen] = '\0';

    printf("[httpd] === request (%d bytes) ===\n%s\n"
           "[httpd] === end of request ===\n\n", rlen, request);

    /* Send the HTTP response */
    ret = eba_tcp_send(conn, HTTP_RESPONSE, strlen(HTTP_RESPONSE), 5000);
    if (ret < 0)
        fprintf(stderr, "[httpd] send response failed\n");
    else
        printf("[httpd] response sent (%zu bytes)\n", strlen(HTTP_RESPONSE));

    eba_tcp_close(conn);
}

int main(void)
{
    struct eba_tcp_socket *srv;

    printf("╔══════════════════════════════════════════╗\n");
    printf("║         EBA HTTP Server (port %d)        ║\n", HTTP_PORT);
    printf("╚══════════════════════════════════════════╝\n\n");

    printf("[httpd] NOTE: run eba_discover (via the 'user' tool) on both\n"
           "              machines before starting client/server.\n\n");

    srv = eba_tcp_create_server(HTTP_PORT);
    if (!srv) {
        fprintf(stderr, "[httpd] Failed to create server on port %d.\n"
                        "        Is the EBA kernel module loaded?\n", HTTP_PORT);
        return EXIT_FAILURE;
    }

    printf("[httpd] Ready – waiting for connections on port %d ...\n\n",
           HTTP_PORT);

    for (;;) {
        struct eba_tcp_conn *conn;

        conn = eba_tcp_accept(srv, 0); /* wait indefinitely */
        if (!conn) {
            fprintf(stderr, "[httpd] accept failed, retrying in 1 s ...\n");
            sleep(1);
            continue;
        }

        printf("[httpd] *** connection accepted ***\n");
        handle_connection(conn);
        printf("[httpd] connection closed – waiting for next request ...\n\n");
    }

    eba_tcp_destroy_server(srv);
    return EXIT_SUCCESS;
}
