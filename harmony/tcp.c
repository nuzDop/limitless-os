/*
 * TCP Protocol Implementation
 * Transmission Control Protocol for Harmony
 */

#include "harmony_net.h"
#include "tcp.h"
#include "../continuum/flux_memory.h"

// =============================================================================
// Global TCP State
// =============================================================================

static tcp_connection_t* g_tcp_connections;
static uint16_t g_tcp_port_counter = PORT_EPHEMERAL_MIN;
static spinlock_t g_tcp_lock = SPINLOCK_INIT;

// =============================================================================
// TCP Checksum Calculation
// =============================================================================

static uint16_t tcp_checksum(ipv4_header_t* ip_hdr, tcp_header_t* tcp_hdr,
                             void* data, size_t data_len) {
    // TCP pseudo-header
    struct {
        uint32_t src_addr;
        uint32_t dest_addr;
        uint8_t zero;
        uint8_t protocol;
        uint16_t tcp_length;
    } __attribute__((packed)) pseudo_header;
    
    pseudo_header.src_addr = ip_hdr->src_addr;
    pseudo_header.dest_addr = ip_hdr->dest_addr;
    pseudo_header.zero = 0;
    pseudo_header.protocol = IPPROTO_TCP;
    pseudo_header.tcp_length = htons(sizeof(tcp_header_t) + data_len);
    
    // Calculate checksum
    uint32_t sum = 0;
    uint16_t* ptr;
    
    // Add pseudo-header
    ptr = (uint16_t*)&pseudo_header;
    for (size_t i = 0; i < sizeof(pseudo_header) / 2; i++) {
        sum += ntohs(ptr[i]);
    }
    
    // Add TCP header
    tcp_hdr->checksum = 0;
    ptr = (uint16_t*)tcp_hdr;
    for (size_t i = 0; i < sizeof(tcp_header_t) / 2; i++) {
        sum += ntohs(ptr[i]);
    }
    
    // Add data
    if (data && data_len > 0) {
        ptr = (uint16_t*)data;
        for (size_t i = 0; i < data_len / 2; i++) {
            sum += ntohs(ptr[i]);
        }
        
        // Handle odd byte
        if (data_len & 1) {
            sum += ((uint8_t*)data)[data_len - 1] << 8;
        }
    }
    
    // Fold 32-bit sum to 16 bits
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return ~sum;
}

// =============================================================================
// TCP Segment Creation
// =============================================================================

static tcp_segment_t* tcp_create_segment(tcp_connection_t* conn, uint8_t flags,
                                         void* data, size_t data_len) {
    size_t segment_size = sizeof(tcp_segment_t) + data_len;
    tcp_segment_t* segment = flux_allocate(NULL, segment_size, FLUX_ALLOC_KERNEL);
    if (!segment) {
        return NULL;
    }
    
    // Build TCP header
    segment->tcp_header.src_port = htons(conn->local_port);
    segment->tcp_header.dest_port = htons(conn->remote_port);
    segment->tcp_header.seq_num = htonl(conn->send_seq);
    segment->tcp_header.ack_num = htonl(conn->recv_ack);
    segment->tcp_header.data_offset = (sizeof(tcp_header_t) / 4) << 4;
    segment->tcp_header.flags = flags;
    segment->tcp_header.window = htons(conn->recv_window);
    segment->tcp_header.checksum = 0;
    segment->tcp_header.urgent_ptr = 0;
    
    // Copy data
    if (data && data_len > 0) {
        memcpy(segment->data, data, data_len);
    }
    
    segment->data_len = data_len;
    segment->timestamp = harmony_get_time();
    segment->retransmissions = 0;
    segment->next = NULL;
    
    return segment;
}

// =============================================================================
// TCP State Machine
// =============================================================================

static void tcp_set_state(tcp_connection_t* conn, uint8_t new_state) {
    conn->state = new_state;
    
    // Notify socket layer
    if (conn->socket) {
        switch (new_state) {
            case TCP_ESTABLISHED:
                if (conn->socket->on_connect) {
                    conn->socket->on_connect(conn->socket);
                }
                break;
                
            case TCP_CLOSED:
                if (conn->socket->on_close) {
                    conn->socket->on_close(conn->socket);
                }
                break;
        }
    }
}

