/**
 * @file eba_tcp.h
 * @brief EBA TCP – a lightweight TCP-like protocol built on top of EBA.
 *
 * Design overview
 * ===============
 * Each endpoint owns a fixed-size *mailbox* buffer allocated from the EBA
 * memory pool.  A TCP segment is written atomically into a peer's mailbox
 * with eba_remote_write(); the receiving side polls the magic word at offset 0
 * to detect an incoming segment and reads it with eba_read().
 *
 * Server listen buffers are registered as EBA *services* whose ID equals the
 * TCP port number.  This means a client only needs to know the server's EBA
 * node ID and port number; no out-of-band buffer-ID exchange is required.
 *
 * All ports 1-65535 are valid because EBA_MAX_SERVICES has been raised to
 * 65536 in include/eba_internals.h.
 *
 * Handshake
 * ---------
 *   Client                          Server
 *   ------                          ------
 *   alloc rx_buf
 *   SYN ──────────────────────────> listen_buf (service ID = port)
 *                 <─────────────── SYN-ACK  (reply_buf_id = conn_rx_buf)
 *   ACK ──────────────────────────> conn_rx_buf
 *                 ESTABLISHED            ESTABLISHED
 *
 * Data transfer (after ESTABLISHED)
 * ----------------------------------
 *   eba_tcp_send() fragments data into EBA_TCP_MAX_DATA-byte segments and
 *   writes each to the peer's conn_rx_buf via eba_remote_write().
 *   eba_tcp_recv() polls the local rx_buf until a segment appears.
 *
 * Connection teardown
 * -------------------
 *   eba_tcp_close() sends a FIN segment and waits briefly for a FIN-ACK.
 */

#ifndef EBA_TCP_H
#define EBA_TCP_H

#include "eba_user.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdio.h>

/* --------------------------------------------------------------------------
 * Configuration
 * -------------------------------------------------------------------------- */

/**
 * EBA_TCP_IFNAME - network interface used for EBA communication.
 * Must match INTERFACE_NAME defined in include/ebp.h.
 * Can be overridden at compile time: -DEBA_TCP_IFNAME='"eth0"'
 */
#ifndef EBA_TCP_IFNAME
#define EBA_TCP_IFNAME  "enp0s8"
#endif

/* --------------------------------------------------------------------------
 * Wire-format constants
 * -------------------------------------------------------------------------- */

/** Magic word that marks a valid segment in a mailbox buffer (0 = empty). */
#define EBA_TCP_MAGIC       0xEBA0FACEu

/** TCP flag bits embedded in struct eba_tcp_seg::flags. */
#define EBA_TCP_SYN         0x01u   /**< Synchronise sequence numbers. */
#define EBA_TCP_ACK         0x02u   /**< Acknowledgment field valid.   */
#define EBA_TCP_FIN         0x04u   /**< No more data from sender.     */
#define EBA_TCP_RST         0x08u   /**< Reset the connection.         */
#define EBA_TCP_PSH         0x10u   /**< Push data to application now. */

/** Maximum application data bytes that fit in a single segment. */
#define EBA_TCP_MAX_DATA    512u

/** Size of the RX mailbox buffer allocated per endpoint (bytes). */
#define EBA_TCP_RX_BUF_SZ   4096u

/** Polling interval when waiting for an incoming segment (microseconds). */
#define EBA_TCP_POLL_US     1000u   /* 1 ms */

/* --------------------------------------------------------------------------
 * Segment structure
 * --------------------------------------------------------------------------
 *
 * A single eba_tcp_seg is written atomically into the peer's mailbox with
 * eba_remote_write().  The receiver polls magic at offset 0:
 *   magic == EBA_TCP_MAGIC  →  valid segment present
 *   magic == 0              →  mailbox is empty
 *
 * After consuming a segment the receiver writes 0 to the first 4 bytes so
 * the slot is marked free for the next incoming segment.
 *
 * Total size: 4+1+2+2+4+4+4+8+6+512 = 547 bytes (fits in one Ethernet frame).
 */
struct eba_tcp_seg {
    uint32_t magic;                  /**< EBA_TCP_MAGIC when valid; 0 when empty.  */
    uint8_t  flags;                  /**< Combination of EBA_TCP_* flag bits.      */
    uint16_t src_port;               /**< Sender's TCP port.                       */
    uint16_t dst_port;               /**< Destination TCP port.                    */
    uint32_t seq;                    /**< Sender's sequence number.                */
    uint32_t ack;                    /**< Acknowledged sequence number.            */
    uint32_t data_len;               /**< Bytes of payload (0..EBA_TCP_MAX_DATA).  */
    uint64_t reply_buf_id;           /**< Sender's RX mailbox buffer ID.           */
    uint8_t  src_mac[6];             /**< Sender's MAC (used to resolve node ID).  */
    uint8_t  data[EBA_TCP_MAX_DATA]; /**< Application payload.                     */
} __attribute__((packed));

/* --------------------------------------------------------------------------
 * TCP connection states
 * -------------------------------------------------------------------------- */

enum eba_tcp_state {
    EBA_TCP_CLOSED = 0,
    EBA_TCP_LISTEN,
    EBA_TCP_SYN_SENT,
    EBA_TCP_SYN_RCVD,
    EBA_TCP_ESTABLISHED,
    EBA_TCP_FIN_WAIT1,
    EBA_TCP_FIN_WAIT2,
    EBA_TCP_CLOSE_WAIT,
    EBA_TCP_LAST_ACK,
    EBA_TCP_TIME_WAIT,
};

