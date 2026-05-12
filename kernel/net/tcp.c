/* =============================================================================
 *  RexOS - TCP (Transmission Control Protocol) Implementation
 *  Supports: SYN / SYN-ACK / ACK (three-way handshake), data send/recv, FIN.
 * ========================================================================== */
#include <rexos/net.h>
#include <mm/heap.h>
#include <lib/printf.h>
#include <lib/string.h>

/* ---- Socket table -------------------------------------------------------- */
#define TCP_MAX_SOCKETS 8

static tcp_socket_t *s_sockets = NULL;
static uint16_t s_next_port = 49152; /* Ephemeral port range start */

static uint16_t tcp_alloc_port(void) {
    return s_next_port++;
}

static tcp_socket_t *tcp_find_socket(const ip4_addr_t *remote_ip, uint16_t remote_port, uint16_t local_port) {
    for (tcp_socket_t *s = s_sockets; s; s = s->next) {
        if (s->remote_port == remote_port &&
            s->local_port == local_port &&
            kmemcmp(s->remote_ip.ip, remote_ip->ip, 4) == 0) {
            return s;
        }
    }
    return NULL;
}

/* ---- Pseudo-header checksum for TCP -------------------------------------- */
static uint16_t tcp_checksum(const ip4_addr_t *src, const ip4_addr_t *dst,
                             const void *tcp_seg, uint32_t tcp_len) {
    /* Pseudo-header: src_ip, dst_ip, zero, proto, tcp_len */
    uint8_t pseudo[12];
    kmemcpy(pseudo + 0, src->ip, 4);
    kmemcpy(pseudo + 4, dst->ip, 4);
    pseudo[8] = 0;
    pseudo[9] = IPV4_PROTO_TCP;
    pseudo[10] = (tcp_len >> 8) & 0xFF;
    pseudo[11] = tcp_len & 0xFF;

    /* Sum over pseudo-header + TCP segment */
    uint32_t sum = 0;
    const uint16_t *p = (const uint16_t *)pseudo;
    for (int i = 0; i < 6; i++) sum += p[i];

    p = (const uint16_t *)tcp_seg;
    uint32_t words = tcp_len / 2;
    for (uint32_t i = 0; i < words; i++) sum += p[i];
    if (tcp_len & 1) sum += ((const uint8_t *)tcp_seg)[tcp_len - 1];

    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return ~sum;
}

/* ---- Send a raw TCP segment ---------------------------------------------- */
static void tcp_send_segment(tcp_socket_t *s, uint8_t flags, const void *data, uint32_t data_len) {
    uint32_t seg_len = sizeof(tcp_header_t) + data_len;
    uint8_t buffer[2048];
    if (seg_len > sizeof(buffer)) return;

    tcp_header_t *hdr = (tcp_header_t *)buffer;
    hdr->src_port    = htons(s->local_port);
    hdr->dest_port   = htons(s->remote_port);
    hdr->seq         = htonl(s->seq);
    hdr->ack         = (flags & TCP_ACK) ? htonl(s->ack) : 0;
    hdr->data_offset = 5; /* 5 × 4 = 20 bytes, no options */
    hdr->reserved    = 0;
    hdr->flags       = flags;
    hdr->window_size = htons(TCP_RX_BUF_SIZE);
    hdr->checksum    = 0;
    hdr->urgent_ptr  = 0;

    if (data && data_len > 0)
        kmemcpy(hdr->options_and_payload, data, data_len);

    hdr->checksum = tcp_checksum(&s->dev->ip, &s->remote_ip, buffer, seg_len);
    ipv4_send(s->dev, &s->remote_ip, IPV4_PROTO_TCP, buffer, seg_len);
}

/* ---- Public API ---------------------------------------------------------- */

tcp_socket_t *tcp_connect(net_device_t *dev, const ip4_addr_t *dest_ip, uint16_t dest_port) {
    tcp_socket_t *s = (tcp_socket_t *)kmalloc(sizeof(tcp_socket_t));
    if (!s) return NULL;

    kmemset(s, 0, sizeof(*s));
    s->dev         = dev;
    s->remote_ip   = *dest_ip;
    s->remote_port = dest_port;
    s->local_port  = tcp_alloc_port();
    s->seq         = 0xA1B2C3D4; /* ISN - Initial Sequence Number */
    s->ack         = 0;
    s->state       = TCP_SYN_SENT;

    /* Add to socket list */
    s->next = s_sockets;
    s_sockets = s;

    kprintf("[tcp] Connecting to %d.%d.%d.%d:%u from port %u\n",
            dest_ip->ip[0], dest_ip->ip[1], dest_ip->ip[2], dest_ip->ip[3],
            dest_port, s->local_port);

    /* Send SYN */
    tcp_send_segment(s, TCP_SYN, NULL, 0);
    s->seq++; /* SYN consumes one sequence number */

    return s;
}

