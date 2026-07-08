/* =============================================================================
 *  RexOS - TCP (Transmission Control Protocol) Implementation
 *  Supports: three-way handshake, in-order data recv (with overlap trimming),
 *  send-side retransmission (simplified RTO), RST handling, FIN teardown.
 *
 *  Konkurrencia-modell (single core):
 *    - tcp_receive() az e1000 IRQ kontextusában fut (IF=0).
 *    - A publikus API-k (connect/send/close/poll/release) task kontextusból
 *      hívódnak; a socket-állapotot IRQ-letiltással védik, így nem
 *      versenyeznek az IRQ-oldali feldolgozással.
 * ========================================================================== */
#include <rexos/net.h>
#include <mm/heap.h>
#include <lib/printf.h>
#include <lib/string.h>

extern uint64_t pit_ticks(void);

/* ---- IRQ save/restore (nested-safe) --------------------------------------- */
static inline uint64_t tcp_irq_save(void) {
    uint64_t flags;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(flags) :: "memory");
    return flags;
}
static inline void tcp_irq_restore(uint64_t flags) {
    if (flags & 0x200) __asm__ volatile("sti" ::: "memory");
}

static inline uint64_t tcp_rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

/* ---- Socket table -------------------------------------------------------- */
static tcp_socket_t *s_sockets = NULL;
static uint16_t s_next_port = 49152; /* Ephemeral port range start */

static bool tcp_port_in_use(uint16_t port) {
    for (tcp_socket_t *s = s_sockets; s; s = s->next) {
        if (s->local_port == port) return true;
    }
    return false;
}

