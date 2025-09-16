/*
 * TCP Protocol Header
 * Transmission Control Protocol definitions
 */

#ifndef TCP_H
#define TCP_H

#include "harmony_net.h"

// =============================================================================
// TCP Constants
// =============================================================================

// TCP Flags
#define TCP_FLAG_FIN    0x01
#define TCP_FLAG_SYN    0x02
#define TCP_FLAG_RST    0x04
#define TCP_FLAG_PSH    0x08
#define TCP_FLAG_ACK    0x10
#define TCP_FLAG_URG    0x20
#define TCP_FLAG_ECE    0x40
#define TCP_FLAG_CWR    0x80

// TCP Options
#define TCP_OPT_END     0
#define TCP_OPT_NOP     1
#define TCP_OPT_MSS     2
#define TCP_OPT_WSCALE  3
#define TCP_OPT_SACK_OK 4
#define TCP_OPT_SACK    5
#define TCP_OPT_TIMESTAMP 8

// TCP Parameters
#define TCP_DEFAULT_MSS         1460
#define TCP_DEFAULT_WINDOW      65535
#define TCP_MAX_RETRANSMITS     5
#define TCP_RETRANSMIT_TIMEOUT  1000000    // 1 second in microseconds
#define TCP_TIME_WAIT_DURATION  120000000  // 2 minutes
#define TCP_KEEPALIVE_INTERVAL  7200000000 // 2 hours
#define TCP_RECV_BUFFER_SIZE    65536
#define TCP_SEND_BUFFER_SIZE    65536

// =============================================================================
// TCP Data Structures
// =============================================================================

// TCP Segment
typedef struct tcp_segment {
    tcp_header_t tcp_header;
    uint8_t* data;
    size_t data_len;
    uint64_t timestamp;
    uint32_t retransmissions;
    struct tcp_segment* next;
} tcp_segment_t;

// TCP Connection
typedef struct tcp_connection {
    uint32_t local_addr;
    uint32_t remote_addr;
    uint16_t local_port;
    uint16_t remote_port;
    
    // TCP state machine
    uint8_t state;
    
    // Sequence numbers
    uint32_t send_seq;      // Next sequence to send
    uint32_t send_una;      // Send unacknowledged
    uint32_t send_wnd;      // Send window
    uint32_t recv_seq;      // Next expected sequence
    uint32_t recv_ack;      // Receive acknowledgment
    uint32_t recv_wnd;      // Receive window
    
    // Options
    uint16_t mss;           // Maximum segment size
    uint8_t window_scale;
    bool sack_permitted;
    
    // Buffers
    uint8_t* recv_buffer;
    size_t recv_buffer_size;
    size_t recv_buffer_used;
    
    uint8_t* send_buffer;
    size_t send_buffer_size;
    size_t send_buffer_used;
    
    // Retransmission queue
    tcp_segment_t* retrans_queue;
    tcp_segment_t* unacked_segments;
    
    // Timers
    uint64_t retransmit_timer;
    uint64_t time_wait_timer;
    uint64_t keepalive_timer;
    uint64_t persist_timer;
    
    // Window management
    uint32_t send_window;
    uint32_t recv_window;
    uint32_t congestion_window;
    uint32_t ssthresh;
    
    // Round-trip time estimation
    uint32_t srtt;          // Smoothed RTT
    uint32_t rttvar;        // RTT variance
    uint32_t rto;           // Retransmission timeout
    
    // Associated socket
    socket_t* socket;
    
    // Listen backlog
    int backlog;
    struct tcp_connection** accept_queue;
    int accept_queue_head;
    int accept_queue_tail;
    
    spinlock_t lock;
    struct tcp_connection* next;
} tcp_connection_t;

// =============================================================================
// Function Prototypes
// =============================================================================

// Connection management
tcp_connection_t* tcp_create_connection(void);
void tcp_destroy_connection(tcp_connection_t* conn);
tcp_connection_t* tcp_find_connection(uint32_t src_addr, uint16_t src_port,
                                      uint32_t dest_addr, uint16_t dest_port);
tcp_connection_t* tcp_find_listener(uint16_t port);
tcp_connection_t* tcp_find_socket_connection(socket_t* sock);

// Port allocation
uint16_t tcp_allocate_port(void);
void tcp_release_port(uint16_t port);

// Segment operations
int tcp_send_segment(tcp_connection_t* conn, tcp_segment_t* segment);
void tcp_send_rst(network_interface_t* iface, ipv4_header_t* ip_hdr,
                  tcp_header_t* tcp_hdr);

// Input/Output
void tcp_input(network_interface_t* iface, ipv4_header_t* ip_hdr,
              tcp_header_t* tcp_hdr, void* data, size_t data_len);
int tcp_output(tcp_connection_t* conn);

// Socket interface
int tcp_connect(socket_t* sock, uint32_t dest_addr, uint16_t dest_port);
int tcp_listen(socket_t* sock, int backlog);
socket_t* tcp_accept(socket_t* sock);
int tcp_send(socket_t* sock, void* data, size_t len);
int tcp_recv(socket_t* sock, void* buffer, size_t len);
int tcp_close(socket_t* sock);

// Timer handling
void tcp_timer_tick(void);
void tcp_retransmit_timeout(tcp_connection_t* conn);
void tcp_keepalive_timeout(tcp_connection_t* conn);

// Helper functions
uint64_t harmony_get_time(void);
uint32_t harmony_random(void);
uint16_t htons(uint16_t hostshort);
uint32_t htonl(uint32_t hostlong);
uint16_t ntohs(uint16_t netshort);
uint32_t ntohl(uint32_t netlong);

#endif /* TCP_H */
