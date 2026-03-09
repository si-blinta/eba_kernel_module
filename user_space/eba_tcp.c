/*
 * eba_tcp.c - Lightweight TCP-like protocol implemented in user space on top
 *             of the EBA kernel module.
 *
 * Transport mechanism
 * -------------------
 * Each endpoint owns a fixed-size *mailbox* EBA buffer.  A sender writes a
 * complete struct eba_tcp_seg into the peer's mailbox with eba_remote_write()
 * (one atomic EBP_OP_WRITE → one Ethernet frame ≤ 1500 bytes).
 * The receiver busy-polls the magic word at offset 0; when it sees
 * EBA_TCP_MAGIC the segment is ready.  After consuming it the receiver
 * zeroes the magic so the slot is free for the next segment.
 *
 * Node-ID resolution
 * ------------------
 * Every segment carries the sender's MAC address in src_mac[].
 * The receiver calls eba_get_node_infos() and searches for a matching MAC
 * to obtain the peer's EBA node ID.  If not found, node_id 0 (broadcast) is
 * used as a fallback – this is safe in a 2-node cluster.
 */

#include "eba_tcp.h"

#include <sys/socket.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <inttypes.h>
#include <errno.h>

/* --------------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------------- */

/** Return monotonic time in milliseconds. */
static uint64_t tcp_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

/**
 * get_local_mac - read the hardware address of EBA_TCP_IFNAME.
 * @mac: output buffer, must be at least 6 bytes.
 * Returns 0 on success, -1 on failure.
 */
static int get_local_mac(uint8_t mac[6])
{
    int sock;
    struct ifreq ifr;

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        return -1;

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, EBA_TCP_IFNAME, IFNAMSIZ - 1);

    if (ioctl(sock, SIOCGIFHWADDR, &ifr) < 0) {
        close(sock);
        return -1;
    }
    memcpy(mac, ifr.ifr_hwaddr.sa_data, 6);
    close(sock);
    return 0;
}

/**
 * get_node_id_by_mac - translate a 6-byte MAC to an EBA node ID.
 *
 * Calls eba_get_node_infos() and linearly searches the resulting table for a
 * MAC match.  Returns the matching node ID, or 0 if not found (0 means
 * "broadcast" in the EBA kernel module).
 */
static uint16_t get_node_id_by_mac(const uint8_t mac[6])
{
    struct eba_node_info nodes[MAX_NODE_COUNT];
    int i;

    memset(nodes, 0, sizeof(nodes));
    if (eba_get_node_infos(nodes) < 0)
        return 0;

    for (i = 0; i < MAX_NODE_COUNT; i++) {
        if (nodes[i].id != 0 && memcmp(nodes[i].mac, mac, 6) == 0)
            return nodes[i].id;
    }
    return 0;
}

/**
 * tcp_recv_seg - poll @rx_buf until a valid segment is available.
 * @rx_buf:     local EBA buffer ID to poll.
 * @seg:        output – populated on success.
 * @timeout_ms: maximum wait time.
 *
 * Reads the 4-byte magic word at offset 0.  When it equals EBA_TCP_MAGIC the
 * full segment is read, the magic is cleared (so the slot is free), and the
 * function returns 0.
 *
 * Returns 0 on success, -1 on timeout or read error.
 */
static int tcp_recv_seg(uint64_t rx_buf, struct eba_tcp_seg *seg,
                        uint32_t timeout_ms)
{
    uint64_t deadline;
    uint32_t magic;
    uint32_t zero = 0;

    if (timeout_ms == 0)
        timeout_ms = 10000;
    deadline = tcp_now_ms() + timeout_ms;

    while (1) {
        if (eba_read(&magic, rx_buf, 0, sizeof(magic)) != 0) {
            fprintf(stderr, "[eba_tcp] tcp_recv_seg: eba_read(magic) failed\n");
            return -1;
        }

        if (magic == EBA_TCP_MAGIC) {
            /* Full segment present – read it then clear the slot */
            if (eba_read(seg, rx_buf, 0, sizeof(*seg)) != 0) {
                fprintf(stderr, "[eba_tcp] tcp_recv_seg: eba_read(seg) failed\n");
                return -1;
            }
            /* Mark the mailbox empty */
            if (eba_write(&zero, rx_buf, 0, sizeof(zero)) != 0)
                fprintf(stderr, "[eba_tcp] tcp_recv_seg: eba_write(clear) failed\n");
            return 0;
        }

        if (tcp_now_ms() >= deadline)
            return -1; /* timeout */

        usleep(EBA_TCP_POLL_US);
    }
}

