#include <rexos/net.h>
#include <lib/printf.h>
#include <lib/string.h>

void eth_receive(net_device_t *dev, const eth_header_t *eth, uint32_t len) {
    uint16_t ethertype = ntohs(eth->ethertype);
    
    if (ethertype == ETHERTYPE_ARP) {
        if (len >= sizeof(eth_header_t) + sizeof(arp_header_t)) {
            arp_receive(dev, (const arp_header_t *)eth->payload, len - sizeof(eth_header_t));
        }
    } else if (ethertype == ETHERTYPE_IPv4) {
        if (len >= sizeof(eth_header_t) + sizeof(ip4_header_t)) {
            ipv4_receive(dev, (const ip4_header_t *)eth->payload, len - sizeof(eth_header_t));
        }
    }
}

void eth_send(net_device_t *dev, const mac_addr_t *dest, uint16_t ethertype, const void *payload, uint32_t payload_len) {
    uint32_t frame_len = sizeof(eth_header_t) + payload_len;
    
    /* TODO: Optimize this by avoiding dynamic allocation if possible */
    uint8_t buffer[2048]; 
    if (frame_len > sizeof(buffer)) return;

    eth_header_t *eth = (eth_header_t *)buffer;
    eth->dest_mac = *dest;
    eth->src_mac = dev->mac;
    eth->ethertype = htons(ethertype);
    
    kmemcpy(eth->payload, payload, payload_len);
    
    dev->send_frame(dev, buffer, frame_len);
}
