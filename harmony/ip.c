/*
 * IP Layer Implementation
 * Internet Protocol v4/v6 for Harmony
 */

#include "harmony_net.h"
#include "ip.h"
#include "tcp.h"
#include "udp.h"
#include "icmp.h"
#include "arp.h"
#include "../continuum/flux_memory.h"

// =============================================================================
// Global IP State
// =============================================================================

static network_interface_t* g_interfaces;
static route_entry_t* g_routing_table;
static uint16_t g_ip_id_counter = 1;
static spinlock_t g_ip_lock = SPINLOCK_INIT;

// =============================================================================
// IP Checksum
// =============================================================================

uint16_t ip_checksum(void* data, size_t len) {
    uint32_t sum = 0;
    uint16_t* ptr = (uint16_t*)data;
    
    // Add 16-bit words
    while (len > 1) {
        sum += ntohs(*ptr++);
        len -= 2;
    }
    
    // Add odd byte if present
    if (len > 0) {
        sum += ((uint8_t*)ptr)[0] << 8;
    }
    
    // Fold 32-bit sum to 16 bits
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return ~sum;
}

// =============================================================================
// Routing
// =============================================================================

network_interface_t* ip_route_lookup(uint32_t dest_addr) {
    spinlock_acquire(&g_ip_lock);
    
    route_entry_t* best_route = NULL;
    uint32_t best_mask = 0;
    
    // Find most specific route
    route_entry_t* route = g_routing_table;
    while (route) {
        if ((dest_addr & route->netmask) == (route->dest & route->netmask)) {
            if (route->netmask >= best_mask) {
                best_mask = route->netmask;
                best_route = route;
            }
        }
        route = route->next;
    }
    
    spinlock_release(&g_ip_lock);
    
    return best_route ? best_route->interface : NULL;
}

int ip_add_route(uint32_t dest, uint32_t netmask, uint32_t gateway,
                network_interface_t* iface) {
    route_entry_t* route = flux_allocate(NULL, sizeof(route_entry_t),
                                        FLUX_ALLOC_KERNEL);
    if (!route) {
        return -1;
    }
    
    route->dest = dest;
    route->netmask = netmask;
    route->gateway = gateway;
    route->interface = iface;
    route->metric = 1;
    route->flags = 0;
    
    spinlock_acquire(&g_ip_lock);
    route->next = g_routing_table;
    g_routing_table = route;
    spinlock_release(&g_ip_lock);
    
    return 0;
}

// =============================================================================
// IP Input Processing
// =============================================================================

void ip_input(network_interface_t* iface, void* packet, size_t len) {
    if (len < sizeof(ipv4_header_t)) {
        return;
    }
    
    ipv4_header_t* ip_hdr = (ipv4_header_t*)packet;
    
    // Check version
    uint8_t version = (ip_hdr->version_ihl >> 4) & 0x0F;
    if (version == 4) {
        ip4_input(iface, ip_hdr, len);
    } else if (version == 6) {
        ip6_input(iface, (ipv6_header_t*)packet, len);
    }
}

void ip4_input(network_interface_t* iface, ipv4_header_t* ip_hdr, size_t len) {
    // Verify header length
    uint8_t ihl = ip_hdr->version_ihl & 0x0F;
    if (ihl < 5) {
        return;  // Invalid header length
    }
    
    size_t header_len = ihl * 4;
    size_t total_len = ntohs(ip_hdr->total_length);
    
    if (total_len > len || total_len < header_len) {
        return;  // Invalid packet length
    }
    
    // Verify checksum
    uint16_t checksum = ip_hdr->checksum;
    ip_hdr->checksum = 0;
    if (ip_checksum(ip_hdr, header_len) != checksum) {
        return;  // Invalid checksum
    }
    ip_hdr->checksum = checksum;
    
    // Check if packet is for us
    uint32_t dest_addr = ntohl(ip_hdr->dest_addr);
    bool for_us = false;
    
    if (dest_addr == 0xFFFFFFFF) {
        // Broadcast
        for_us = true;
    } else if ((dest_addr & 0xF0000000) == 0xE0000000) {
        // Multicast
        for_us = ip_is_multicast_member(dest_addr);
    } else {
        // Unicast - check our addresses
        network_interface_t* iface_iter = g_interfaces;
        while (iface_iter) {
            if (iface_iter->ipv4_addr == dest_addr) {
                for_us = true;
                break;
            }
            iface_iter = iface_iter->next;
        }
    }
    
    if (!for_us) {
        // Forward packet if routing enabled
        if (ip_forwarding_enabled()) {
            ip_forward(iface, ip_hdr, total_len);
        }
        return;
    }
    
    // Process based on protocol
    void* payload = (uint8_t*)ip_hdr + header_len;
    size_t payload_len = total_len - header_len;
    
    switch (ip_hdr->protocol) {
        case IPPROTO_ICMP:
            icmp_input(iface, ip_hdr, (icmp_header_t*)payload, payload_len);
            break;
            
        case IPPROTO_TCP:
            tcp_input(iface, ip_hdr, (tcp_header_t*)payload, 
                     (uint8_t*)payload + sizeof(tcp_header_t),
                     payload_len - sizeof(tcp_header_t));
            break;
            
        case IPPROTO_UDP:
            udp_input(iface, ip_hdr, (udp_header_t*)payload,
                     (uint8_t*)payload + sizeof(udp_header_t),
                     payload_len - sizeof(udp_header_t));
            break;
            
        default:
            // Unknown protocol - send ICMP protocol unreachable
            icmp_send_protocol_unreachable(iface, ip_hdr);
            break;
    }
    
    iface->rx_packets++;
    iface->rx_bytes += len;
}