/**
 * tcp_send_seg - write @seg into the peer's mailbox.
 * @remote_rx_buf: peer's EBA RX mailbox buffer ID.
 * @seg:           segment to send (magic must already be set to EBA_TCP_MAGIC).
 * @node_id:       peer's EBA node ID; 0 uses Ethernet broadcast.
 * @timeout_ms:    eba_remote_write timeout.
 *
 * Returns 0 on success, -1 on failure.
 */
static int tcp_send_seg(uint64_t remote_rx_buf, struct eba_tcp_seg *seg,
                        uint16_t node_id, uint32_t timeout_ms)
{
    int ret;

    if (timeout_ms == 0)
        timeout_ms = 5000;

    ret = eba_remote_write(remote_rx_buf, 0, sizeof(*seg),
                           (const char *)seg, node_id, timeout_ms);
    if (ret < 0) {
        fprintf(stderr,
                "[eba_tcp] tcp_send_seg: eba_remote_write to buf %" PRIu64 " failed (rc=%d)\n",
                remote_rx_buf, ret);
        return -1;
    }
    /*
     * Brief pause so the receiver has time to clear the mailbox before the
     * next segment is delivered.  This is only relevant when sending multiple
     * back-to-back segments (e.g. large HTTP response).
     */
    usleep(5000); /* 5 ms */
    return 0;
}

/**
 * build_seg - initialise all fields of a struct eba_tcp_seg.
 */
static void build_seg(struct eba_tcp_seg *seg,
                      uint8_t flags,
                      uint16_t src_port, uint16_t dst_port,
                      uint32_t seq, uint32_t ack,
                      uint64_t reply_buf_id,
                      const void *data, uint32_t data_len)
{
    memset(seg, 0, sizeof(*seg));
    seg->magic        = EBA_TCP_MAGIC;
    seg->flags        = flags;
    seg->src_port     = src_port;
    seg->dst_port     = dst_port;
    seg->seq          = seq;
    seg->ack          = ack;
    seg->data_len     = data_len;
    seg->reply_buf_id = reply_buf_id;

    /* Best-effort MAC fill; failure just means node-ID lookup falls back to 0 */
    get_local_mac(seg->src_mac);

    if (data && data_len > 0) {
        uint32_t copy = (data_len > EBA_TCP_MAX_DATA) ? EBA_TCP_MAX_DATA : data_len;
        memcpy(seg->data, data, copy);
    }
}

/* --------------------------------------------------------------------------
 * Public API implementation
 * -------------------------------------------------------------------------- */

struct eba_tcp_socket *eba_tcp_create_server(uint16_t port)
{
    struct eba_tcp_socket *sock;
    uint64_t orig_id;
    uint32_t zero = 0;

    if (port == 0) {
        fprintf(stderr, "[eba_tcp] create_server: port 0 is invalid\n");
        return NULL;
    }

    sock = malloc(sizeof(*sock));
    if (!sock)
        return NULL;

    /* Allocate the listen mailbox */
    orig_id = eba_alloc(EBA_TCP_RX_BUF_SZ, 0, 0);
    if (!orig_id) {
        fprintf(stderr, "[eba_tcp] create_server: eba_alloc failed\n");
        free(sock);
        return NULL;
    }

    /*
     * Register the buffer as a well-known EBA service with ID = port.
     * After this call the buffer is addressable only by its new ID (= port);
     * the original dynamic ID is invalidated.
     */
    if (eba_register_service(orig_id, (uint64_t)port) < 0) {
        fprintf(stderr,
                "[eba_tcp] create_server: eba_register_service(orig=%" PRIu64 ", port=%u) failed\n",
                orig_id, port);
        free(sock);
        return NULL;
    }

    sock->port       = port;
    sock->listen_buf = (uint64_t)port; /* use the service ID from now on */

    /* Ensure the mailbox appears empty (magic = 0) */
    if (eba_write(&zero, sock->listen_buf, 0, sizeof(zero)) != 0)
        fprintf(stderr, "[eba_tcp] create_server: failed to zero mailbox\n");

