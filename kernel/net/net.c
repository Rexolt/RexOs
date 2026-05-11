#include <rexos/net.h>
#include <lib/printf.h>
#include <lib/string.h>

static net_device_t *s_net_devices = NULL;

uint16_t net_checksum(const void *data, uint32_t len) {
    const uint16_t *p = data;
    uint32_t sum = 0;
    while(len > 1) {
        sum += *p++;
        len -= 2;
    }
    if (len == 1) {
        sum += *(uint8_t*)p;
    }
    while(sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return ~sum;
}

void net_register_device(net_device_t *dev) {
    dev->next = s_net_devices;
    s_net_devices = dev;
    
    /* Initialize with zeros and use DHCP */
    kmemset(dev->ip.ip, 0, 4);
    kmemset(dev->gateway.ip, 0, 4);
    kmemset(dev->netmask.ip, 0, 4);
    
    kprintf("[net] Registered device '%s'. Requesting IP via DHCP...\n", dev->name);
    dhcp_discover(dev);
}

net_device_t *net_get_default_dev(void) {
    return s_net_devices;
}

void net_receive_frame(net_device_t *dev, const void *data, uint32_t len) {
    if (len < sizeof(eth_header_t)) return;
    
    /* Pass the raw packet to the Ethernet layer */
    eth_receive(dev, (const eth_header_t *)data, len);
}