void tcp_send_data(tcp_socket_t *s, const void *data, uint32_t len) {
    if (!s || s->state != TCP_ESTABLISHED) return;
    tcp_send_segment(s, TCP_ACK | TCP_PSH, data, len);
    s->seq += len;
}

void tcp_close(tcp_socket_t *s) {
    if (!s || s->state == TCP_CLOSED) return;
    tcp_send_segment(s, TCP_FIN | TCP_ACK, NULL, 0);
    s->seq++;
    s->state = TCP_FIN_WAIT_1;
}

bool tcp_socket_is_valid(tcp_socket_t *sock) {
    for (tcp_socket_t *s = s_sockets; s; s = s->next) {
        if (s == sock) return true;
    }
    return false;
}

void tcp_release(tcp_socket_t *sock) {
    if (!sock) return;

    tcp_socket_t **link = &s_sockets;
    while (*link && *link != sock) link = &(*link)->next;
    if (!*link) return;

    if (sock->state == TCP_ESTABLISHED || sock->state == TCP_CLOSE_WAIT) {
        tcp_close(sock);
    }
    *link = sock->next;
    sock->next = NULL;
    kfree(sock);
}

void tcp_receive(net_device_t *dev, const ip4_header_t *ip_hdr, const tcp_header_t *tcp, uint32_t len) {
    uint16_t local_port  = ntohs(tcp->dest_port);
    uint16_t remote_port = ntohs(tcp->src_port);
    uint32_t remote_seq  = ntohl(tcp->seq);
    uint32_t remote_ack  = ntohl(tcp->ack);
    uint8_t  flags       = tcp->flags;

    (void)remote_ack;

    uint8_t hdr_len = tcp->data_offset * 4;
    if (len < hdr_len) return;
    uint32_t data_len = len - hdr_len;
    const uint8_t *data = (const uint8_t *)tcp + hdr_len;

    tcp_socket_t *s = tcp_find_socket(&ip_hdr->src_ip, remote_port, local_port);
    if (!s) return;

    switch (s->state) {

    case TCP_SYN_SENT:
        if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)) {
            /* Got SYN-ACK: send ACK, move to ESTABLISHED */
            s->ack = remote_seq + 1;
            s->state = TCP_ESTABLISHED;
            tcp_send_segment(s, TCP_ACK, NULL, 0);
            kprintf("[tcp] Connected to %d.%d.%d.%d:%u\n",
                    s->remote_ip.ip[0], s->remote_ip.ip[1],
                    s->remote_ip.ip[2], s->remote_ip.ip[3], s->remote_port);
            if (s->on_connect) s->on_connect(s);
        }
        break;

    case TCP_ESTABLISHED:
        if (data_len > 0) {
            s->ack = remote_seq + data_len;

            /* Store into rx_buf */
            uint32_t copy = data_len;
            if (s->rx_len + copy > TCP_RX_BUF_SIZE)
                copy = TCP_RX_BUF_SIZE - s->rx_len;
            kmemcpy(s->rx_buf + s->rx_len, data, copy);
            s->rx_len += copy;

            /* Send ACK */
            tcp_send_segment(s, TCP_ACK, NULL, 0);

            if (s->on_data) s->on_data(s, data, data_len);
        }
        if (flags & TCP_FIN) {
            s->ack = remote_seq + 1;
            tcp_send_segment(s, TCP_ACK, NULL, 0);
            s->state = TCP_CLOSE_WAIT;
            if (s->on_close) s->on_close(s);
            /* Send our own FIN */
            tcp_send_segment(s, TCP_FIN | TCP_ACK, NULL, 0);
            s->state = TCP_CLOSED;
        }
        break;

    case TCP_FIN_WAIT_1:
        if (flags & TCP_ACK) s->state = TCP_FIN_WAIT_2;
        if (flags & TCP_FIN) {
            s->ack = remote_seq + 1;
            tcp_send_segment(s, TCP_ACK, NULL, 0);
            s->state = TCP_CLOSED;
        }
        break;

    case TCP_FIN_WAIT_2:
        if (flags & TCP_FIN) {
            s->ack = remote_seq + 1;
            tcp_send_segment(s, TCP_ACK, NULL, 0);
            s->state = TCP_CLOSED;
        }
        break;

    default:
        break;
    }

    /* Use dev to suppress unused warning */
    (void)dev;
}
