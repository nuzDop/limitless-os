/*
 * Harmony Networking Stack
 * Core networking initialization and management
 */

#include "harmony_net.h"
#include "ethernet.h"
#include "arp.h"
#include "ip.h"
#include "tcp.h"
#include "udp.h"
#include "dhcp.h"
#include "../continuum/flux_memory.h"
#include "../continuum/temporal_scheduler.h"

// =============================================================================
// Global Networking State
// =============================================================================

static bool g_harmony_initialized = false;
static thread_t* g_network_thread;
static spinlock_t g_harmony_lock = SPINLOCK_INIT;

// Statistics
static struct {
    uint64_t packets_received;
    uint64_t packets_sent;
    uint64_t bytes_received;
    uint64_t bytes_sent;
    uint64_t errors;
} g_network_stats;

// =============================================================================
// Network Thread
// =============================================================================

static void harmony_network_thread(void* arg) {
    while (g_harmony_initialized) {
        // Process incoming packets from all interfaces
        network_interface_t* iface = ip_get_interface_list();
        while (iface) {
            // Check for received packets
            uint8_t buffer[ETH_FRAME_LEN];
            int len = iface->receive_packet(iface->driver_data, buffer, sizeof(buffer));
            
            if (len > 0) {
                ethernet_input(iface, buffer, len);
                g_network_stats.packets_received++;
                g_network_stats.bytes_received += len;
            }
            
            iface = iface->next;
        }
        
        // Process TCP timers
        tcp_timer_tick();
        
        // Process ARP cache
        arp_timer_tick();
        
        // Process DHCP renewals
        dhcp_timer_tick();
        
        // Sleep for 10ms
        temporal_sleep(10000);
    }
}

// =============================================================================
// Initialization
// =============================================================================

int harmony_init(void) {
    if (g_harmony_initialized) {
        return 0;
    }
    
    spinlock_acquire(&g_harmony_lock);
    
    // Initialize subsystems
    arp_init();
    ip_init();
    tcp_init();
    udp_init();
    
    // Create network processing thread
    g_network_thread = temporal_create_thread(harmony_network_thread, NULL,
                                             THREAD_PRIORITY_HIGH);
    if (!g_network_thread) {
        spinlock_release(&g_harmony_lock);
        return -1;
    }
    
    g_harmony_initialized = true;
    
    spinlock_release(&g_harmony_lock);
    
    return 0;
}

void harmony_shutdown(void) {
    if (!g_harmony_initialized) {
        return;
    }
    
    spinlock_acquire(&g_harmony_lock);
    
    g_harmony_initialized = false;
    
    // Wait for network thread to exit
    if (g_network_thread) {
        temporal_join_thread(g_network_thread);
        g_network_thread = NULL;
    }
    
    // Cleanup subsystems
    dhcp_cleanup();
    udp_cleanup();
    tcp_cleanup();
    ip_cleanup();
    arp_cleanup();
    
    spinlock_release(&g_harmony_lock);
}

// =============================================================================
// Interface Registration
// =============================================================================

int harmony_register_interface(const char* name, void* driver_data,
                              int (*send_fn)(void*, void*, size_t),
                              int (*recv_fn)(void*, void*, size_t),
                              uint8_t* mac_addr) {
    network_interface_t* iface = ip_add_interface(name, driver_data,
                                                 send_fn, recv_fn);
    if (!iface) {
        return -1;
    }
    
    // Set MAC address
    memcpy(iface->mac_addr, mac_addr, ETH_ALEN);
    
    // Start DHCP if enabled
    if (harmony_use_dhcp()) {
        dhcp_start(iface);
    }
    
    return 0;
}

// =============================================================================
// High-Level Socket API
// =============================================================================

int harmony_socket(int family, int type, int protocol) {
    socket_t* sock = socket_create(family, type, protocol);
    if (!sock) {
        return -1;
    }
    
    return sock->id;
}

