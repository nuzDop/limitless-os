/*
 * Harmony Networking Stack
 * Core networking definitions and structures
 */

#ifndef HARMONY_NET_H
#define HARMONY_NET_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// =============================================================================
// Network Constants
// =============================================================================

// Ethernet
#define ETH_ALEN            6
#define ETH_HLEN            14
#define ETH_FRAME_LEN       1514
#define ETH_MTU             1500
#define ETH_MIN_FRAME       60

// Ethernet Types
#define ETH_P_IP            0x0800
#define ETH_P_ARP           0x0806
#define ETH_P_IPV6          0x86DD
#define ETH_P_VLAN          0x8100

// IP Protocol Numbers
#define IPPROTO_ICMP        1
#define IPPROTO_TCP         6
#define IPPROTO_UDP         17
#define IPPROTO_ICMPV6      58

// Port Ranges
#define PORT_EPHEMERAL_MIN  49152
#define PORT_EPHEMERAL_MAX  65535
#define PORT_RESERVED_MAX   1024

// Socket Types
#define SOCK_STREAM         1
#define SOCK_DGRAM          2
#define SOCK_RAW            3

// Socket Families
#define AF_UNSPEC           0
#define AF_INET             2
#define AF_INET6            10
#define AF_PACKET           17

// Socket Options
#define SOL_SOCKET          1
#define SO_REUSEADDR        2
#define SO_KEEPALIVE        9
#define SO_BROADCAST        6
#define SO_SNDBUF           7
#define SO_RCVBUF           8
#define SO_RCVTIMEO         20
#define SO_SNDTIMEO         21

// TCP States
#define TCP_CLOSED          0
#define TCP_LISTEN          1
#define TCP_SYN_SENT        2
#define TCP_SYN_RECV        3
#define TCP_ESTABLISHED     4
#define TCP_FIN_WAIT1       5
#define TCP_FIN_WAIT2       6
#define TCP_CLOSE_WAIT      7
#define TCP_CLOSING         8
#define TCP_LAST_ACK        9
#define TCP_TIME_WAIT       10

// =============================================================================
// Network Data Structures
// =============================================================================

// Ethernet Header
typedef struct __attribute__((packed)) {
    uint8_t dest[ETH_ALEN];
    uint8_t src[ETH_ALEN];
    uint16_t type;
} eth_header_t;

// IPv4 Header
typedef struct __attribute__((packed)) {
    uint8_t version_ihl;
    uint8_t tos;
    uint16_t total_length;
    uint16_t id;
    uint16_t flags_frag_offset;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t checksum;
    uint32_t src_addr;
    uint32_t dest_addr;
} ipv4_header_t;

// IPv6 Header
typedef struct __attribute__((packed)) {
    uint32_t version_class_flow;
    uint16_t payload_length;
    uint8_t next_header;
    uint8_t hop_limit;
    uint8_t src_addr[16];
    uint8_t dest_addr[16];
} ipv6_header_t;

// TCP Header
typedef struct __attribute__((packed)) {
    uint16_t src_port;
    uint16_t dest_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t data_offset;
    uint8_t flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent_ptr;
} tcp_header_t;

// UDP Header
typedef struct __attribute__((packed)) {
    uint16_t src_port;
    uint16_t dest_port;
    uint16_t length;
    uint16_t checksum;
} udp_header_t;

// ICMP Header
typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    union {
        struct {
            uint16_t id;
            uint16_t sequence;
        } echo;
        uint32_t gateway;
        struct {
            uint16_t unused;
            uint16_t mtu;
        } frag;
    };
} icmp_header_t;

// ARP Header
typedef struct __attribute__((packed)) {
    uint16_t hardware_type;
    uint16_t protocol_type;
    uint8_t hardware_len;
    uint8_t protocol_len;
    uint16_t operation;
    uint8_t sender_mac[ETH_ALEN];
    uint32_t sender_ip;
    uint8_t target_mac[ETH_ALEN];
    uint32_t target_ip;
} arp_header_t;

// Socket Address
typedef struct {
    uint16_t family;
    union {
        struct {
            uint32_t addr;
            uint16_t port;
        } ipv4;
        struct {
            uint8_t addr[16];
            uint16_t port;
            uint32_t flowinfo;
            uint32_t scope_id;
        } ipv6;
        uint8_t raw[128];
    };
} socket_addr_t;

// Network Interface
typedef struct network_interface {
    char name[16];
    uint32_t index;
    uint32_t flags;
    uint8_t mac_addr[ETH_ALEN];
    uint32_t ipv4_addr;
    uint32_t ipv4_netmask;
    uint32_t ipv4_broadcast;
    uint8_t ipv6_addr[16];
    uint8_t ipv6_prefix_len;
    uint32_t mtu;
    
    // Driver interface
    void* driver_data;
    int (*send_packet)(void* driver_data, void* data, size_t len);
    int (*receive_packet)(void* driver_data, void* buffer, size_t max_len);
    
    // Statistics
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint64_t rx_errors;
    uint64_t tx_errors;
    
    struct network_interface* next;
} network_interface_t;

// Socket Structure
typedef struct socket {
    uint32_t id;
    uint16_t family;
    uint16_t type;
    uint16_t protocol;
    uint32_t state;
    
    socket_addr_t local_addr;
    socket_addr_t remote_addr;
    
    // Buffers
    uint8_t* recv_buffer;
    size_t recv_buffer_size;
    size_t recv_buffer_used;
    
    uint8_t* send_buffer;
    size_t send_buffer_size;
    size_t send_buffer_used;
    
    // TCP specific
    uint32_t seq_num;
    uint32_t ack_num;
    uint16_t window_size;
    uint32_t tcp_state;
    
    // Options
    bool reuse_addr;
    bool keep_alive;
    bool broadcast;
    uint32_t recv_timeout;
    uint32_t send_timeout;
    
    // Callbacks
    void (*on_connect)(struct socket* sock);
    void (*on_data)(struct socket* sock, void* data, size_t len);
    void (*on_close)(struct socket* sock);
    void (*on_error)(struct socket* sock, int error);
    
    struct socket* next;
} socket_t;

// Routing Table Entry
typedef struct route_entry {
    uint32_t dest;
    uint32_t netmask;
    uint32_t gateway;
    network_interface_t* interface;
    uint32_t metric;
    uint32_t flags;
    struct route_entry* next;
} route_entry_t;

// ARP Cache Entry
typedef struct arp_entry {
    uint32_t ip_addr;
    uint8_t mac_addr[ETH_ALEN];
    uint64_t timestamp;
    bool valid;
    struct arp_entry* next;
} arp_entry_t;

#endif /* HARMONY_NET_H */