static void tcp_handle_syn(tcp_connection_t* conn, tcp_header_t* tcp_hdr) {
    if (conn->state == TCP_LISTEN) {
        // Passive open - received SYN
        conn->recv_seq = ntohl(tcp_hdr->seq_num);
        conn->recv_ack = conn->recv_seq + 1;
        
        // Send SYN-ACK
        tcp_segment_t* syn_ack = tcp_create_segment(conn, TCP_FLAG_SYN | TCP_FLAG_ACK,
                                                    NULL, 0);
        if (syn_ack) {
            tcp_send_segment(conn, syn_ack);
            tcp_set_state(conn, TCP_SYN_RECV);
        }
    }
}

static void tcp_handle_ack(tcp_connection_t* conn, tcp_header_t* tcp_hdr) {
    uint32_t ack_num = ntohl(tcp_hdr->ack_num);
    
    switch (conn->state) {
        case TCP_SYN_SENT:
            if (tcp_hdr->flags & TCP_FLAG_SYN) {
                // Received SYN-ACK
                conn->recv_seq = ntohl(tcp_hdr->seq_num);
                conn->recv_ack = conn->recv_seq + 1;
                conn->send_una = ack_num;
                
                // Send ACK
                tcp_segment_t* ack = tcp_create_segment(conn, TCP_FLAG_ACK, NULL, 0);
                if (ack) {
                    tcp_send_segment(conn, ack);
                    tcp_set_state(conn, TCP_ESTABLISHED);
                }
            }
            break;
            
        case TCP_SYN_RECV:
            // Received ACK of our SYN
            conn->send_una = ack_num;
            tcp_set_state(conn, TCP_ESTABLISHED);
            break;
            
        case TCP_ESTABLISHED:
            // Update send unacknowledged
            if (ack_num > conn->send_una) {
                conn->send_una = ack_num;
                
                // Remove acknowledged segments from retransmission queue
                tcp_segment_t* prev = NULL;
                tcp_segment_t* seg = conn->retrans_queue;
                
                while (seg) {
                    if (ntohl(seg->tcp_header.seq_num) < ack_num) {
                        // Segment acknowledged
                        tcp_segment_t* next = seg->next;
                        
                        if (prev) {
                            prev->next = next;
                        } else {
                            conn->retrans_queue = next;
                        }
                        
                        flux_free(seg);
                        seg = next;
                    } else {
                        prev = seg;
                        seg = seg->next;
                    }
                }
            }
            break;
            
        case TCP_FIN_WAIT1:
            conn->send_una = ack_num;
            tcp_set_state(conn, TCP_FIN_WAIT2);
            break;
            
        case TCP_CLOSING:
            conn->send_una = ack_num;
            tcp_set_state(conn, TCP_TIME_WAIT);
            // Start TIME_WAIT timer
            conn->time_wait_timer = harmony_get_time() + TCP_TIME_WAIT_DURATION;
            break;
            
        case TCP_LAST_ACK:
            conn->send_una = ack_num;
            tcp_set_state(conn, TCP_CLOSED);
            break;
    }
}

static void tcp_handle_fin(tcp_connection_t* conn, tcp_header_t* tcp_hdr) {
    // Update receive sequence
    conn->recv_ack = ntohl(tcp_hdr->seq_num) + 1;
    
    switch (conn->state) {
        case TCP_ESTABLISHED:
            // Send ACK
            tcp_segment_t* ack = tcp_create_segment(conn, TCP_FLAG_ACK, NULL, 0);
            if (ack) {
                tcp_send_segment(conn, ack);
            }
            tcp_set_state(conn, TCP_CLOSE_WAIT);
            break;
            
        case TCP_FIN_WAIT1:
            // Simultaneous close
            tcp_segment_t* ack2 = tcp_create_segment(conn, TCP_FLAG_ACK, NULL, 0);
            if (ack2) {
                tcp_send_segment(conn, ack2);
            }
            tcp_set_state(conn, TCP_CLOSING);
            break;
            
        case TCP_FIN_WAIT2:
            // Normal close
            tcp_segment_t* ack3 = tcp_create_segment(conn, TCP_FLAG_ACK, NULL, 0);
            if (ack3) {
                tcp_send_segment(conn, ack3);
            }
            tcp_set_state(conn, TCP_TIME_WAIT);
            conn->time_wait_timer = harmony_get_time() + TCP_TIME_WAIT_DURATION;
            break;
    }
}

