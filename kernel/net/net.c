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
    
    if (!dhcp_configure(dev)) {
        /* Static fallback matching QEMU user-mode NAT defaults. */
        dev->ip.ip[0] = 10;
        dev->ip.ip[1] = 0;
        dev->ip.ip[2] = 2;
        dev->ip.ip[3] = 15;

        dev->gateway.ip[0] = 10;
        dev->gateway.ip[1] = 0;
        dev->gateway.ip[2] = 2;
        dev->gateway.ip[3] = 2;

        dev->netmask.ip[0] = 255;
        dev->netmask.ip[1] = 255;
        dev->netmask.ip[2] = 255;
        dev->netmask.ip[3] = 0;
        kprintf("[net] DHCP failed, using static fallback for QEMU user NAT\n");
    }

    kprintf("[net] Registered device '%s' with IP %d.%d.%d.%d, GW %d.%d.%d.%d\n",
            dev->name,
            dev->ip.ip[0], dev->ip.ip[1], dev->ip.ip[2], dev->ip.ip[3],
            dev->gateway.ip[0], dev->gateway.ip[1], dev->gateway.ip[2], dev->gateway.ip[3]);
}

net_device_t *net_get_default_dev(void) {
    return s_net_devices;
}

void net_receive_frame(net_device_t *dev, const void *data, uint32_t len) {
    if (len < sizeof(eth_header_t)) return;
    
    /* Pass the raw packet to the Ethernet layer */
    eth_receive(dev, (const eth_header_t *)data, len);
}
