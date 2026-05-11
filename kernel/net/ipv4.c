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
    
    /* Is it for us? */
    if (kmemcmp(ip->dest_ip.ip, dev->ip.ip, 4) != 0) {
        /* For now, drop packets not destined to us (no routing yet) */
        /* Also ignore broadcast IPs for now to keep it simple */
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

void ipv4_send(net_device_t *dev, const ip4_addr_t *dest_ip, uint8_t protocol, const void *payload, uint32_t payload_len) {
    uint32_t total_len = sizeof(ip4_header_t) + payload_len;
    
    uint8_t buffer[2048];
    if (total_len > sizeof(buffer)) return;
    
    ip4_header_t *ip = (ip4_header_t *)buffer;
    ip->version = 4;
    ip->ihl = 5;
    ip->tos = 0;
    ip->length = htons(total_len);
    ip->id = htons(s_ip_id++);
    ip->frag_off = 0;
    ip->ttl = 64;
    ip->protocol = protocol;
    ip->src_ip = dev->ip;
    ip->dest_ip = *dest_ip;
    ip->checksum = 0;
    ip->checksum = net_checksum(ip, sizeof(ip4_header_t));
    
    kmemcpy(ip->payload, payload, payload_len);
    
    mac_addr_t dest_mac;
    if (arp_resolve(dest_ip, &dest_mac)) {
        eth_send(dev, &dest_mac, ETHERTYPE_IPv4, buffer, total_len);
    } else {
        /* Not in cache! Send an ARP request. 
         * For a simple stack, we drop this packet and rely on the upper 
         * layer (TCP/Ping) to retry after the ARP reply arrives. */
        kprintf("[ipv4] MAC not known for %d.%d.%d.%d, sending ARP request...\n",
                dest_ip->ip[0], dest_ip->ip[1], dest_ip->ip[2], dest_ip->ip[3]);
        arp_request(dev, dest_ip);
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
