#include <rexos/net.h>
#include <lib/printf.h>
#include <lib/string.h>

void icmp_receive(net_device_t *dev, const ip4_header_t *ip_hdr, const icmp_header_t *icmp, uint32_t len) {
    if (len < sizeof(icmp_header_t)) return;
    
    if (icmp->type == ICMP_TYPE_ECHO_REQUEST) {
        kprintf("[icmp] Echo Request from %d.%d.%d.%d\n",
                ip_hdr->src_ip.ip[0], ip_hdr->src_ip.ip[1],
                ip_hdr->src_ip.ip[2], ip_hdr->src_ip.ip[3]);
        
        uint32_t payload_len = len - sizeof(icmp_header_t);
        icmp_send_reply(dev, &ip_hdr->src_ip, icmp->id, icmp->seq, icmp->payload, payload_len);
    }
}

void icmp_send_reply(net_device_t *dev, const ip4_addr_t *dest_ip, uint16_t id, uint16_t seq, const void *payload, uint32_t payload_len) {
    uint32_t total_len = sizeof(icmp_header_t) + payload_len;
    uint8_t buffer[2048];
    if (total_len > sizeof(buffer)) return;
    
    icmp_header_t *reply = (icmp_header_t *)buffer;
    reply->type = ICMP_TYPE_ECHO_REPLY;
    reply->code = 0;
    reply->checksum = 0;
    reply->id = id;
    reply->seq = seq;
    
    kmemcpy(reply->payload, payload, payload_len);
    
    reply->checksum = net_checksum(reply, total_len);
    
    ipv4_send(dev, dest_ip, IPV4_PROTO_ICMP, buffer, total_len);
    kprintf("[icmp] Sent Echo Reply to %d.%d.%d.%d\n", dest_ip->ip[0], dest_ip->ip[1], dest_ip->ip[2], dest_ip->ip[3]);
}
