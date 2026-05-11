#include <rexos/net.h>
#include <lib/printf.h>
#include <lib/string.h>

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68

static uint32_t s_dhcp_xid = 0x12345678;

void dhcp_discover(net_device_t *dev) {
    dhcp_packet_t pkt;
    kmemset(&pkt, 0, sizeof(pkt));

    pkt.op = 1; /* Request */
    pkt.htype = 1; /* Ethernet */
    pkt.hlen = 6;
    pkt.xid = htonl(s_dhcp_xid);
    pkt.magic = htonl(0x63825363);  /* RFC 2131: 99.130.83.99 */
    kmemcpy(pkt.chaddr, dev->mac.mac, 6);

    uint8_t *opt = pkt.options;
    *opt++ = 53; *opt++ = 1; *opt++ = DHCP_MSG_DISCOVER;
    *opt++ = 255; /* End */

    ip4_addr_t broadcast = { {255, 255, 255, 255} };
    kprintf("[dhcp] Sending DISCOVER...\n");
    udp_send(dev, &broadcast, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, &pkt, sizeof(pkt));
}

static void dhcp_send_request(net_device_t *dev, ip4_addr_t offered_ip) {
    dhcp_packet_t pkt;
    kmemset(&pkt, 0, sizeof(pkt));

    pkt.op = 1;
    pkt.htype = 1;
    pkt.hlen = 6;
    pkt.xid = htonl(s_dhcp_xid);
    pkt.magic = htonl(0x63825363);  /* RFC 2131: 99.130.83.99 */
    kmemcpy(pkt.chaddr, dev->mac.mac, 6);

    uint8_t *opt = pkt.options;
    *opt++ = 53; *opt++ = 1; *opt++ = DHCP_MSG_REQUEST;
    /* Requested IP */
    *opt++ = 50; *opt++ = 4; 
    kmemcpy(opt, offered_ip.ip, 4); opt += 4;
    *opt++ = 255;

    ip4_addr_t broadcast = { {255, 255, 255, 255} };
    kprintf("[dhcp] Sending REQUEST for %d.%d.%d.%d...\n", 
            offered_ip.ip[0], offered_ip.ip[1], offered_ip.ip[2], offered_ip.ip[3]);
    udp_send(dev, &broadcast, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, &pkt, sizeof(pkt));
}

void dhcp_receive(net_device_t *dev, const uint8_t *data, uint32_t len) {
    if (len < sizeof(dhcp_packet_t)) {
        kprintf("[dhcp] Packet too short: %u < %u\n", len, sizeof(dhcp_packet_t));
        return;
    }
    dhcp_packet_t *pkt = (dhcp_packet_t *)data;

    if (pkt->op != 2) return; /* Not a reply */
    if (ntohl(pkt->xid) != s_dhcp_xid) {
        kprintf("[dhcp] XID mismatch: expected %08x, got %08x\n", s_dhcp_xid, ntohl(pkt->xid));
        return;
    }

    /* Parse options */
    uint8_t msg_type = 0;
    ip4_addr_t mask = {{0,0,0,0}}, gw = {{0,0,0,0}};

    uint8_t *opt = pkt->options;
    while (opt < data + len && *opt != 255) {
        uint8_t type = *opt++;
        if (type == 0) continue; /* Padding */
        uint8_t l = *opt++;
        if (opt + l > data + len) break; /* Overrun */

        if (type == 53) msg_type = *opt;
        else if (type == 1 && l == 4) kmemcpy(mask.ip, opt, 4);
        else if (type == 3 && l == 4) kmemcpy(gw.ip, opt, 4);
        opt += l;
    }

    if (msg_type == DHCP_MSG_OFFER) {
        kprintf("[dhcp] Received OFFER: %d.%d.%d.%d\n", 
                pkt->yiaddr.ip[0], pkt->yiaddr.ip[1], pkt->yiaddr.ip[2], pkt->yiaddr.ip[3]);
        dhcp_send_request(dev, pkt->yiaddr);
    } else if (msg_type == DHCP_MSG_ACK) {
        dev->ip = pkt->yiaddr;
        dev->netmask = mask;
        dev->gateway = gw;
        kprintf("[dhcp] Configured: IP=%d.%d.%d.%d, GW=%d.%d.%d.%d\n",
                dev->ip.ip[0], dev->ip.ip[1], dev->ip.ip[2], dev->ip.ip[3],
                dev->gateway.ip[0], dev->gateway.ip[1], dev->gateway.ip[2], dev->gateway.ip[3]);
    }
}