// =============================================================================
// TCP Input Processing
// =============================================================================

void tcp_input(network_interface_t* iface, ipv4_header_t* ip_hdr,
              tcp_header_t* tcp_hdr, void* data, size_t data_len) {
    // Verify checksum
    uint16_t checksum = tcp_checksum(ip_hdr, tcp_hdr, data, data_len);
    if (checksum != 0) {
        // Invalid checksum
        return;
    }
    
    // Find connection
    uint16_t src_port = ntohs(tcp_hdr->src_port);
    uint16_t dest_port = ntohs(tcp_hdr->dest_port);
    uint32_t src_addr = ntohl(ip_hdr->src_addr);
    uint32_t dest_addr = ntohl(ip_hdr->dest_addr);
    
    tcp_connection_t* conn = tcp_find_connection(src_addr, src_port,
                                                 dest_addr, dest_port);
    
    if (!conn && (tcp_hdr->flags & TCP_FLAG_SYN)) {
        // Check for listening socket
        conn = tcp_find_listener(dest_port);
        if (conn) {
            // Create new connection for incoming SYN
            tcp_connection_t* new_conn = tcp_create_connection();
            if (new_conn) {
                new_conn->local_addr = dest_addr;
                new_conn->local_port = dest_port;
                new_conn->remote_addr = src_addr;
                new_conn->remote_port = src_port;
                new_conn->state = TCP_LISTEN;
                new_conn->socket = conn->socket;
                conn = new_conn;
            }
        }
    }
    
    if (!conn) {
        // No connection found - send RST if not RST
        if (!(tcp_hdr->flags & TCP_FLAG_RST)) {
            tcp_send_rst(iface, ip_hdr, tcp_hdr);
        }
        return;
    }
    
    spinlock_acquire(&conn->lock);
    
    // Process based on flags
    if (tcp_hdr->flags & TCP_FLAG_RST) {
        // Connection reset
        tcp_set_state(conn, TCP_CLOSED);
    } else if (tcp_hdr->flags & TCP_FLAG_SYN) {
        tcp_handle_syn(conn, tcp_hdr);
    } else if (tcp_hdr->flags & TCP_FLAG_FIN) {
        tcp_handle_fin(conn, tcp_hdr);
    }
    
    if (tcp_hdr->flags & TCP_FLAG_ACK) {
        tcp_handle_ack(conn, tcp_hdr);
    }
    
    // Process data
    if (data_len > 0 && conn->state == TCP_ESTABLISHED) {
        // Add to receive buffer
        if (conn->recv_buffer_used + data_len <= conn->recv_buffer_size) {
            memcpy(conn->recv_buffer + conn->recv_buffer_used, data, data_len);
            conn->recv_buffer_used += data_len;
            conn->recv_ack += data_len;
            
            // Send ACK
            tcp_segment_t* ack = tcp_create_segment(conn, TCP_FLAG_ACK, NULL, 0);
            if (ack) {
                tcp_send_segment(conn, ack);
            }
            
            // Notify socket
            if (conn->socket && conn->socket->on_data) {
                conn->socket->on_data(conn->socket, data, data_len);
            }
        }
    }
    
    spinlock_release(&conn->lock);
}

// =============================================================================
// TCP Connection Management
// =============================================================================

