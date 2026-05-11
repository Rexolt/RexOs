#include <rexos/net.h>
#include <lib/printf.h>
#include <lib/string.h>

void ipv4_receive(net_device_t *dev, const ip4_header_t *ip, uint32_t len) {
    if (ip->version != 4) return;
    
    uint8_t header_len = ip->ihl * 4;
    if (len < header_len) return;
    
    /* Calculate checksum to verify */
    uint16_t csum = net_checksum(ip, header_len);
    if (csum != 0) {
        /* Drop bad packet */
        return;
    }
    
    /* Is it for us? Accept limited broadcast too: DHCP replies arrive before
     * the interface owns an address, so they target 255.255.255.255. */
    bool is_broadcast = ip->dest_ip.ip[0] == 255 && ip->dest_ip.ip[1] == 255 &&
                        ip->dest_ip.ip[2] == 255 && ip->dest_ip.ip[3] == 255;
    if (!is_broadcast && kmemcmp(ip->dest_ip.ip, dev->ip.ip, 4) != 0) {
        /* For now, drop packets not destined to us (no routing yet). */
        return;
    }
    
    uint32_t payload_len = ntohs(ip->length) - header_len;
    if (payload_len > len - header_len) payload_len = len - header_len;
    
    if (ip->protocol == IPV4_PROTO_ICMP) {
        icmp_receive(dev, ip, (const icmp_header_t *)ip->payload, payload_len);
    } else if (ip->protocol == IPV4_PROTO_UDP) {
        if (payload_len >= sizeof(udp_header_t)) {
            udp_receive(dev, ip, (const udp_header_t *)ip->payload, payload_len);
        }
    } else if (ip->protocol == IPV4_PROTO_TCP) {
        if (payload_len >= sizeof(tcp_header_t)) {
            tcp_receive(dev, ip, (const tcp_header_t *)ip->payload, payload_len);
        }
    }
}

static uint16_t s_ip_id = 0;

/* External ARP queue function (implemented in arp.c) */
extern void arp_queue_ipv4(net_device_t *dev, const ip4_addr_t *next_hop, const void *data, uint32_t len);

void ipv4_send(net_device_t *dev, const ip4_addr_t *dest_ip, uint8_t protocol, const void *payload, uint32_t payload_len) {
    uint32_t total_len = sizeof(ip4_header_t) + payload_len;
    uint8_t buffer[2048];
    if (total_len > sizeof(buffer)) return;
    
    ip4_header_t *ip = (ip4_header_t *)buffer;
    ip->version = 4; ip->ihl = 5; ip->tos = 0;
    ip->length = htons(total_len);
    ip->id = htons(s_ip_id++);
    ip->frag_off = 0; ip->ttl = 64;
    ip->protocol = protocol;
    ip->src_ip = dev->ip;
    ip->dest_ip = *dest_ip;
    ip->checksum = 0;
    ip->checksum = net_checksum(ip, sizeof(ip4_header_t));
    kmemcpy(ip->payload, payload, payload_len);

    /* --- Routing ---
     * Ha a célcím nincs az egy helyi alhálózaton, az átjárón (gateway) küldjük át.
     * Helyi: (dest_ip & netmask) == (my_ip & netmask) */
    bool is_limited_broadcast = dest_ip->ip[0] == 255 && dest_ip->ip[1] == 255 &&
                                dest_ip->ip[2] == 255 && dest_ip->ip[3] == 255;
    bool is_local = true;
    if (!is_limited_broadcast) {
        for (int i = 0; i < 4; i++) {
            if ((dest_ip->ip[i] & dev->netmask.ip[i]) !=
                (dev->ip.ip[i]  & dev->netmask.ip[i])) {
                is_local = false;
                break;
            }
        }
    }
    const ip4_addr_t *next_hop = is_local ? dest_ip : &dev->gateway;

    /* Broadcast check */
    bool is_bcast = true;
    for(int i=0; i<4; i++) if(dest_ip->ip[i] != 255) is_bcast = false;

    mac_addr_t dest_mac;
    if (is_bcast) {
        kmemset(dest_mac.mac, 0xFF, 6);
        eth_send(dev, &dest_mac, ETHERTYPE_IPv4, buffer, total_len);
    } else if (arp_resolve(next_hop, &dest_mac)) {
        eth_send(dev, &dest_mac, ETHERTYPE_IPv4, buffer, total_len);
    } else {
        /* Nincs MAC a cache-ben: küldjünk ARP Request-et a next_hop-ra */
        kprintf("[ipv4] ARP miss for %d.%d.%d.%d (next_hop), requesting...\n",
                next_hop->ip[0], next_hop->ip[1],
                next_hop->ip[2], next_hop->ip[3]);
        arp_queue_ipv4(dev, next_hop, buffer, total_len);
        arp_request(dev, next_hop);
    }
}

uint16_t ipv4_pseudo_checksum(const ip4_addr_t *src, const ip4_addr_t *dst, uint8_t proto, uint16_t len) {
    uint8_t pseudo[12];
    kmemcpy(pseudo + 0, src->ip, 4);
    kmemcpy(pseudo + 4, dst->ip, 4);
    pseudo[8]  = 0;
    pseudo[9]  = proto;
    pseudo[10] = len >> 8;
    pseudo[11] = len & 0xFF;
    return net_checksum(pseudo, 12);
}
