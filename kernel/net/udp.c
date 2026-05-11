#include <rexos/net.h>
#include <lib/printf.h>
#include <lib/string.h>

void udp_receive(net_device_t *dev, const ip4_header_t *ip_hdr, const udp_header_t *udp, uint32_t len) {
    (void)dev;
    if (len < sizeof(udp_header_t)) return;

    uint16_t src_port = ntohs(udp->src_port);
    uint16_t dest_port = ntohs(udp->dest_port);
    uint16_t udp_len = ntohs(udp->length);
    
    if (len < udp_len) return;
    
    uint32_t payload_len = udp_len - sizeof(udp_header_t);
    const uint8_t *payload = udp->payload;

    kprintf("[udp] Packet from %d.%d.%d.%d:%u to port %u (len: %u)\n",
            ip_hdr->src_ip.ip[0], ip_hdr->src_ip.ip[1],
            ip_hdr->src_ip.ip[2], ip_hdr->src_ip.ip[3],
            src_port, dest_port, payload_len);

    /* Route DHCP replies before the interface has a configured IPv4 address. */
    if (dest_port == 68) {
        dhcp_receive(dev, payload, payload_len, src_port);
        return;
    }

    /* Route to DNS if it's a response on our DNS query port */
    if (dest_port == 1053) {
        dns_receive(dev, payload, payload_len, src_port);
        return;
    }

    /* Később itt adhatjuk át a csomagot a porton figyelő alkalmazásnak */
    (void)payload;
}

void udp_send(net_device_t *dev, const ip4_addr_t *dest_ip, uint16_t src_port, uint16_t dest_port, const void *payload, uint32_t payload_len) {
    uint32_t total_len = sizeof(udp_header_t) + payload_len;
    uint8_t buffer[2048];
    if (total_len > sizeof(buffer)) return;
    
    udp_header_t *udp = (udp_header_t *)buffer;
    udp->src_port = htons(src_port);
    udp->dest_port = htons(dest_port);
    udp->length = htons(total_len);
    udp->checksum = 0; /* Opcionális IPv4 esetén, 0 azt jelenti, hogy nincs checksum */
    
    kmemcpy(udp->payload, payload, payload_len);
    
    /* Az IPv4 Pseudo-Header checksum kiszámítása opcionális, de ajánlott lenne. 
       Egyelőre 0-án hagyjuk az egyszerűség kedvéért. */
    
    ipv4_send(dev, dest_ip, IPV4_PROTO_UDP, buffer, total_len);
}
