/* =============================================================================
 *  RexOS - DHCPv4 client (RFC 2131)
 *  Minimal DISCOVER/OFFER/REQUEST/ACK flow for boot-time address configuration.
 * ========================================================================== */
#include <rexos/net.h>
#include <lib/printf.h>
#include <lib/string.h>

#define DHCP_CLIENT_PORT 68
#define DHCP_SERVER_PORT 67
#define DHCP_MAGIC_COOKIE 0x63825363u
#define DHCP_HTYPE_ETHERNET 1
#define DHCP_HLEN_ETHERNET 6
#define DHCP_BOOTREQUEST 1
#define DHCP_BOOTREPLY 2
#define DHCP_FLAG_BROADCAST 0x8000

#define DHCP_OPT_PAD         0
#define DHCP_OPT_SUBNET_MASK 1
#define DHCP_OPT_ROUTER      3
#define DHCP_OPT_REQ_IP      50
#define DHCP_OPT_MSG_TYPE    53
#define DHCP_OPT_SERVER_ID   54
#define DHCP_OPT_PARAM_REQ   55
#define DHCP_OPT_END         255

#define DHCPDISCOVER 1
#define DHCPOFFER    2
#define DHCPREQUEST  3
#define DHCPACK      5

#define PIT_HZ 100
#define DHCP_TIMEOUT_TICKS (2 * PIT_HZ)

typedef struct {
    uint8_t  op;
    uint8_t  htype;
    uint8_t  hlen;
    uint8_t  hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    ip4_addr_t ciaddr;
    ip4_addr_t yiaddr;
    ip4_addr_t siaddr;
    ip4_addr_t giaddr;
    uint8_t  chaddr[16];
    uint8_t  sname[64];
    uint8_t  file[128];
    uint32_t magic_cookie;
    uint8_t  options[];
} __packed dhcp_packet_t;

static volatile bool s_offer_seen = false;
static volatile bool s_ack_seen = false;
static uint32_t s_xid = 0x52455801; /* 'REX' + client attempt counter */
static ip4_addr_t s_offered_ip;
static ip4_addr_t s_server_id;

extern uint64_t pit_ticks(void);

static void ip4_zero(ip4_addr_t *ip) {
    ip->ip[0] = 0; ip->ip[1] = 0; ip->ip[2] = 0; ip->ip[3] = 0;
}

static bool ip4_is_zero(const ip4_addr_t *ip) {
    return ip->ip[0] == 0 && ip->ip[1] == 0 && ip->ip[2] == 0 && ip->ip[3] == 0;
}

static void dhcp_build_base(net_device_t *dev, dhcp_packet_t *pkt, uint32_t xid) {
    kmemset(pkt, 0, sizeof(*pkt));
    pkt->op = DHCP_BOOTREQUEST;
    pkt->htype = DHCP_HTYPE_ETHERNET;
    pkt->hlen = DHCP_HLEN_ETHERNET;
    pkt->xid = htonl(xid);
    pkt->flags = htons(DHCP_FLAG_BROADCAST);
    for (int i = 0; i < 6; i++) pkt->chaddr[i] = dev->mac.mac[i];
    pkt->magic_cookie = htonl(DHCP_MAGIC_COOKIE);
}

static void dhcp_send(net_device_t *dev, uint8_t msg_type, const ip4_addr_t *req_ip,
                      const ip4_addr_t *server_id, uint32_t xid) {
    uint8_t buffer[548];
    dhcp_packet_t *pkt = (dhcp_packet_t *)buffer;
    dhcp_build_base(dev, pkt, xid);

    uint32_t pos = sizeof(dhcp_packet_t);
    buffer[pos++] = DHCP_OPT_MSG_TYPE; buffer[pos++] = 1; buffer[pos++] = msg_type;

    if (req_ip) {
        buffer[pos++] = DHCP_OPT_REQ_IP; buffer[pos++] = 4;
        kmemcpy(buffer + pos, req_ip->ip, 4); pos += 4;
    }
    if (server_id && !ip4_is_zero(server_id)) {
        buffer[pos++] = DHCP_OPT_SERVER_ID; buffer[pos++] = 4;
        kmemcpy(buffer + pos, server_id->ip, 4); pos += 4;
    }

    buffer[pos++] = DHCP_OPT_PARAM_REQ; buffer[pos++] = 3;
    buffer[pos++] = DHCP_OPT_SUBNET_MASK;
    buffer[pos++] = DHCP_OPT_ROUTER;
    buffer[pos++] = 6; /* DNS server; parsed later when resolver becomes configurable. */
    buffer[pos++] = DHCP_OPT_END;

    ip4_addr_t broadcast = { {255, 255, 255, 255} };
    udp_send(dev, &broadcast, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, buffer, pos);
}

