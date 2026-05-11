/* =============================================================================
 *  RexOS - DNS Client (RFC 1035)
 *  Sends a DNS query via UDP port 53 and parses the response.
 * ========================================================================== */
#include <rexos/net.h>
#include <lib/printf.h>
#include <lib/string.h>

#define DNS_PORT       53
#define DNS_LOCAL_PORT 1053
#define DNS_SERVER_IP  { 8, 8, 8, 8 }   /* Google Public DNS */
#define PIT_HZ         100
#define DNS_TIMEOUT_TICKS (2 * PIT_HZ)   /* 2 seconds at the 100 Hz PIT rate */

/* DNS Header structure */
typedef struct {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;  /* Question count */
    uint16_t ancount;  /* Answer count */
    uint16_t nscount;  /* Name server count */
    uint16_t arcount;  /* Additional records count */
} __packed dns_header_t;

/* Resolved IP storage (set by dns_receive callback) */
static ip4_addr_t s_resolved_ip;
static bool       s_resolved = false;
static uint16_t   s_query_id = 0xBEEF;

/* ---- Encode hostname into DNS wire format -------------------------------- */
/* "example.com" -> \x07example\x03com\x00  */
static uint32_t dns_encode_name(const char *hostname, uint8_t *out) {
    uint32_t total = 0;
    while (*hostname) {
        const char *dot = hostname;
        while (*dot && *dot != '.') dot++;
        uint8_t len = (uint8_t)(dot - hostname);
        out[total++] = len;
        for (uint8_t i = 0; i < len; i++) out[total++] = hostname[i];
        hostname = (*dot == '.') ? dot + 1 : dot;
    }
    out[total++] = 0; /* Root label */
    return total;
}

/* ---- Send a DNS A-record query ------------------------------------------ */
static void dns_send_query(net_device_t *dev, const char *hostname) {
    uint8_t packet[512];
    uint32_t pos = 0;

    dns_header_t *hdr = (dns_header_t *)packet;
    hdr->id      = htons(s_query_id);
    hdr->flags   = htons(0x0100);  /* QR=0 (query), RD=1 (recursion desired) */
    hdr->qdcount = htons(1);
    hdr->ancount = 0;
    hdr->nscount = 0;
    hdr->arcount = 0;
    pos = sizeof(dns_header_t);

    /* Encode the hostname */
    pos += dns_encode_name(hostname, packet + pos);

    /* QTYPE=A (1), QCLASS=IN (1) */
    packet[pos++] = 0x00; packet[pos++] = 0x01;
    packet[pos++] = 0x00; packet[pos++] = 0x01;

    ip4_addr_t dns_server = { DNS_SERVER_IP };
    udp_send(dev, &dns_server, DNS_LOCAL_PORT, DNS_PORT, packet, pos);
    kprintf("[dns] Query sent for '%s' (id=0x%x)\n", hostname, s_query_id);
}

/* ---- Parse DNS response -------------------------------------------------- */
void dns_receive(net_device_t *dev, const uint8_t *data, uint32_t len, uint16_t src_port) {
    (void)dev;
    if (src_port != DNS_PORT) return;
    if (len < sizeof(dns_header_t)) return;

    const dns_header_t *hdr = (const dns_header_t *)data;
    uint16_t id = ntohs(hdr->id);
    if (id != s_query_id) return;  /* Not our query */

    uint16_t flags   = ntohs(hdr->flags);
    uint16_t ancount = ntohs(hdr->ancount);

    /* Check QR bit (bit 15): must be 1 (response) */
    if (!(flags & 0x8000)) return;

    /* RCODE (bits 3..0): 0 = no error */
    if ((flags & 0x000F) != 0) {
        kprintf("[dns] Error response: RCODE=%u\n", flags & 0xF);
        return;
    }
    if (ancount == 0) {
        kprintf("[dns] No answers in response.\n");
        return;
    }

    /* Skip past the question section - we need to parse it to find the answer */
    uint32_t pos = sizeof(dns_header_t);

    /* Skip the question section (qdcount questions) */
    uint16_t qdcount = ntohs(hdr->qdcount);
    for (uint16_t q = 0; q < qdcount; q++) {
        /* Skip the QNAME (sequence of labels ending in 0) */
        while (pos < len) {
            uint8_t label_len = data[pos++];
            if (label_len == 0) break;
            /* Handle compression pointer (top 2 bits = 11) */
            if ((label_len & 0xC0) == 0xC0) { pos++; break; }
            pos += label_len;
        }
        pos += 4; /* QTYPE + QCLASS */
    }

    /* Parse answer records */
    for (uint16_t i = 0; i < ancount && pos < len; i++) {
        /* Skip NAME (could be a compression pointer) */
        if ((data[pos] & 0xC0) == 0xC0) {
            pos += 2;
        } else {
            while (pos < len && data[pos] != 0) pos += data[pos] + 1;
            pos++; /* null terminator */
        }

        if (pos + 10 > len) break;
        uint16_t rtype  = (data[pos] << 8) | data[pos+1]; pos += 2;
        /*uint16_t rclass =*/ pos += 2; /* class */
        /*uint32_t ttl    =*/ pos += 4; /* ttl */
        uint16_t rdlen  = (data[pos] << 8) | data[pos+1]; pos += 2;

        if (rtype == 1 && rdlen == 4 && pos + 4 <= len) {
            /* A record found! */
            s_resolved_ip.ip[0] = data[pos];
            s_resolved_ip.ip[1] = data[pos+1];
            s_resolved_ip.ip[2] = data[pos+2];
            s_resolved_ip.ip[3] = data[pos+3];
            s_resolved = true;
            kprintf("[dns] Resolved: %d.%d.%d.%d\n",
                    s_resolved_ip.ip[0], s_resolved_ip.ip[1],
                    s_resolved_ip.ip[2], s_resolved_ip.ip[3]);
            pos += rdlen;
            return;
        }
        pos += rdlen;
    }
}

/* ---- Public: blocking DNS resolve --------------------------------------- */
extern uint64_t pit_ticks(void);

bool dns_resolve(net_device_t *dev, const char *hostname, ip4_addr_t *out_ip) {
    if (!hostname || hostname[0] == 0) return false;

    for (int attempt = 0; attempt < 3; attempt++) {
        s_resolved = false;
        dns_send_query(dev, hostname);

        /* Várakozás valódi óra alapján: 2 másodperc a 100 Hz-es PIT-tel. */
        uint64_t start_tick = pit_ticks();

        while (pit_ticks() - start_tick < DNS_TIMEOUT_TICKS) {
            if (s_resolved) break;
            __asm__ volatile("pause");
        }

        if (s_resolved) {
            *out_ip = s_resolved_ip;
            return true;
        }
        kprintf("[dns] Attempt %d failed (timeout)...\n", attempt + 1);
    }

    kprintf("[dns] DNS resolution failed for '%s'\n", hostname);
    return false;
}
