#include <rexos/net.h>
#include <lib/printf.h>
#include <lib/string.h>

#define ARP_CACHE_SIZE 32

typedef struct {
    ip4_addr_t ip;
    mac_addr_t mac;
    bool       valid;
} arp_entry_t;

static arp_entry_t s_arp_cache[ARP_CACHE_SIZE];

static void arp_cache_update(const ip4_addr_t *ip, const mac_addr_t *mac) {
    /* Update if exists */
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (s_arp_cache[i].valid && kmemcmp(s_arp_cache[i].ip.ip, ip->ip, 4) == 0) {
            s_arp_cache[i].mac = *mac;
            return;
        }
    }
    
    /* Find empty slot */
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!s_arp_cache[i].valid) {
            s_arp_cache[i].ip = *ip;
            s_arp_cache[i].mac = *mac;
            s_arp_cache[i].valid = true;
            return;
        }
    }
    
    /* If full, just overwrite the first one (simple FIFO-like without tracking) */
    s_arp_cache[0].ip = *ip;
    s_arp_cache[0].mac = *mac;
    s_arp_cache[0].valid = true;
}

bool arp_resolve(const ip4_addr_t *ip, mac_addr_t *out_mac) {
    /* Broadcast address is always broadcast MAC */
    if (ip->ip[0] == 255 && ip->ip[1] == 255 && ip->ip[2] == 255 && ip->ip[3] == 255) {
        for(int i=0; i<6; i++) out_mac->mac[i] = 0xFF;
        return true;
    }
    
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (s_arp_cache[i].valid && kmemcmp(s_arp_cache[i].ip.ip, ip->ip, 4) == 0) {
            *out_mac = s_arp_cache[i].mac;
            return true;
        }
    }
    return false;
}

void arp_receive(net_device_t *dev, const arp_header_t *arp, uint32_t len) {
    (void)len;
    
    if (ntohs(arp->hw_type) != ARP_HW_ETHERNET || ntohs(arp->proto_type) != ETHERTYPE_IPv4) {
        return;
    }
    
    if (arp->hw_len != 6 || arp->proto_len != 4) return;
    
    uint16_t op = ntohs(arp->op);
    
    if (op == ARP_OP_REQUEST) {
        /* Opportunistic cache update */
        arp_cache_update(&arp->src_ip, &arp->src_mac);
        
        /* Is it asking for our IP? */
        if (kmemcmp(arp->dest_ip.ip, dev->ip.ip, 4) == 0) {
            arp_reply(dev, &arp->src_mac, &arp->src_ip);
        }
    } else if (op == ARP_OP_REPLY) {
        arp_cache_update(&arp->src_ip, &arp->src_mac);
        
        kprintf("[arp] Cached: %d.%d.%d.%d -> %02x:%02x:%02x:%02x:%02x:%02x\n",
                arp->src_ip.ip[0], arp->src_ip.ip[1], arp->src_ip.ip[2], arp->src_ip.ip[3],
                arp->src_mac.mac[0], arp->src_mac.mac[1], arp->src_mac.mac[2],
                arp->src_mac.mac[3], arp->src_mac.mac[4], arp->src_mac.mac[5]);
    }
}

void arp_request(net_device_t *dev, const ip4_addr_t *target_ip) {
    arp_header_t arp;
    arp.hw_type = htons(ARP_HW_ETHERNET);
    arp.proto_type = htons(ETHERTYPE_IPv4);
    arp.hw_len = 6;
    arp.proto_len = 4;
    arp.op = htons(ARP_OP_REQUEST);
    
    arp.src_mac = dev->mac;
    arp.src_ip = dev->ip;
    
    /* Broadcast MAC */
    for (int i=0; i<6; i++) arp.dest_mac.mac[i] = 0xFF;
    arp.dest_ip = *target_ip;
    
    mac_addr_t broadcast = { {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF} };
    eth_send(dev, &broadcast, ETHERTYPE_ARP, &arp, sizeof(arp));
}

void arp_reply(net_device_t *dev, const mac_addr_t *target_mac, const ip4_addr_t *target_ip) {
    arp_header_t arp;
    arp.hw_type = htons(ARP_HW_ETHERNET);
    arp.proto_type = htons(ETHERTYPE_IPv4);
    arp.hw_len = 6;
    arp.proto_len = 4;
    arp.op = htons(ARP_OP_REPLY);
    
    arp.src_mac = dev->mac;
    arp.src_ip = dev->ip;
    arp.dest_mac = *target_mac;
    arp.dest_ip = *target_ip;
    
    eth_send(dev, target_mac, ETHERTYPE_ARP, &arp, sizeof(arp));
}