    printf("[eba_tcp] server created – listening on port %u (buf_id=%u)\n",
           port, port);
    return sock;
}

void eba_tcp_destroy_server(struct eba_tcp_socket *sock)
{
    free(sock);
}

struct eba_tcp_conn *eba_tcp_accept(struct eba_tcp_socket *sock,
                                    uint32_t timeout_ms)
{
    struct eba_tcp_seg syn, syn_ack, ack_seg;
    struct eba_tcp_conn *conn;
    uint64_t conn_rx_buf;
    uint16_t client_node;
    uint32_t zero = 0;

    if (!sock)
        return NULL;
    if (timeout_ms == 0)
        timeout_ms = 30000;

    printf("[eba_tcp] accept: waiting for SYN on port %u ...\n", sock->port);

    /* ── Step 1: wait for SYN in the listen mailbox ── */
    if (tcp_recv_seg(sock->listen_buf, &syn, timeout_ms) != 0) {
        fprintf(stderr, "[eba_tcp] accept: timeout waiting for SYN\n");
        return NULL;
    }
    if (!(syn.flags & EBA_TCP_SYN)) {
        fprintf(stderr, "[eba_tcp] accept: expected SYN, got flags=0x%02x\n",
                syn.flags);
        return NULL;
    }
    printf("[eba_tcp] accept: SYN from port %u, reply_buf=%" PRIu64 "\n",
           syn.src_port, syn.reply_buf_id);

    /* ── Step 2: allocate a per-connection RX mailbox ── */
    conn_rx_buf = eba_alloc(EBA_TCP_RX_BUF_SZ, 0, 0);
    if (!conn_rx_buf) {
        fprintf(stderr, "[eba_tcp] accept: eba_alloc(conn_rx_buf) failed\n");
        return NULL;
    }
    if (eba_write(&zero, conn_rx_buf, 0, sizeof(zero)) != 0)
        fprintf(stderr, "[eba_tcp] accept: failed to zero conn_rx_buf\n");

    /* ── Step 3: resolve client's EBA node ID from its MAC address ── */
    client_node = get_node_id_by_mac(syn.src_mac);
    printf("[eba_tcp] accept: client_node=%u "
           "(MAC %02x:%02x:%02x:%02x:%02x:%02x)\n",
           client_node,
           syn.src_mac[0], syn.src_mac[1], syn.src_mac[2],
           syn.src_mac[3], syn.src_mac[4], syn.src_mac[5]);

    /* ── Step 4: send SYN-ACK ── */
    build_seg(&syn_ack,
              EBA_TCP_SYN | EBA_TCP_ACK,
              sock->port, syn.src_port,
              1,               /* server ISN */
              syn.seq + 1,     /* ack = client_ISN + 1 */
              conn_rx_buf,     /* server's conn RX buf for subsequent sends */
              NULL, 0);

    if (tcp_send_seg(syn.reply_buf_id, &syn_ack, client_node, 5000) != 0) {
        fprintf(stderr, "[eba_tcp] accept: send SYN-ACK failed\n");
        return NULL;
    }
    printf("[eba_tcp] accept: SYN-ACK sent, waiting for ACK ...\n");

    /* ── Step 5: wait for ACK in the connection mailbox ── */
    if (tcp_recv_seg(conn_rx_buf, &ack_seg, 5000) != 0) {
        fprintf(stderr, "[eba_tcp] accept: timeout waiting for ACK\n");
        return NULL;
    }
    if (!(ack_seg.flags & EBA_TCP_ACK)) {
        fprintf(stderr, "[eba_tcp] accept: expected ACK, got flags=0x%02x\n",
                ack_seg.flags);
        return NULL;
    }

    /* ── Step 6: connection ESTABLISHED ── */
    conn = malloc(sizeof(*conn));
    if (!conn)
        return NULL;

    conn->state          = EBA_TCP_ESTABLISHED;
    conn->local_port     = sock->port;
    conn->remote_port    = syn.src_port;
    conn->rx_buf         = conn_rx_buf;
    conn->remote_rx_buf  = ack_seg.reply_buf_id;  /* client's RX buf */
    conn->remote_node_id = client_node;
    conn->snd_nxt        = 2;           /* next byte after server ISN=1 */
    conn->rcv_nxt        = syn.seq + 1;

    printf("[eba_tcp] accept: ESTABLISHED "
           "(local_rx=%" PRIu64 ", remote_rx=%" PRIu64 ", remote_node=%u)\n",
           conn->rx_buf, conn->remote_rx_buf, conn->remote_node_id);
    return conn;
}