static uint16_t tcp_alloc_port(void) {
    for (int guard = 0; guard < 65536; guard++) {
        if (s_next_port < 49152) s_next_port = 49152; /* wrap vissza az ephemeral sávba */
        uint16_t p = s_next_port++;
        if (!tcp_port_in_use(p)) return p;
    }
    return 49152; /* elvileg elérhetetlen */
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
/* Ha flags & TCP_SYN, az options_buf MSS option-t kap (4 byte: 02 04 MSS_hi MSS_lo).
 * A seq explicit paraméter, hogy retransmitnél régebbi sorszámmal is tudjunk
 * küldeni (SYN újraküldés, tx_buf visszajátszás). */
static void tcp_output(tcp_socket_t *s, uint8_t flags, uint32_t seq,
                       const void *data, uint32_t data_len) {
    uint8_t buffer[2048];
    uint32_t opt_len = 0;
    if (flags & TCP_SYN) opt_len = 4; /* MSS option only */

    uint32_t seg_len = sizeof(tcp_header_t) + opt_len + data_len;
    if (seg_len > sizeof(buffer)) return;

    tcp_header_t *hdr = (tcp_header_t *)buffer;
    hdr->src_port    = htons(s->local_port);
    hdr->dest_port   = htons(s->remote_port);
    hdr->seq         = htonl(seq);
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

static void tcp_send_segment(tcp_socket_t *s, uint8_t flags, const void *data, uint32_t data_len) {
    tcp_output(s, flags, s->seq, data, data_len);
}

/* ---- RST válasz socket nélküli szegmensre (RFC 793 3.4) ------------------- */
static void tcp_send_rst(net_device_t *dev, const ip4_header_t *ip_hdr,
                         const tcp_header_t *tcp, uint32_t data_len) {
    tcp_header_t rst;
    kmemset(&rst, 0, sizeof(rst));
    rst.src_port    = tcp->dest_port;
    rst.dest_port   = tcp->src_port;
    rst.data_offset = sizeof(tcp_header_t) / 4;

    uint32_t remote_seq = ntohl(tcp->seq);
    uint32_t seg_ack = remote_seq + data_len
                     + ((tcp->flags & TCP_SYN) ? 1 : 0)
                     + ((tcp->flags & TCP_FIN) ? 1 : 0);
    if (tcp->flags & TCP_ACK) {
        rst.seq   = tcp->ack; /* már network byte order */
        rst.flags = TCP_RST;
    } else {
        rst.seq   = 0;
        rst.ack   = htonl(seg_ack);
        rst.flags = TCP_RST | TCP_ACK;
    }
    rst.checksum = tcp_checksum(&ip_hdr->dest_ip, &ip_hdr->src_ip, &rst, sizeof(rst));
    ipv4_send(dev, &ip_hdr->src_ip, IPV4_PROTO_TCP, &rst, sizeof(rst));
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

/* ---- ACK feldolgozás: tx_buf ürítése, RTO frissítés ----------------------- */
static void tcp_handle_ack(tcp_socket_t *s, uint32_t remote_ack) {
    uint32_t outstanding = s->seq - s->snd_una;      /* mod 2^32 */
    uint32_t acked       = remote_ack - s->snd_una;  /* mod 2^32 */
    if (acked == 0 || acked > outstanding) return;   /* régi vagy hibás ACK */

    uint32_t from_buf = (acked > s->tx_len) ? s->tx_len : acked;
    if (from_buf) {
        kmemmove(s->tx_buf, s->tx_buf + from_buf, s->tx_len - from_buf);
        s->tx_len -= from_buf;
    }
    s->snd_una += acked;
    s->rto_tries = 0;
    s->rto_deadline = s->tx_len ? pit_ticks() + TCP_RTO_BASE_TICKS : 0;
}

/* ---- Public API ---------------------------------------------------------- */

tcp_socket_t *tcp_connect(net_device_t *dev, const ip4_addr_t *dest_ip, uint16_t dest_port) {
    tcp_socket_t *s = (tcp_socket_t *)kmalloc(sizeof(tcp_socket_t));
    if (!s) return NULL;

    kmemset(s, 0, sizeof(*s));
    s->dev         = dev;
    s->remote_ip   = *dest_ip;
    s->remote_port = dest_port;

    uint64_t flags = tcp_irq_save();
    s->local_port  = tcp_alloc_port();
    /* ISN: TSC-alapú, hogy ne legyen megjósolható és ne ütközzön korábbi
     * kapcsolat sorszámaival (RFC 6528 szellemében, egyszerűsítve). */
    uint64_t tsc = tcp_rdtsc();
    s->iss     = (uint32_t)(tsc ^ (tsc >> 17) ^ ((uint64_t)dest_port << 20));
    s->seq     = s->iss;
    s->snd_una = s->iss;
    s->ack     = 0;
    s->state   = TCP_SYN_SENT;

    /* Add to socket list */
    s->next = s_sockets;
    s_sockets = s;

    kprintf("[tcp] Connecting to %d.%d.%d.%d:%u from port %u\n",
            dest_ip->ip[0], dest_ip->ip[1], dest_ip->ip[2], dest_ip->ip[3],
            dest_port, s->local_port);

    /* Send SYN */
    tcp_output(s, TCP_SYN, s->iss, NULL, 0);
    s->seq++; /* SYN consumes one sequence number */
    s->rto_tries = 0;
    s->rto_deadline = pit_ticks() + TCP_RTO_BASE_TICKS;
    tcp_irq_restore(flags);

    return s;
}

void tcp_send_data(tcp_socket_t *s, const void *data, uint32_t len) {
    if (!s || !data || len == 0) return;

    uint64_t flags = tcp_irq_save();
    if (s->state != TCP_ESTABLISHED) {
        tcp_irq_restore(flags);
        return;
    }
    uint32_t mss = s->peer_mss ? s->peer_mss : TCP_DEFAULT_MSS;
    if (mss > TCP_OUR_MSS) mss = TCP_OUR_MSS;

    /* Retransmit-másolat: ami befér, azt eltesszük. Ha a tx_buf betelik,
     * a maradékot buffer nélkül küldjük (best effort). */
    uint32_t space = TCP_TX_BUF_SIZE - s->tx_len;
    uint32_t keep  = (len < space) ? len : space;
    if (keep) {
        kmemcpy(s->tx_buf + s->tx_len, data, keep);
        s->tx_len += keep;
    }

    const uint8_t *p = (const uint8_t *)data;
    uint32_t left = len;
    while (left > 0) {
        uint32_t chunk = (left > mss) ? mss : left;
        uint8_t seg_flags = TCP_ACK | ((chunk == left) ? TCP_PSH : 0);
        tcp_send_segment(s, seg_flags, p, chunk);
        s->seq += chunk;
        p    += chunk;
        left -= chunk;
    }

    if (!s->rto_deadline) {
        s->rto_tries = 0;
        s->rto_deadline = pit_ticks() + TCP_RTO_BASE_TICKS;
    }
    tcp_irq_restore(flags);
}

/* Lejárt RTO-k kezelése. Task kontextusból hívjuk (syscall várakozó ciklusok),
 * így nem az IRQ útvonalat terheli. */
void tcp_poll(void) {
    uint64_t flags = tcp_irq_save();
    uint64_t now = pit_ticks();

    for (tcp_socket_t *s = s_sockets; s; s = s->next) {
        if (!s->rto_deadline || now < s->rto_deadline) continue;

        if (s->rto_tries >= TCP_MAX_RETRIES) {
            kprintf("[tcp] port %u: retransmit limit, dropping connection\n", s->local_port);
            s->state = TCP_CLOSED;
            s->rto_deadline = 0;
            continue;
        }
        s->rto_tries++;
        uint64_t backoff = (uint64_t)TCP_RTO_BASE_TICKS << s->rto_tries;
        s->rto_deadline = now + backoff;

        if (s->state == TCP_SYN_SENT) {
            kprintf("[tcp] port %u: SYN retransmit #%u\n", s->local_port, s->rto_tries);
            tcp_output(s, TCP_SYN, s->iss, NULL, 0);
        } else if (s->state == TCP_ESTABLISHED && s->tx_len > 0) {
            uint32_t mss = s->peer_mss ? s->peer_mss : TCP_DEFAULT_MSS;
            kprintf("[tcp] port %u: data retransmit #%u (%u bytes from seq %u)\n",
                    s->local_port, s->rto_tries, s->tx_len, s->snd_una);
            uint32_t off = 0;
            while (off < s->tx_len) {
                uint32_t chunk = s->tx_len - off;
                if (chunk > mss) chunk = mss;
                uint8_t seg_flags = TCP_ACK | ((off + chunk == s->tx_len) ? TCP_PSH : 0);
                tcp_output(s, seg_flags, s->snd_una + off, s->tx_buf + off, chunk);
                off += chunk;
            }
        } else {
            s->rto_deadline = 0; /* nincs mit újraküldeni */
        }
    }
    tcp_irq_restore(flags);
}

void tcp_send_window_update(tcp_socket_t *s) {
    if (!s || s->state != TCP_ESTABLISHED) return;
    tcp_send_segment(s, TCP_ACK, NULL, 0);
}

void tcp_close(tcp_socket_t *s) {
    if (!s || s->state == TCP_CLOSED) return;
    uint64_t flags = tcp_irq_save();
    if (s->state != TCP_CLOSED) {
        tcp_send_segment(s, TCP_FIN | TCP_ACK, NULL, 0);
        s->seq++;
        s->state = TCP_FIN_WAIT_1;
    }
    tcp_irq_restore(flags);
}

bool tcp_socket_is_valid(tcp_socket_t *sock) {
    uint64_t flags = tcp_irq_save();
    bool found = false;
    for (tcp_socket_t *s = s_sockets; s; s = s->next) {
        if (s == sock) { found = true; break; }
    }
    tcp_irq_restore(flags);
    return found;
}

void tcp_release(tcp_socket_t *sock) {
    if (!sock) return;

    uint64_t flags = tcp_irq_save();
    tcp_socket_t **link = &s_sockets;
    while (*link && *link != sock) link = &(*link)->next;
    if (!*link) {
        tcp_irq_restore(flags);
        return;
    }

    if (sock->state == TCP_ESTABLISHED || sock->state == TCP_CLOSE_WAIT) {
        tcp_send_segment(sock, TCP_FIN | TCP_ACK, NULL, 0);
        sock->seq++;
    }
    *link = sock->next;
    sock->next = NULL;
    tcp_irq_restore(flags);
    kfree(sock);
}

/* ---- Bejövő szegmens feldolgozás (IRQ kontextus) -------------------------- */
void tcp_receive(net_device_t *dev, const ip4_header_t *ip_hdr, const tcp_header_t *tcp, uint32_t len) {
    uint16_t local_port  = ntohs(tcp->dest_port);
    uint16_t remote_port = ntohs(tcp->src_port);
    uint32_t remote_seq  = ntohl(tcp->seq);
    uint32_t remote_ack  = ntohl(tcp->ack);
    uint8_t  flags       = tcp->flags;

    uint8_t hdr_len = tcp->data_offset * 4;
    if (hdr_len < sizeof(tcp_header_t) || len < hdr_len) return;

    /* Checksum ellenőrzés: sérült szegmenst eldobunk (a pszeudó-fejléces
     * összeg a checksum mezővel együtt 0-t ad vissza, ha érvényes). */
    if (tcp_checksum(&ip_hdr->src_ip, &ip_hdr->dest_ip, tcp, len) != 0) {
        kprintf("[tcp] bad checksum, dropping segment (port %u)\n", local_port);
        return;
    }

    uint32_t data_len = len - hdr_len;
    const uint8_t *data = (const uint8_t *)tcp + hdr_len;

    tcp_socket_t *s = tcp_find_socket(&ip_hdr->src_ip, remote_port, local_port);
    if (!s) {
        /* Nincs ilyen kapcsolat: RST-tel jelezzük (kivéve ha maga is RST). */
        if (!(flags & TCP_RST)) tcp_send_rst(dev, ip_hdr, tcp, data_len);
        return;
    }

    /* RST: azonnali bontás. A csatlakozó/fogadó syscall a CLOSED állapotból
     * tudja, hogy a peer elutasította / eldobta a kapcsolatot. */
    if (flags & TCP_RST) {
        kprintf("[tcp] RST from peer (port %u, state %d)\n", s->local_port, s->state);
        s->state = TCP_CLOSED;
        s->rto_deadline = 0;
        if (s->on_close) s->on_close(s);
        return;
    }

    switch (s->state) {

    case TCP_SYN_SENT:
        if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)) {
            /* Got SYN-ACK: parse options (MSS), send ACK, move to ESTABLISHED */
            tcp_parse_options(s, tcp);
            if (!s->peer_mss) s->peer_mss = TCP_DEFAULT_MSS;
            tcp_handle_ack(s, remote_ack); /* SYN-ünk nyugtázva: snd_una = iss+1 */
            s->ack = remote_seq + 1;
            s->state = TCP_ESTABLISHED;
            s->rto_deadline = 0;
            tcp_send_segment(s, TCP_ACK, NULL, 0);
            kprintf("[tcp] Connected to %d.%d.%d.%d:%u\n",
                    s->remote_ip.ip[0], s->remote_ip.ip[1],
                    s->remote_ip.ip[2], s->remote_ip.ip[3], s->remote_port);
            if (s->on_connect) s->on_connect(s);
        }
        break;

    case TCP_ESTABLISHED: {
        if (flags & TCP_ACK) tcp_handle_ack(s, remote_ack);

        /* Átfedés-levágás: ha a szegmens eleje már megvan (retransmit),
         * csak az új farokrészt dolgozzuk fel. */
        bool consumed_data = false;
        if (data_len > 0) {
            uint32_t have = s->ack - remote_seq; /* mennyi byte-unk van már ebből (mod 2^32) */
            if (have < 0x80000000u && have < data_len) {
                data       += have;
                data_len   -= have;
                remote_seq += have;
                /* remote_seq == s->ack: pontosan a következő várt byte-tól */
                if (s->rx_len + data_len <= TCP_RX_BUF_SIZE) {
                    kmemcpy(s->rx_buf + s->rx_len, data, data_len);
                    s->rx_len += data_len;
                    s->ack    += data_len;
                    consumed_data = true;
                    if (s->on_data) s->on_data(s, data, data_len);
                } else {
                    /* Nem fér be: NEM nyugtázzuk, a peer újraküldi, amikor a
                     * hirdetett ablak (rx_buf szabad hely) újra nyílik. */
                    tcp_send_segment(s, TCP_ACK, NULL, 0);
                }
            } else {
                /* Teljesen duplikált vagy out-of-order: duplicate ACK. */
                tcp_send_segment(s, TCP_ACK, NULL, 0);
            }
        }

        if (flags & TCP_FIN) {
            /* FIN csak sorrendben fogyasztható: a szegmens utolsó byte-ja után. */
            if (remote_seq + data_len == s->ack) {
                s->ack += 1;
                tcp_send_segment(s, TCP_ACK, NULL, 0);
                s->state = TCP_CLOSE_WAIT;
                if (s->on_close) s->on_close(s);
                /* Fél-lezárást nem támogatunk: azonnal küldjük a saját FIN-t. */
                tcp_send_segment(s, TCP_FIN | TCP_ACK, NULL, 0);
                s->seq++;
                s->state = TCP_LAST_ACK;
            }
        } else if (consumed_data) {
            tcp_send_segment(s, TCP_ACK, NULL, 0);
        }
        break;
    }

    case TCP_LAST_ACK:
        if ((flags & TCP_ACK) && remote_ack == s->seq) {
            s->state = TCP_CLOSED;
            s->rto_deadline = 0;
        }
        break;

    case TCP_FIN_WAIT_1:
        if (flags & TCP_ACK) {
            tcp_handle_ack(s, remote_ack);
            if (remote_ack == s->seq) s->state = TCP_FIN_WAIT_2;
        }
        if (flags & TCP_FIN) {
            s->ack = remote_seq + data_len + 1;
            tcp_send_segment(s, TCP_ACK, NULL, 0);
            s->state = TCP_CLOSED; /* TIME_WAIT kihagyva (egyszerűsítés) */
        }
        break;

    case TCP_FIN_WAIT_2:
        if (flags & TCP_FIN) {
            s->ack = remote_seq + data_len + 1;
            tcp_send_segment(s, TCP_ACK, NULL, 0);
            s->state = TCP_CLOSED;
        }
        break;

    default:
        break;
    }

    (void)dev;
}