/* --------------------------------------------------------------------------
 * Handle types
 * -------------------------------------------------------------------------- */

/**
 * struct eba_tcp_socket - server-side listen handle.
 *
 * Created by eba_tcp_create_server(); destroyed by eba_tcp_destroy_server().
 */
struct eba_tcp_socket {
    uint16_t port;        /**< TCP port number (= EBA service/buffer ID). */
    uint64_t listen_buf;  /**< EBA buffer ID (equals port after register_service). */
};

/**
 * struct eba_tcp_conn - per-connection state.
 *
 * Returned by eba_tcp_accept() or eba_tcp_connect().
 * Freed by eba_tcp_close().
 */
struct eba_tcp_conn {
    enum eba_tcp_state state;
    uint16_t local_port;
    uint16_t remote_port;
    uint64_t rx_buf;           /**< Local RX mailbox buffer ID.        */
    uint64_t remote_rx_buf;    /**< Peer's RX mailbox buffer ID.       */
    uint16_t remote_node_id;   /**< Peer's EBA node ID (0 = broadcast).*/
    uint32_t snd_nxt;          /**< Next sequence number to send.      */
    uint32_t rcv_nxt;          /**< Next expected incoming sequence.   */
};

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

/**
 * eba_tcp_create_server - allocate a listen socket bound to @port.
 * @port: TCP port (1-65535).  Must not already be registered as an EBA
 *        service on this node.
 *
 * Allocates an EBA buffer, registers it with service ID = @port, and zeroes
 * the mailbox so it is immediately ready for incoming connections.
 *
 * Returns a heap-allocated eba_tcp_socket on success, NULL on failure.
 */
struct eba_tcp_socket *eba_tcp_create_server(uint16_t port);

/**
 * eba_tcp_destroy_server - release resources associated with a listen socket.
 * @sock: socket returned by eba_tcp_create_server().  May be NULL.
 */
void eba_tcp_destroy_server(struct eba_tcp_socket *sock);

/**
 * eba_tcp_accept - wait for an incoming TCP connection.
 * @sock:       listen socket.
 * @timeout_ms: maximum time to wait in ms.  0 uses a 30-second default.
 *
 * Polls the listen mailbox for a SYN segment, completes the 3-way handshake,
 * and returns a ready-to-use connection handle.
 *
 * Returns a heap-allocated eba_tcp_conn on success, NULL on error/timeout.
 */
struct eba_tcp_conn *eba_tcp_accept(struct eba_tcp_socket *sock,
                                    uint32_t timeout_ms);

/**
 * eba_tcp_connect - connect to a remote TCP server.
 * @local_port:  source port for this connection.
 * @remote_port: server's TCP port (must be registered as an EBA service).
 * @remote_node: EBA node ID of the server (from eba_get_node_infos() after
 *               eba_discover()).  Pass 0 to use Ethernet broadcast (works
 *               in a 2-node cluster but generates noise on larger networks).
 * @timeout_ms:  handshake timeout in ms.  0 uses a 10-second default.
 *
 * Returns a heap-allocated eba_tcp_conn on success, NULL on error/timeout.
 */
struct eba_tcp_conn *eba_tcp_connect(uint16_t local_port,
                                     uint16_t remote_port,
                                     uint16_t remote_node,
                                     uint32_t timeout_ms);

/**
 * eba_tcp_send - send data over an established connection.
 * @conn:       connection returned by eba_tcp_accept() / eba_tcp_connect().
 * @data:       pointer to data to send.
 * @size:       number of bytes.
 * @timeout_ms: per-segment send timeout in ms.  0 uses a 5-second default.
 *
 * Fragments @data into EBA_TCP_MAX_DATA-byte segments and writes each to the
 * peer's mailbox sequentially.
 *
 * Returns 0 on success, -1 on error.
 */
int eba_tcp_send(struct eba_tcp_conn *conn, const void *data, size_t size,
                 uint32_t timeout_ms);

/**
 * eba_tcp_recv - receive data from an established connection.
 * @conn:       connection.
 * @buf:        output buffer.
 * @max_size:   maximum bytes to copy into @buf.
 * @timeout_ms: wait timeout for the first segment in ms.  0 uses 10 seconds.
 *
 * Polls for incoming segments, accumulating data until @max_size bytes are
 * received, a FIN is seen, or the short inter-segment timeout (200 ms) fires.
 *
 * Returns bytes written into @buf (≥ 0), or -1 on error.
 * Returns 0 when the peer sent a FIN (connection closing).
 */
int eba_tcp_recv(struct eba_tcp_conn *conn, void *buf, size_t max_size,
                 uint32_t timeout_ms);

/**
 * eba_tcp_close - gracefully close a connection.
 * @conn: connection to close.  The pointer is freed before this returns.
 *
 * Sends a FIN-ACK and waits briefly for the peer's FIN-ACK.  Frees all
 * resources regardless of whether the handshake completes.
 *
 * Returns 0.
 */
int eba_tcp_close(struct eba_tcp_conn *conn);

#endif /* EBA_TCP_H */
