#pragma once
#include <rexos/types.h>

/* --- Bájtsorrend (Endianness) --- */
/* A hálózat Big-Endian (Network Byte Order), az x86 Little-Endian. */
static inline uint16_t htons(uint16_t x) {
    return (x << 8) | (x >> 8);
}
static inline uint16_t ntohs(uint16_t x) {
    return htons(x);
}
static inline uint32_t htonl(uint32_t x) {
    return ((x & 0x000000FF) << 24) |
           ((x & 0x0000FF00) <<  8) |
           ((x & 0x00FF0000) >>  8) |
           ((x & 0xFF000000) >> 24);
}
static inline uint32_t ntohl(uint32_t x) {
    return htonl(x);
}

/* --- Alapvető típusok --- */
typedef struct {
    uint8_t mac[6];
} __packed mac_addr_t;

typedef struct {
    uint8_t ip[4];
} __packed ip4_addr_t;

/* --- Ethernet II Fejléc --- */
#define ETHERTYPE_IPv4 0x0800
#define ETHERTYPE_ARP  0x0806

typedef struct {
    mac_addr_t dest_mac;
    mac_addr_t src_mac;
    uint16_t   ethertype;  /* Network byte order! */
    uint8_t    payload[];
} __packed eth_header_t;

/* --- ARP Fejléc --- */
#define ARP_HW_ETHERNET 1
#define ARP_OP_REQUEST  1
#define ARP_OP_REPLY    2

typedef struct {
    uint16_t hw_type;    /* Hardware type (pl. 1 = Ethernet) */
    uint16_t proto_type; /* Protocol type (pl. 0x0800 = IPv4) */
    uint8_t  hw_len;     /* MAC cím hossza (6) */
    uint8_t  proto_len;  /* IP cím hossza (4) */
    uint16_t op;         /* Operation: 1=Request, 2=Reply */
    
    mac_addr_t src_mac;
    ip4_addr_t src_ip;
    mac_addr_t dest_mac;
    ip4_addr_t dest_ip;
} __packed arp_header_t;

/* --- IPv4 Fejléc --- */
#define IPV4_PROTO_ICMP 1
#define IPV4_PROTO_TCP  6
#define IPV4_PROTO_UDP  17

typedef struct {
    uint8_t  ihl : 4;    /* Internet Header Length */
    uint8_t  version : 4;/* Version (4) */
    uint8_t  tos;        /* Type of Service */
    uint16_t length;     /* Total Length */
    uint16_t id;         /* Identification */
    uint16_t frag_off;   /* Fragment Offset */
    uint8_t  ttl;        /* Time To Live */
    uint8_t  protocol;   /* Protocol (pl. 1=ICMP, 6=TCP, 17=UDP) */
    uint16_t checksum;   /* Header Checksum */
    ip4_addr_t src_ip;
    ip4_addr_t dest_ip;
    uint8_t  payload[];
} __packed ip4_header_t;

/* --- ICMP Fejléc --- */
#define ICMP_TYPE_ECHO_REPLY   0
#define ICMP_TYPE_ECHO_REQUEST 8

typedef struct {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    /* Type-specific adatok jönnek ide, pl Echo esetén: */
    uint16_t id;
    uint16_t seq;
    uint8_t  payload[];
} __packed icmp_header_t;

/* --- UDP Fejléc --- */
typedef struct {
    uint16_t src_port;
    uint16_t dest_port;
    uint16_t length;
    uint16_t checksum;
    uint8_t  payload[];
} __packed udp_header_t;

/* --- TCP Fejléc --- */
typedef struct {
    uint16_t src_port;
    uint16_t dest_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t  reserved : 4;
    uint8_t  data_offset : 4;
    uint8_t  flags;
    uint16_t window_size;
    uint16_t checksum;
    uint16_t urgent_ptr;
    uint8_t  options_and_payload[];
} __packed tcp_header_t;

/* TCP Flag bitek */
#define TCP_FIN  (1 << 0)
#define TCP_SYN  (1 << 1)
#define TCP_RST  (1 << 2)
#define TCP_PSH  (1 << 3)
#define TCP_ACK  (1 << 4)
#define TCP_URG  (1 << 5)

/* TCP State Machine */
typedef enum {
    TCP_CLOSED, TCP_SYN_SENT, TCP_SYN_RECEIVED,
    TCP_ESTABLISHED, TCP_FIN_WAIT_1, TCP_FIN_WAIT_2,
    TCP_CLOSE_WAIT, TCP_CLOSING, TCP_LAST_ACK, TCP_TIME_WAIT,
} tcp_state_t;