static void dhcp_parse_options(const uint8_t *opts, uint32_t len, uint8_t *msg_type,
                               ip4_addr_t *server_id, ip4_addr_t *netmask,
                               ip4_addr_t *gateway) {
    uint32_t pos = 0;
    while (pos < len) {
        uint8_t opt = opts[pos++];
        if (opt == DHCP_OPT_END) break;
        if (opt == DHCP_OPT_PAD) continue;
        if (pos >= len) break;
        uint8_t opt_len = opts[pos++];
        if (pos + opt_len > len) break;

        if (opt == DHCP_OPT_MSG_TYPE && opt_len == 1) {
            *msg_type = opts[pos];
        } else if (opt == DHCP_OPT_SERVER_ID && opt_len >= 4) {
            kmemcpy(server_id->ip, opts + pos, 4);
        } else if (opt == DHCP_OPT_SUBNET_MASK && opt_len >= 4) {
            kmemcpy(netmask->ip, opts + pos, 4);
        } else if (opt == DHCP_OPT_ROUTER && opt_len >= 4) {
            kmemcpy(gateway->ip, opts + pos, 4);
        }
        pos += opt_len;
    }
}

void dhcp_receive(net_device_t *dev, const uint8_t *data, uint32_t len, uint16_t src_port) {
    if (src_port != DHCP_SERVER_PORT || len < sizeof(dhcp_packet_t)) return;

    const dhcp_packet_t *pkt = (const dhcp_packet_t *)data;
    if (pkt->op != DHCP_BOOTREPLY || pkt->htype != DHCP_HTYPE_ETHERNET ||
        pkt->hlen != DHCP_HLEN_ETHERNET) return;
    if (ntohl(pkt->xid) != s_xid) return;
    if (ntohl(pkt->magic_cookie) != DHCP_MAGIC_COOKIE) return;
    if (kmemcmp(pkt->chaddr, dev->mac.mac, 6) != 0) return;

    uint8_t msg_type = 0;
    ip4_addr_t server_id; ip4_zero(&server_id);
    ip4_addr_t netmask;   ip4_zero(&netmask);
    ip4_addr_t gateway;   ip4_zero(&gateway);
    dhcp_parse_options(pkt->options, len - sizeof(dhcp_packet_t), &msg_type,
                       &server_id, &netmask, &gateway);

    if (msg_type == DHCPOFFER) {
        s_offered_ip = pkt->yiaddr;
        s_server_id = server_id;
        s_offer_seen = true;
        kprintf("[dhcp] OFFER %d.%d.%d.%d\n",
                s_offered_ip.ip[0], s_offered_ip.ip[1], s_offered_ip.ip[2], s_offered_ip.ip[3]);
    } else if (msg_type == DHCPACK) {
        dev->ip = pkt->yiaddr;
        if (!ip4_is_zero(&netmask)) dev->netmask = netmask;
        if (!ip4_is_zero(&gateway)) dev->gateway = gateway;
        s_ack_seen = true;
        kprintf("[dhcp] ACK IP %d.%d.%d.%d GW %d.%d.%d.%d MASK %d.%d.%d.%d\n",
                dev->ip.ip[0], dev->ip.ip[1], dev->ip.ip[2], dev->ip.ip[3],
                dev->gateway.ip[0], dev->gateway.ip[1], dev->gateway.ip[2], dev->gateway.ip[3],
                dev->netmask.ip[0], dev->netmask.ip[1], dev->netmask.ip[2], dev->netmask.ip[3]);
    }
}

bool dhcp_configure(net_device_t *dev) {
    ip4_zero(&dev->ip);
    ip4_zero(&dev->gateway);
    dev->netmask.ip[0] = 255; dev->netmask.ip[1] = 255; dev->netmask.ip[2] = 255; dev->netmask.ip[3] = 0;

    for (int attempt = 0; attempt < 3; attempt++) {
        s_xid++;
        s_offer_seen = false;
        s_ack_seen = false;
        ip4_zero(&s_offered_ip);
        ip4_zero(&s_server_id);

        kprintf("[dhcp] DISCOVER (xid=0x%x)\n", s_xid);
        dhcp_send(dev, DHCPDISCOVER, NULL, NULL, s_xid);

        uint64_t start = pit_ticks();
        while (!s_offer_seen && pit_ticks() - start < DHCP_TIMEOUT_TICKS) {
            __asm__ volatile("pause");
        }
        if (!s_offer_seen) continue;

        kprintf("[dhcp] REQUEST %d.%d.%d.%d\n",
                s_offered_ip.ip[0], s_offered_ip.ip[1], s_offered_ip.ip[2], s_offered_ip.ip[3]);
        dhcp_send(dev, DHCPREQUEST, &s_offered_ip, &s_server_id, s_xid);

        start = pit_ticks();
        while (!s_ack_seen && pit_ticks() - start < DHCP_TIMEOUT_TICKS) {
            __asm__ volatile("pause");
        }
        if (s_ack_seen) return true;
    }

    kprintf("[dhcp] no lease received\n");
    return false;
}