tcp_connection_t* tcp_create_connection(void) {
    tcp_connection_t* conn = flux_allocate(NULL, sizeof(tcp_connection_t),
                                          FLUX_ALLOC_KERNEL | FLUX_ALLOC_ZERO);
    if (!conn) {
        return NULL;
    }
    
    // Initialize connection
    conn->state = TCP_CLOSED;
    conn->send_seq = harmony_random() & 0x7FFFFFFF;  // Random ISN
    conn->recv_window = TCP_DEFAULT_WINDOW;
    conn->send_window = TCP_DEFAULT_WINDOW;
    conn->mss = TCP_DEFAULT_MSS;
    spinlock_init(&conn->lock);
    
    // Allocate buffers
    conn->recv_buffer_size = TCP_RECV_BUFFER_SIZE;
    conn->recv_buffer = flux_allocate(NULL, conn->recv_buffer_size,
                                     FLUX_ALLOC_KERNEL);
    
    conn->send_buffer_size = TCP_SEND_BUFFER_SIZE;
    conn->send_buffer = flux_allocate(NULL, conn->send_buffer_size,
                                     FLUX_ALLOC_KERNEL);
    
    // Add to connection list
    spinlock_acquire(&g_tcp_lock);
    conn->next = g_tcp_connections;
    g_tcp_connections = conn;
    spinlock_release(&g_tcp_lock);
    
    return conn;
}

// =============================================================================
// Socket Interface
// =============================================================================

int tcp_connect(socket_t* sock, uint32_t dest_addr, uint16_t dest_port) {
    tcp_connection_t* conn = tcp_create_connection();
    if (!conn) {
        return -1;
    }
    
    conn->socket = sock;
    conn->local_addr = sock->local_addr.ipv4.addr;
    conn->local_port = tcp_allocate_port();
    conn->remote_addr = dest_addr;
    conn->remote_port = dest_port;
    
    // Send SYN
    tcp_segment_t* syn = tcp_create_segment(conn, TCP_FLAG_SYN, NULL, 0);
    if (!syn) {
        tcp_destroy_connection(conn);
        return -1;
    }
    
    tcp_send_segment(conn, syn);
    tcp_set_state(conn, TCP_SYN_SENT);
    
    sock->state = TCP_SYN_SENT;
    
    return 0;
}

int tcp_listen(socket_t* sock, int backlog) {
    tcp_connection_t* conn = tcp_create_connection();
    if (!conn) {
        return -1;
    }
    
    conn->socket = sock;
    conn->local_addr = sock->local_addr.ipv4.addr;
    conn->local_port = sock->local_addr.ipv4.port;
    conn->state = TCP_LISTEN;
    conn->backlog = backlog;
    
    sock->state = TCP_LISTEN;
    
    return 0;
}

int tcp_send(socket_t* sock, void* data, size_t len) {
    tcp_connection_t* conn = tcp_find_socket_connection(sock);
    if (!conn || conn->state != TCP_ESTABLISHED) {
        return -1;
    }
    
    // Create segments for data
    size_t sent = 0;
    while (sent < len) {
        size_t segment_len = (len - sent > conn->mss) ? conn->mss : (len - sent);
        
        tcp_segment_t* segment = tcp_create_segment(conn, TCP_FLAG_ACK | TCP_FLAG_PSH,
                                                   (uint8_t*)data + sent, segment_len);
        if (!segment) {
            return sent;
        }
        
        tcp_send_segment(conn, segment);
        
        conn->send_seq += segment_len;
        sent += segment_len;
    }
    
    return sent;
}

int tcp_close(socket_t* sock) {
    tcp_connection_t* conn = tcp_find_socket_connection(sock);
    if (!conn) {
        return -1;
    }
    
    switch (conn->state) {
        case TCP_ESTABLISHED:
            // Send FIN
            tcp_segment_t* fin = tcp_create_segment(conn, TCP_FLAG_FIN | TCP_FLAG_ACK,
                                                   NULL, 0);
            if (fin) {
                tcp_send_segment(conn, fin);
                tcp_set_state(conn, TCP_FIN_WAIT1);
            }
            break;
            
        case TCP_CLOSE_WAIT:
            // Send FIN
            tcp_segment_t* fin2 = tcp_create_segment(conn, TCP_FLAG_FIN | TCP_FLAG_ACK,
                                                    NULL, 0);
            if (fin2) {
                tcp_send_segment(conn, fin2);
                tcp_set_state(conn, TCP_LAST_ACK);
            }
            break;
            
        default:
            tcp_set_state(conn, TCP_CLOSED);
            break;
    }
    
    return 0;
}
