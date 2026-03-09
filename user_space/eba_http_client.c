/*
 * eba_http_client.c - HTTP/1.0 client demo for the EBA TCP stack.
 *
 * Usage
 * -----
 *   sudo ./eba_http_client <server_node_id>
 *
 * <server_node_id> is the EBA node ID of the machine running eba_httpd.
 * Obtain it by running eba_discover (via the 'user' tool) and then reading
 * the node list (e.g. with EBA_IOCTL_GET_NODE_INFOS or the 'user' helper).
 * In a two-node cluster the server is almost always node 1.
 *
 * Example
 * -------
 *   # On the server machine:
 *   sudo ./eba_httpd
 *
 *   # On the client machine (after discovery):
 *   sudo ./eba_http_client 1
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "eba_tcp.h"

#define HTTP_SERVER_PORT  80
#define HTTP_CLIENT_PORT  12345   /* ephemeral source port */

/* Maximum response buffer size */
#define RESPONSE_BUF_SZ   (64 * 1024)

static void print_usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s <server_node_id>\n"
            "\n"
            "  server_node_id – EBA node ID of the HTTP server.\n"
            "                   In a 2-node cluster this is typically 1.\n"
            "\n"
            "Steps:\n"
            "  1. Load the EBA kernel module on both machines.\n"
            "  2. Run eba_discover on both machines (via the 'user' tool).\n"
            "  3. Start eba_httpd on the server.\n"
            "  4. Run this client with the server's node ID.\n",
            prog);
}

int main(int argc, char *argv[])
{
    uint16_t server_node;
    struct eba_tcp_conn *conn;
    const char *http_request;
    char *response;
    int rlen;

    if (argc < 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    server_node = (uint16_t)atoi(argv[1]);
    if (server_node == 0) {
        fprintf(stderr, "Error: server_node_id must be ≥ 1 "
                "(0 is the unregistered-node sentinel).\n");
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    printf("╔══════════════════════════════════════════╗\n");
    printf("║          EBA HTTP Client                 ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");

    printf("[http_client] Connecting to HTTP server: "
           "node=%u port=%u ...\n", server_node, HTTP_SERVER_PORT);

    conn = eba_tcp_connect(HTTP_CLIENT_PORT, HTTP_SERVER_PORT,
                           server_node, 10000);
    if (!conn) {
        fprintf(stderr,
                "[http_client] Connection failed!\n"
                "  – Is eba_httpd running on node %u?\n"
                "  – Did you run eba_discover on both machines?\n"
                "  – Is the EBA kernel module loaded?\n",
                server_node);
        return EXIT_FAILURE;
    }

    printf("[http_client] Connected! Sending HTTP GET request ...\n");

    http_request = "GET / HTTP/1.0\r\nHost: eba-server\r\n\r\n";
    if (eba_tcp_send(conn, http_request, strlen(http_request), 5000) < 0) {
        fprintf(stderr, "[http_client] Failed to send HTTP request\n");
        eba_tcp_close(conn);
        return EXIT_FAILURE;
    }

    printf("[http_client] Request sent. Waiting for response ...\n\n");

    response = malloc(RESPONSE_BUF_SZ);
    if (!response) {
        perror("malloc");
        eba_tcp_close(conn);
        return EXIT_FAILURE;
    }

    rlen = eba_tcp_recv(conn, response, RESPONSE_BUF_SZ - 1, 10000);
    if (rlen < 0) {
        fprintf(stderr, "[http_client] Failed to receive response\n");
        free(response);
        eba_tcp_close(conn);
        return EXIT_FAILURE;
    }

    response[rlen] = '\0';

    printf("─────────────────────────────────────────────\n");
    printf("[http_client] Response (%d bytes):\n", rlen);
    printf("─────────────────────────────────────────────\n");
    printf("%s", response);
    printf("─────────────────────────────────────────────\n\n");

    free(response);
    eba_tcp_close(conn);

    printf("[http_client] Done.\n");
    return EXIT_SUCCESS;
}