struct eba_tcp_conn *eba_tcp_connect(uint16_t local_port,
                                     uint16_t remote_port,
                                     uint16_t remote_node,
                                     uint32_t timeout_ms)
{
    struct eba_tcp_seg syn, syn_ack, ack;
    struct eba_tcp_conn *conn;
    uint64_t conn_rx_buf;
    uint16_t server_node;
    uint32_t zero = 0;

    if (timeout_ms == 0)
        timeout_ms = 10000;

    /* ── Step 1: allocate local RX mailbox ── */
    conn_rx_buf = eba_alloc(EBA_TCP_RX_BUF_SZ, 0, 0);
    if (!conn_rx_buf) {
        fprintf(stderr, "[eba_tcp] connect: eba_alloc failed\n");
        return NULL;
    }
    if (eba_write(&zero, conn_rx_buf, 0, sizeof(zero)) != 0)
        fprintf(stderr, "[eba_tcp] connect: failed to zero conn_rx_buf\n");

    printf("[eba_tcp] connect: RX buf %" PRIu64 " allocated, "
           "sending SYN to port %u node %u ...\n",
           conn_rx_buf, remote_port, remote_node);

    /* ── Step 2: send SYN to the server's listen buffer (ID = remote_port) ── */
    build_seg(&syn,
              EBA_TCP_SYN,
              local_port, remote_port,
              0,           /* client ISN */
              0,
              conn_rx_buf, /* server will use this to send back SYN-ACK */
              NULL, 0);

    if (tcp_send_seg((uint64_t)remote_port, &syn, remote_node, timeout_ms) != 0) {
        fprintf(stderr, "[eba_tcp] connect: send SYN failed\n");
        return NULL;
    }

    /* ── Step 3: wait for SYN-ACK in local RX buf ── */
    if (tcp_recv_seg(conn_rx_buf, &syn_ack, timeout_ms) != 0) {
        fprintf(stderr, "[eba_tcp] connect: timeout waiting for SYN-ACK\n");
        return NULL;
    }
    if (!((syn_ack.flags & EBA_TCP_SYN) && (syn_ack.flags & EBA_TCP_ACK))) {
        fprintf(stderr,
                "[eba_tcp] connect: expected SYN-ACK, got flags=0x%02x\n",
                syn_ack.flags);
        return NULL;
    }
    printf("[eba_tcp] connect: SYN-ACK received, server_conn_rx=%" PRIu64 "\n",
           syn_ack.reply_buf_id);

    /* Refine the server node ID from its MAC if not already known */
    server_node = remote_node;
    if (server_node == 0)
        server_node = get_node_id_by_mac(syn_ack.src_mac);

    /* ── Step 4: send ACK to the server's connection RX buffer ── */
    build_seg(&ack,
              EBA_TCP_ACK,
              local_port, remote_port,
              1,                  /* client next seq */
              syn_ack.seq + 1,    /* ack = server_ISN + 1 */
              conn_rx_buf,        /* our RX buf for reverse traffic */
              NULL, 0);

    if (tcp_send_seg(syn_ack.reply_buf_id, &ack, server_node, 5000) != 0) {
        fprintf(stderr, "[eba_tcp] connect: send ACK failed\n");
        return NULL;
    }

    /* ── Step 5: connection ESTABLISHED ── */
    conn = malloc(sizeof(*conn));
    if (!conn)
        return NULL;

    conn->state          = EBA_TCP_ESTABLISHED;
    conn->local_port     = local_port;
    conn->remote_port    = remote_port;
    conn->rx_buf         = conn_rx_buf;
    conn->remote_rx_buf  = syn_ack.reply_buf_id;
    conn->remote_node_id = server_node;
    conn->snd_nxt        = 1;
    conn->rcv_nxt        = syn_ack.seq + 1;

    printf("[eba_tcp] connect: ESTABLISHED "
           "(local_rx=%" PRIu64 ", remote_rx=%" PRIu64 ", remote_node=%u)\n",
           conn->rx_buf, conn->remote_rx_buf, conn->remote_node_id);
    return conn;
}