int harmony_bind(int sockfd, uint32_t addr, uint16_t port) {
    socket_t* sock = socket_get(sockfd);
    if (!sock) {
        return -1;
    }
    
    sock->local_addr.family = AF_INET;
    sock->local_addr.ipv4.addr = addr;
    sock->local_addr.ipv4.port = port;
    
    if (sock->type == SOCK_DGRAM) {
        return udp_bind(sock, addr, port);
    } else if (sock->type == SOCK_STREAM) {
        return 0;  // TCP bind happens on listen
    }
    
    return -1;
}

int harmony_listen(int sockfd, int backlog) {
    socket_t* sock = socket_get(sockfd);
    if (!sock || sock->type != SOCK_STREAM) {
        return -1;
    }
    
    return tcp_listen(sock, backlog);
}

int harmony_accept(int sockfd, uint32_t* addr, uint16_t* port) {
    socket_t* sock = socket_get(sockfd);
    if (!sock || sock->type != SOCK_STREAM) {
        return -1;
    }
    
    socket_t* new_sock = tcp_accept(sock);
    if (!new_sock) {
        return -1;
    }
    
    if (addr) {
        *addr = new_sock->remote_addr.ipv4.addr;
    }
    if (port) {
        *port = new_sock->remote_addr.ipv4.port;
    }
    
    return new_sock->id;
}

int harmony_connect(int sockfd, uint32_t addr, uint16_t port) {
    socket_t* sock = socket_get(sockfd);
    if (!sock) {
        return -1;
    }
    
    if (sock->type == SOCK_STREAM) {
        return tcp_connect(sock, addr, port);
    } else if (sock->type == SOCK_DGRAM) {
        // UDP connect just sets default destination
        sock->remote_addr.family = AF_INET;
        sock->remote_addr.ipv4.addr = addr;
        sock->remote_addr.ipv4.port = port;
        return 0;
    }
    
    return -1;
}

int harmony_send(int sockfd, void* data, size_t len, int flags) {
    socket_t* sock = socket_get(sockfd);
    if (!sock) {
        return -1;
    }
    
    if (sock->type == SOCK_STREAM) {
        return tcp_send(sock, data, len);
    } else if (sock->type == SOCK_DGRAM) {
        return udp_sendto(sock, data, len, 
                         sock->remote_addr.ipv4.addr,
                         sock->remote_addr.ipv4.port);
    }
    
    return -1;
}

int harmony_recv(int sockfd, void* buffer, size_t len, int flags) {
    socket_t* sock = socket_get(sockfd);
    if (!sock) {
        return -1;
    }
    
    if (sock->type == SOCK_STREAM) {
        return tcp_recv(sock, buffer, len);
    } else if (sock->type == SOCK_DGRAM) {
        return udp_recvfrom(sock, buffer, len, NULL, NULL);
    }
    
    return -1;
}

int harmony_close(int sockfd) {
    socket_t* sock = socket_get(sockfd);
    if (!sock) {
        return -1;
    }
    
    if (sock->type == SOCK_STREAM) {
        tcp_close(sock);
    }
    
    socket_destroy(sock);
    return 0;
}

// =============================================================================
// Statistics
// =============================================================================

void harmony_get_stats(harmony_stats_t* stats) {
    if (stats) {
        stats->packets_received = g_network_stats.packets_received;
        stats->packets_sent = g_network_stats.packets_sent;
        stats->bytes_received = g_network_stats.bytes_received;
        stats->bytes_sent = g_network_stats.bytes_sent;
        stats->errors = g_network_stats.errors;
    }
}

// =============================================================================
// Helper Functions
// =============================================================================

uint64_t harmony_get_time(void) {
    return temporal_get_time();
}

uint32_t harmony_random(void) {
    static uint32_t seed = 0x12345678;
    seed = seed * 1103515245 + 12345;
    return seed;
}

bool harmony_use_dhcp(void) {
    // Could be configurable
    return true;
}

void* harmony_allocate(size_t size) {
    return flux_allocate(NULL, size, FLUX_ALLOC_KERNEL);
}

void harmony_free(void* ptr) {
    flux_free(ptr);
}