#define TCP_RX_BUF_SIZE 8192

typedef struct tcp_socket {
    tcp_state_t    state;
    ip4_addr_t     remote_ip;
    uint16_t       local_port;
    uint16_t       remote_port;
    uint32_t       seq;
    uint32_t       ack;
    uint8_t        rx_buf[TCP_RX_BUF_SIZE];
    uint32_t       rx_len;
    struct net_device *dev;
    void (*on_data)(struct tcp_socket *s, const uint8_t *d, uint32_t l);
    void (*on_connect)(struct tcp_socket *s);
    void (*on_close)(struct tcp_socket *s);
    struct tcp_socket *next;
} tcp_socket_t;

/* Hálózati Interfész (Net Device) Absztrakció */
typedef struct net_device {
    char       name[16];
    mac_addr_t mac;
    ip4_addr_t ip;
    ip4_addr_t gateway;
    ip4_addr_t netmask;
    
    /* Küldés a kártyán keresztül */
    int (*send_frame)(struct net_device *dev, const void *data, uint32_t len);
    
    struct net_device *next;
} net_device_t;

/* Segédfüggvény (net.c) */
uint16_t      net_checksum(const void *data, uint32_t len);
net_device_t *net_get_default_dev(void);

/* Globális interfész regisztráció (net.c) */
void net_register_device(net_device_t *dev);
void net_receive_frame(net_device_t *dev, const void *data, uint32_t len);

/* Ethernet (eth.c) */
void eth_receive(net_device_t *dev, const eth_header_t *eth, uint32_t len);
void eth_send(net_device_t *dev, const mac_addr_t *dest, uint16_t ethertype, const void *payload, uint32_t payload_len);

/* ARP (arp.c) */
void arp_receive(net_device_t *dev, const arp_header_t *arp, uint32_t len);
void arp_request(net_device_t *dev, const ip4_addr_t *target_ip);
void arp_reply(net_device_t *dev, const mac_addr_t *target_mac, const ip4_addr_t *target_ip);
bool arp_resolve(const ip4_addr_t *ip, mac_addr_t *out_mac);
bool arp_queue_ipv4(net_device_t *dev, const ip4_addr_t *next_hop, const void *packet, uint32_t packet_len);

/* DHCP (dhcp.c) */
bool dhcp_configure(net_device_t *dev);
void dhcp_receive(net_device_t *dev, const uint8_t *data, uint32_t len, uint16_t src_port);

/* IPv4 (ipv4.c) */
void     ipv4_receive(net_device_t *dev, const ip4_header_t *ip, uint32_t len);
void     ipv4_send(net_device_t *dev, const ip4_addr_t *dest_ip, uint8_t protocol, const void *payload, uint32_t payload_len);
uint16_t ipv4_pseudo_checksum(const ip4_addr_t *src, const ip4_addr_t *dst, uint8_t proto, uint16_t len);

/* ICMP (icmp.c) */
void icmp_receive(net_device_t *dev, const ip4_header_t *ip_hdr, const icmp_header_t *icmp, uint32_t len);
void icmp_send_reply(net_device_t *dev, const ip4_addr_t *dest_ip, uint16_t id, uint16_t seq, const void *payload, uint32_t payload_len);

/* UDP (udp.c) */
void udp_receive(net_device_t *dev, const ip4_header_t *ip_hdr, const udp_header_t *udp, uint32_t len);
void udp_send(net_device_t *dev, const ip4_addr_t *dest_ip, uint16_t src_port, uint16_t dest_port, const void *payload, uint32_t payload_len);

/* TCP (tcp.c) */
void          tcp_receive(net_device_t *dev, const ip4_header_t *ip_hdr, const tcp_header_t *tcp, uint32_t len);
tcp_socket_t *tcp_connect(net_device_t *dev, const ip4_addr_t *dest_ip, uint16_t dest_port);
void          tcp_send_data(tcp_socket_t *sock, const void *data, uint32_t len);
void          tcp_close(tcp_socket_t *sock);

/* DNS (dns.c) */
bool dns_resolve(net_device_t *dev, const char *hostname, ip4_addr_t *out_ip);
void dns_receive(net_device_t *dev, const uint8_t *data, uint32_t len, uint16_t src_port);