int eba_tcp_send(struct eba_tcp_conn *conn, const void *data, size_t size,
                 uint32_t timeout_ms)
{
    const uint8_t *ptr = (const uint8_t *)data;
    size_t remaining = size;
    uint32_t seq;

    if (!conn || conn->state != EBA_TCP_ESTABLISHED)
        return -1;
    if (timeout_ms == 0)
        timeout_ms = 5000;

    seq = conn->snd_nxt;

    while (remaining > 0) {
        struct eba_tcp_seg seg;
        uint32_t chunk = (remaining > EBA_TCP_MAX_DATA)
                         ? EBA_TCP_MAX_DATA
                         : (uint32_t)remaining;

        build_seg(&seg,
                  EBA_TCP_PSH | EBA_TCP_ACK,
                  conn->local_port, conn->remote_port,
                  seq,
                  conn->rcv_nxt,
                  conn->rx_buf,   /* our RX buf so peer can send replies */
                  ptr, chunk);

        if (tcp_send_seg(conn->remote_rx_buf, &seg,
                         conn->remote_node_id, timeout_ms) != 0) {
            fprintf(stderr,
                    "[eba_tcp] send: segment failed at offset %zu / %zu\n",
                    size - remaining, size);
            return -1;
        }

        ptr       += chunk;
        remaining -= chunk;
        seq       += chunk;
    }

    conn->snd_nxt = seq;
    return 0;
}

int eba_tcp_recv(struct eba_tcp_conn *conn, void *buf, size_t max_size,
                 uint32_t timeout_ms)
{
    struct eba_tcp_seg seg;
    size_t total = 0;
    uint8_t *out = (uint8_t *)buf;
    uint64_t deadline;
    uint32_t wait_ms;

    if (!conn || conn->state != EBA_TCP_ESTABLISHED)
        return -1;
    if (timeout_ms == 0)
        timeout_ms = 10000;

    deadline = tcp_now_ms() + timeout_ms;

    do {
        uint64_t now = tcp_now_ms();
        wait_ms = (now >= deadline) ? 0 : (uint32_t)(deadline - now);
        if (wait_ms == 0)
            break;

        if (tcp_recv_seg(conn->rx_buf, &seg, wait_ms) != 0)
            break; /* timeout – return what we have so far */

        if (seg.flags & EBA_TCP_RST) {
            conn->state = EBA_TCP_CLOSED;
            return -1;
        }

        if (seg.flags & EBA_TCP_FIN) {
            conn->state = EBA_TCP_CLOSE_WAIT;
            break;
        }

        if (seg.data_len > 0 && total < max_size) {
            uint32_t copy = seg.data_len;
            /* Clamp to the actual on-wire payload array to prevent overread */
            if (copy > EBA_TCP_MAX_DATA)
                copy = EBA_TCP_MAX_DATA;
            if (total + copy > max_size)
                copy = (uint32_t)(max_size - total);
            memcpy(out + total, seg.data, copy);
            total        += copy;
            conn->rcv_nxt += seg.data_len;
        }

        /*
         * After receiving the first segment use a much shorter deadline so
         * we don't stall waiting for back-to-back segments that may never come
         * (e.g. after an HTTP request that fits in a single segment).
         */
        deadline = tcp_now_ms() + 200; /* 200 ms for subsequent segments */

    } while (total < max_size);

    return (int)total;
}

int eba_tcp_close(struct eba_tcp_conn *conn)
{
    struct eba_tcp_seg fin, fin_ack;

    if (!conn)
        return 0;

    if (conn->state == EBA_TCP_ESTABLISHED ||
        conn->state == EBA_TCP_CLOSE_WAIT)
    {
        build_seg(&fin,
                  EBA_TCP_FIN | EBA_TCP_ACK,
                  conn->local_port, conn->remote_port,
                  conn->snd_nxt++,
                  conn->rcv_nxt,
                  conn->rx_buf,
                  NULL, 0);

        /* Best-effort FIN – ignore errors */
        tcp_send_seg(conn->remote_rx_buf, &fin, conn->remote_node_id, 2000);

        if (conn->state == EBA_TCP_ESTABLISHED) {
            conn->state = EBA_TCP_FIN_WAIT1;
            /* Wait briefly for the peer's FIN-ACK */
            tcp_recv_seg(conn->rx_buf, &fin_ack, 2000); /* best effort */
        }
    }

    conn->state = EBA_TCP_CLOSED;
    free(conn);
    return 0;
}