// =============================================================================
// IP Output
// =============================================================================

int ip_send(uint32_t src_addr, uint32_t dest_addr, uint8_t protocol,
           void* data, size_t len) {
    // Route lookup
    network_interface_t* iface = ip_route_lookup(dest_addr);
    if (!iface) {
        return -1;  // No route to host
    }
    
    // Use interface address if no source specified
    if (src_addr == 0) {
        src_addr = iface->ipv4_addr;
    }
    
    // Fragment if necessary
    if (len + sizeof(ipv4_header_t) > iface->mtu) {
        return ip_fragment_and_send(iface, src_addr, dest_addr, protocol, data, len);
    }
    
    // Build IP header
    size_t packet_len = sizeof(ipv4_header_t) + len;
    uint8_t* packet = flux_allocate(NULL, packet_len, FLUX_ALLOC_KERNEL);
    if (!packet) {
        return -1;
    }
    
    ipv4_header_t* ip_hdr = (ipv4_header_t*)packet;
    ip_hdr->version_ihl = 0x45;  // Version 4, header length 5 (20 bytes)
    ip_hdr->tos = 0;
    ip_hdr->total_length = htons(packet_len);
    ip_hdr->id = htons(g_ip_id_counter++);
    ip_hdr->flags_frag_offset = htons(0x4000);  // Don't fragment
    ip_hdr->ttl = 64;
    ip_hdr->protocol = protocol;
    ip_hdr->checksum = 0;
    ip_hdr->src_addr = htonl(src_addr);
    ip_hdr->dest_addr = htonl(dest_addr);
    
    // Calculate checksum
    ip_hdr->checksum = ip_checksum(ip_hdr, sizeof(ipv4_header_t));
    
    // Copy payload
    memcpy(packet + sizeof(ipv4_header_t), data, len);
    
    // Send via Ethernet
    int result = ethernet_send(iface, dest_addr, ETH_P_IP, packet, packet_len);
    
    flux_free(packet);
    
    if (result == 0) {
        iface->tx_packets++;
        iface->tx_bytes += packet_len;
    }
    
    return result;
}

// =============================================================================
// Network Interface Management
// =============================================================================

network_interface_t* ip_add_interface(const char* name, void* driver_data,
                                     int (*send_fn)(void*, void*, size_t),
                                     int (*recv_fn)(void*, void*, size_t)) {
    network_interface_t* iface = flux_allocate(NULL, sizeof(network_interface_t),
                                              FLUX_ALLOC_KERNEL | FLUX_ALLOC_ZERO);
    if (!iface) {
        return NULL;
    }
    
    strncpy(iface->name, name, sizeof(iface->name) - 1);
    iface->driver_data = driver_data;
    iface->send_packet = send_fn;
    iface->receive_packet = recv_fn;
    iface->mtu = ETH_MTU;
    
    // Assign index
    static uint32_t next_index = 1;
    iface->index = next_index++;
    
    // Add to interface list
    spinlock_acquire(&g_ip_lock);
    iface->next = g_interfaces;
    g_interfaces = iface;
    spinlock_release(&g_ip_lock);
    
    return iface;
}

int ip_configure_interface(network_interface_t* iface, uint32_t ipv4_addr,
                          uint32_t netmask) {
    iface->ipv4_addr = ipv4_addr;
    iface->ipv4_netmask = netmask;
    iface->ipv4_broadcast = ipv4_addr | ~netmask;
    
    // Add route for local network
    ip_add_route(ipv4_addr & netmask, netmask, 0, iface);
    
    // Send gratuitous ARP
    arp_send_announcement(iface);
    
    return 0;
}

network_interface_t* ip_get_interface(const char* name) {
    spinlock_acquire(&g_ip_lock);
    
    network_interface_t* iface = g_interfaces;
    while (iface) {
        if (strcmp(iface->name, name) == 0) {
            spinlock_release(&g_ip_lock);
            return iface;
        }
        iface = iface->next;
    }
    
    spinlock_release(&g_ip_lock);
    return NULL;
}

network_interface_t* ip_get_default_interface(void) {
    spinlock_acquire(&g_ip_lock);
    network_interface_t* iface = g_interfaces;
    spinlock_release(&g_ip_lock);
    return iface;
}
