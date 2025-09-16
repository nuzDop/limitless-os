/*
 * UDP Protocol Implementation
 * User Datagram Protocol for Harmony
 */

#include "harmony_net.h"
#include "udp.h"
#include "ip.h"
#include "../continuum/flux_memory.h"

// =============================================================================
// Global UDP State
// =============================================================================

static udp_socket_t* g_udp_sockets;
static uint16_t g_udp_port_counter = PORT_EPHEMERAL_MIN;
static spinlock_t g_udp_lock = SPINLOCK_INIT;

// =============================================================================
// UDP Checksum
// =============================================================================

static uint16_t udp_checksum(ipv4_header_t* ip_hdr, udp_header_t* udp_hdr,
                             void* data, size_t data_len) {
    // UDP pseudo-header
    struct {
        uint32_t src_addr;
        uint32_t dest_addr;
        uint8_t zero;
        uint8_t protocol;
        uint16_t udp_length;
    } __attribute__((packed)) pseudo_header;
    
    pseudo_header.src_addr = ip_hdr->src_addr;
    pseudo_header.dest_addr = ip_hdr->dest_addr;
    pseudo_header.zero = 0;
    pseudo_header.protocol = IPPROTO_UDP;
    pseudo_header.udp_length = udp_hdr->length;
    
    // Calculate checksum
    uint32_t sum = 0;
    uint16_t* ptr;
    
    // Add pseudo-header
    ptr = (uint16_t*)&pseudo_header;
    for (size_t i = 0; i < sizeof(pseudo_header) / 2; i++) {
        sum += ntohs(ptr[i]);
    }
    
    // Add UDP header
    udp_hdr->checksum = 0;
    ptr = (uint16_t*)udp_hdr;
    for (size_t i = 0; i < sizeof(udp_header_t) / 2; i++) {
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
// UDP Input Processing
// =============================================================================

void udp_input(network_interface_t* iface, ipv4_header_t* ip_hdr,
              udp_header_t* udp_hdr, void* data, size_t data_len) {
    // Verify checksum (optional for UDP)
    if (udp_hdr->checksum != 0) {
        uint16_t checksum = udp_checksum(ip_hdr, udp_hdr, data, data_len);
        if (checksum != 0) {
            // Invalid checksum
            iface->rx_errors++;
            return;
        }
    }
    
    uint16_t dest_port = ntohs(udp_hdr->dest_port);
    uint32_t src_addr = ntohl(ip_hdr->src_addr);
    uint16_t src_port = ntohs(udp_hdr->src_port);
    
    // Find socket
    spinlock_acquire(&g_udp_lock);
    
    udp_socket_t* sock = g_udp_sockets;
    while (sock) {
        if (sock->local_port == dest_port) {
            if (sock->local_addr == 0 || sock->local_addr == ntohl(ip_hdr->dest_addr)) {
                // Match found
                break;
            }
        }
        sock = sock->next;
    }
    
    spinlock_release(&g_udp_lock);
    
    if (!sock) {
        // No socket listening - send ICMP port unreachable
        icmp_send_port_unreachable(iface, ip_hdr);
        return;
    }
    
    // Add to socket receive queue
    spinlock_acquire(&sock->lock);
    
    udp_packet_t* packet = flux_allocate(NULL, sizeof(udp_packet_t) + data_len,
                                        FLUX_ALLOC_KERNEL);
    if (packet) {
        packet->src_addr = src_addr;
        packet->src_port = src_port;
        packet->data_len = data_len;
        memcpy(packet->data, data, data_len);
        packet->next = NULL;
        
        // Add to queue
        if (sock->recv_queue_tail) {
            sock->recv_queue_tail->next = packet;
        } else {
            sock->recv_queue_head = packet;
        }
        sock->recv_queue_tail = packet;
        sock->recv_queue_count++;
        
        // Notify socket
        if (sock->socket && sock->socket->on_data) {
            sock->socket->on_data(sock->socket, data, data_len);
        }
    }
    
    spinlock_release(&sock->lock);
}

// =============================================================================
// UDP Output
// =============================================================================

int udp_output(udp_socket_t* sock, uint32_t dest_addr, uint16_t dest_port,
              void* data, size_t data_len) {
    if (data_len > UDP_MAX_PAYLOAD) {
        return -1;
    }
    
    // Build UDP header
    udp_header_t udp_hdr;
    udp_hdr.src_port = htons(sock->local_port);
    udp_hdr.dest_port = htons(dest_port);
    udp_hdr.length = htons(sizeof(udp_header_t) + data_len);
    udp_hdr.checksum = 0;  // Optional for IPv4
    
    // Build complete packet
    size_t packet_len = sizeof(udp_header_t) + data_len;
    uint8_t* packet = flux_allocate(NULL, packet_len, FLUX_ALLOC_KERNEL);
    if (!packet) {
        return -1;
    }
    
    memcpy(packet, &udp_hdr, sizeof(udp_header_t));
    memcpy(packet + sizeof(udp_header_t), data, data_len);
    
    // Send via IP layer
    int result = ip_send(sock->local_addr ? sock->local_addr : 0,
                        dest_addr, IPPROTO_UDP, packet, packet_len);
    
    flux_free(packet);
    
    if (result == 0) {
        sock->packets_sent++;
        sock->bytes_sent += data_len;
    }
    
    return result;
}

// =============================================================================
// Socket Management
// =============================================================================

udp_socket_t* udp_create_socket(void) {
    udp_socket_t* sock = flux_allocate(NULL, sizeof(udp_socket_t),
                                      FLUX_ALLOC_KERNEL | FLUX_ALLOC_ZERO);
    if (!sock) {
        return NULL;
    }
    
    spinlock_init(&sock->lock);
    
    // Add to socket list
    spinlock_acquire(&g_udp_lock);
    sock->next = g_udp_sockets;
    g_udp_sockets = sock;
    spinlock_release(&g_udp_lock);
    
    return sock;
}

void udp_destroy_socket(udp_socket_t* sock) {
    if (!sock) {
        return;
    }
    
    // Remove from socket list
    spinlock_acquire(&g_udp_lock);
    
    udp_socket_t** prev = &g_udp_sockets;
    while (*prev) {
        if (*prev == sock) {
            *prev = sock->next;
            break;
        }
        prev = &(*prev)->next;
    }
    
    spinlock_release(&g_udp_lock);
    
    // Free receive queue
    udp_packet_t* packet = sock->recv_queue_head;
    while (packet) {
        udp_packet_t* next = packet->next;
        flux_free(packet);
        packet = next;
    }
    
    flux_free(sock);
}

// =============================================================================
// Socket Interface
// =============================================================================

int udp_bind(socket_t* sock, uint32_t addr, uint16_t port) {
    udp_socket_t* udp_sock = udp_create_socket();
    if (!udp_sock) {
        return -1;
    }
    
    // Check if port is already in use
    spinlock_acquire(&g_udp_lock);
    
    if (port != 0) {
        udp_socket_t* existing = g_udp_sockets;
        while (existing) {
            if (existing->local_port == port && 
                (existing->local_addr == addr || existing->local_addr == 0 || addr == 0)) {
                spinlock_release(&g_udp_lock);
                udp_destroy_socket(udp_sock);
                return -1;  // Port in use
            }
            existing = existing->next;
        }
    } else {
        // Allocate ephemeral port
        port = g_udp_port_counter++;
        if (g_udp_port_counter > PORT_EPHEMERAL_MAX) {
            g_udp_port_counter = PORT_EPHEMERAL_MIN;
        }
    }
    
    spinlock_release(&g_udp_lock);
    
    udp_sock->local_addr = addr;
    udp_sock->local_port = port;
    udp_sock->socket = sock;
    
    sock->local_addr.ipv4.addr = addr;
    sock->local_addr.ipv4.port = port;
    
    return 0;
}

int udp_sendto(socket_t* sock, void* data, size_t len,
              uint32_t dest_addr, uint16_t dest_port) {
    udp_socket_t* udp_sock = udp_find_socket(sock);
    if (!udp_sock) {
        // Socket not bound - auto-bind
        if (udp_bind(sock, 0, 0) != 0) {
            return -1;
        }
        udp_sock = udp_find_socket(sock);
        if (!udp_sock) {
            return -1;
        }
    }
    
    return udp_output(udp_sock, dest_addr, dest_port, data, len);
}

int udp_recvfrom(socket_t* sock, void* buffer, size_t len,
                uint32_t* src_addr, uint16_t* src_port) {
    udp_socket_t* udp_sock = udp_find_socket(sock);
    if (!udp_sock) {
        return -1;
    }
    
    spinlock_acquire(&udp_sock->lock);
    
    if (udp_sock->recv_queue_head == NULL) {
        spinlock_release(&udp_sock->lock);
        return 0;  // No data available
    }
    
    udp_packet_t* packet = udp_sock->recv_queue_head;
    udp_sock->recv_queue_head = packet->next;
    if (udp_sock->recv_queue_head == NULL) {
        udp_sock->recv_queue_tail = NULL;
    }
    udp_sock->recv_queue_count--;
    
    spinlock_release(&udp_sock->lock);
    
    // Copy data
    size_t copy_len = (packet->data_len < len) ? packet->data_len : len;
    memcpy(buffer, packet->data, copy_len);
    
    if (src_addr) {
        *src_addr = packet->src_addr;
    }
    if (src_port) {
        *src_port = packet->src_port;
    }
    
    flux_free(packet);
    
    return copy_len;
}
