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

/* ---- Free space in our RX buffer (for advertised window) ----------------- */
static uint16_t tcp_free_window(const tcp_socket_t *s) {
    uint32_t free_bytes = (s->rx_len >= TCP_RX_BUF_SIZE) ? 0 : (TCP_RX_BUF_SIZE - s->rx_len);
    if (free_bytes > 0xFFFF) free_bytes = 0xFFFF;
    return (uint16_t)free_bytes;
}

/* ---- Send a raw TCP segment ---------------------------------------------- */
/* Ha flags & TCP_SYN, az options_buf MSS option-t kap (4 byte: 02 04 MSS_hi MSS_lo). */
static void tcp_send_segment(tcp_socket_t *s, uint8_t flags, const void *data, uint32_t data_len) {
    uint8_t buffer[2048];
    uint32_t opt_len = 0;
    if (flags & TCP_SYN) opt_len = 4; /* MSS option only */

    uint32_t seg_len = sizeof(tcp_header_t) + opt_len + data_len;
    if (seg_len > sizeof(buffer)) return;

    tcp_header_t *hdr = (tcp_header_t *)buffer;
    hdr->src_port    = htons(s->local_port);
    hdr->dest_port   = htons(s->remote_port);
    hdr->seq         = htonl(s->seq);
    hdr->ack         = (flags & TCP_ACK) ? htonl(s->ack) : 0;
    hdr->data_offset = (sizeof(tcp_header_t) + opt_len) / 4;
    hdr->reserved    = 0;
    hdr->flags       = flags;
    hdr->window_size = htons(tcp_free_window(s));
    hdr->checksum    = 0;
    hdr->urgent_ptr  = 0;

    uint8_t *p = hdr->options_and_payload;
    if (opt_len == 4) {
        /* MSS option: kind=2, len=4, value=TCP_OUR_MSS */
        p[0] = 2;
        p[1] = 4;
        p[2] = (TCP_OUR_MSS >> 8) & 0xFF;
        p[3] = TCP_OUR_MSS & 0xFF;
        p += 4;
    }

    if (data && data_len > 0)
        kmemcpy(p, data, data_len);

    hdr->checksum = tcp_checksum(&s->dev->ip, &s->remote_ip, buffer, seg_len);
    ipv4_send(s->dev, &s->remote_ip, IPV4_PROTO_TCP, buffer, seg_len);
}

/* ---- Parse TCP options for MSS ------------------------------------------- */
static void tcp_parse_options(tcp_socket_t *s, const tcp_header_t *tcp) {
    uint8_t hdr_len = tcp->data_offset * 4;
    if (hdr_len <= sizeof(tcp_header_t)) return;
    const uint8_t *opt = (const uint8_t *)tcp + sizeof(tcp_header_t);
    uint32_t left = hdr_len - sizeof(tcp_header_t);
    while (left > 0) {
        uint8_t kind = opt[0];
        if (kind == 0) break;          /* END */
        if (kind == 1) { opt++; left--; continue; } /* NOP */
        if (left < 2) break;
        uint8_t olen = opt[1];
        if (olen < 2 || olen > left) break;
        if (kind == 2 && olen == 4) {  /* MSS */
            uint16_t mss = ((uint16_t)opt[2] << 8) | opt[3];
            if (mss >= 64 && mss <= TCP_OUR_MSS) s->peer_mss = mss;
            else s->peer_mss = TCP_OUR_MSS; /* clamp */
        }
        opt += olen;
        left -= olen;
    }
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
    if (!s || s->state != TCP_ESTABLISHED || !data || len == 0) return;
    uint32_t mss = s->peer_mss ? s->peer_mss : TCP_DEFAULT_MSS;
    if (mss > TCP_OUR_MSS) mss = TCP_OUR_MSS;
    const uint8_t *p = (const uint8_t *)data;
    uint32_t left = len;
    while (left > 0) {
        uint32_t chunk = (left > mss) ? mss : left;
        uint8_t flags = TCP_ACK | ((chunk == left) ? TCP_PSH : 0);
        tcp_send_segment(s, flags, p, chunk);
        s->seq += chunk;
        p    += chunk;
        left -= chunk;
    }
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
            /* Got SYN-ACK: parse options (MSS), send ACK, move to ESTABLISHED */
            tcp_parse_options(s, tcp);
            if (!s->peer_mss) s->peer_mss = TCP_DEFAULT_MSS;
            s->ack = remote_seq + 1;
            s->state = TCP_ESTABLISHED;
            tcp_send_segment(s, TCP_ACK, NULL, 0);
            kprintf("[tcp] Connected to %d.%d.%d.%d:%u\n",
                    s->remote_ip.ip[0], s->remote_ip.ip[1],
                    s->remote_ip.ip[2], s->remote_ip.ip[3], s->remote_port);
            if (s->on_connect) s->on_connect(s);
        }
        break;

    case TCP_ESTABLISHED: {
        /* In-order ellenőrzés: csak akkor fogadjuk be a payload-ot, ha a
         * remote_seq pont a következő várt byte. Out-of-order szegmensekre
         * duplicate-ACK-et küldünk és eldobjuk - egyszerű kezdeti viselkedés,
         * a szerver retransmittel. (A TLS rétegnek byte-pontos in-order
         * stream kell, így bármi más csendben elrontaná a rekordokat.) */
        bool consumed_data = false;
        if (data_len > 0) {
            if (remote_seq == s->ack) {
                uint32_t copy = data_len;
                if (s->rx_len + copy > TCP_RX_BUF_SIZE)
                    copy = TCP_RX_BUF_SIZE - s->rx_len;
                kmemcpy(s->rx_buf + s->rx_len, data, copy);
                s->rx_len += copy;
                s->ack += data_len;
                consumed_data = true;
                kprintf("[tcp] rx data seq=%u len=%u rx_buf=%u/%u flags=0x%x\n",
                        remote_seq, data_len, s->rx_len, TCP_RX_BUF_SIZE, flags);
                if (s->on_data) s->on_data(s, data, data_len);
            } else {
                /* Reordered / retransmitted: csak nyugtázzuk amit eddig vártunk. */
                kprintf("[tcp] OUT-OF-ORDER seq=%u expected=%u len=%u flags=0x%x (DROP)\n",
                        remote_seq, s->ack, data_len, flags);
                tcp_send_segment(s, TCP_ACK, NULL, 0);
            }
        } else if (flags & TCP_ACK) {
            /* Csak ACK, nincs data - csendben elfogadjuk. */
        }

        if (flags & TCP_FIN) {
            /* FIN csak akkor "fogyasztott", ha a megfelelő sorrendben van.
             * Ha jött adat ÉS FIN ugyanabban a szegmensben, az ACK
             * = remote_seq + data_len + 1 (s->ack-et már megnöveltük data_len-nel). */
            if (remote_seq + (consumed_data ? data_len : 0) == s->ack) {
                s->ack += 1;
                tcp_send_segment(s, TCP_ACK, NULL, 0);
                s->state = TCP_CLOSE_WAIT;
                if (s->on_close) s->on_close(s);
                /* Saját FIN visszaküldése - egyszerűsített átmenet zárt felé. */
                tcp_send_segment(s, TCP_FIN | TCP_ACK, NULL, 0);
                s->state = TCP_CLOSED;
            }
        } else if (consumed_data) {
            tcp_send_segment(s, TCP_ACK, NULL, 0);
        }
        break;
    }

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
