/*
 * ARP Protocol Implementation
 * Address Resolution Protocol for Harmony
 */

#include "harmony_net.h"
#include "arp.h"
#include "ethernet.h"
#include "../continuum/flux_memory.h"

// =============================================================================
// Global ARP State
// =============================================================================

#define ARP_CACHE_SIZE      256
#define ARP_CACHE_TIMEOUT   300000000  // 5 minutes in microseconds
#define ARP_REQUEST_TIMEOUT 1000000    // 1 second
#define ARP_MAX_RETRIES     3

static arp_entry_t* g_arp_cache[ARP_CACHE_SIZE];
static arp_pending_t* g_pending_requests;
static spinlock_t g_arp_lock = SPINLOCK_INIT;

// =============================================================================
// ARP Cache Management
// =============================================================================

static uint32_t arp_hash(uint32_t ip_addr) {
    return (ip_addr ^ (ip_addr >> 16)) % ARP_CACHE_SIZE;
}

int arp_add_entry(uint32_t ip_addr, uint8_t* mac_addr) {
    uint32_t hash = arp_hash(ip_addr);
    
    spinlock_acquire(&g_arp_lock);
    
    // Check if entry already exists
    arp_entry_t* entry = g_arp_cache[hash];
    while (entry) {
        if (entry->ip_addr == ip_addr) {
            // Update existing entry
            memcpy(entry->mac_addr, mac_addr, ETH_ALEN);
            entry->timestamp = harmony_get_time();
            entry->valid = true;
            spinlock_release(&g_arp_lock);
            
            // Process pending packets
            arp_process_pending(ip_addr, mac_addr);
            return 0;
        }
        entry = entry->next;
    }
    
    // Create new entry
    entry = flux_allocate(NULL, sizeof(arp_entry_t), FLUX_ALLOC_KERNEL);
    if (!entry) {
        spinlock_release(&g_arp_lock);
        return -1;
    }
    
    entry->ip_addr = ip_addr;
    memcpy(entry->mac_addr, mac_addr, ETH_ALEN);
    entry->timestamp = harmony_get_time();
    entry->valid = true;
    entry->next = g_arp_cache[hash];
    g_arp_cache[hash] = entry;
    
    spinlock_release(&g_arp_lock);
    
    // Process pending packets
    arp_process_pending(ip_addr, mac_addr);
    
    return 0;
}

int arp_resolve(network_interface_t* iface, uint32_t ip_addr, uint8_t* mac_addr) {
    uint32_t hash = arp_hash(ip_addr);
    
    spinlock_acquire(&g_arp_lock);
    
    // Check cache
    arp_entry_t* entry = g_arp_cache[hash];
    while (entry) {
        if (entry->ip_addr == ip_addr && entry->valid) {
            // Check if entry is still valid
            if (harmony_get_time() - entry->timestamp < ARP_CACHE_TIMEOUT) {
                memcpy(mac_addr, entry->mac_addr, ETH_ALEN);
                spinlock_release(&g_arp_lock);
                return 0;
            } else {
                // Entry expired
                entry->valid = false;
            }
        }
        entry = entry->next;
    }
    
    spinlock_release(&g_arp_lock);
    
    // Not in cache - send ARP request
    arp_send_request(iface, ip_addr);
    
    return -1;
}

// =============================================================================
// ARP Input Processing
// =============================================================================

void arp_input(network_interface_t* iface, arp_header_t* arp_hdr, size_t len) {
    if (len < sizeof(arp_header_t)) {
        return;
    }
    
    // Verify hardware and protocol types
    if (ntohs(arp_hdr->hardware_type) != ARP_HW_ETHERNET ||
        ntohs(arp_hdr->protocol_type) != ETH_P_IP ||
        arp_hdr->hardware_len != ETH_ALEN ||
        arp_hdr->protocol_len != 4) {
        return;
    }
    
    uint16_t operation = ntohs(arp_hdr->operation);
    uint32_t sender_ip = ntohl(arp_hdr->sender_ip);
    uint32_t target_ip = ntohl(arp_hdr->target_ip);
    
    // Update ARP cache with sender
    arp_add_entry(sender_ip, arp_hdr->sender_mac);
    
    // Check if this ARP is for us
    if (target_ip != iface->ipv4_addr) {
        return;
    }
    
    switch (operation) {
        case ARP_OP_REQUEST:
            // Send ARP reply
            arp_send_reply(iface, arp_hdr);
            break;
            
        case ARP_OP_REPLY:
            // Already updated cache above
            break;
    }
}

// =============================================================================
// ARP Output
// =============================================================================

int arp_send_request(network_interface_t* iface, uint32_t target_ip) {
    // Check if request already pending
    spinlock_acquire(&g_arp_lock);
    
    arp_pending_t* pending = g_pending_requests;
    while (pending) {
        if (pending->ip_addr == target_ip && pending->interface == iface) {
            // Request already pending
            if (harmony_get_time() - pending->timestamp < ARP_REQUEST_TIMEOUT) {
                spinlock_release(&g_arp_lock);
                return 0;
            }
            // Request timed out - retry
            pending->retries++;
            if (pending->retries >= ARP_MAX_RETRIES) {
                // Max retries reached
                spinlock_release(&g_arp_lock);
                return -1;
            }
            pending->timestamp = harmony_get_time();
            break;
        }
        pending = pending->next;
    }
    
    if (!pending) {
        // Create new pending request
        pending = flux_allocate(NULL, sizeof(arp_pending_t), FLUX_ALLOC_KERNEL);
        if (!pending) {
            spinlock_release(&g_arp_lock);
            return -1;
        }
        pending->ip_addr = target_ip;
        pending->interface = iface;
        pending->timestamp = harmony_get_time();
        pending->retries = 0;
        pending->packet_queue = NULL;
        pending->next = g_pending_requests;
        g_pending_requests = pending;
    }
    
    spinlock_release(&g_arp_lock);
    
    // Build ARP request packet
    size_t packet_len = sizeof(eth_header_t) + sizeof(arp_header_t);
    uint8_t* packet = flux_allocate(NULL, packet_len, FLUX_ALLOC_KERNEL);
    if (!packet) {
        return -1;
    }
    
    eth_header_t* eth_hdr = (eth_header_t*)packet;
    arp_header_t* arp_hdr = (arp_header_t*)(packet + sizeof(eth_header_t));
    
    // Ethernet header
    memset(eth_hdr->dest, 0xFF, ETH_ALEN);  // Broadcast
    memcpy(eth_hdr->src, iface->mac_addr, ETH_ALEN);
    eth_hdr->type = htons(ETH_P_ARP);
    
    // ARP header
    arp_hdr->hardware_type = htons(ARP_HW_ETHERNET);
    arp_hdr->protocol_type = htons(ETH_P_IP);
    arp_hdr->hardware_len = ETH_ALEN;
    arp_hdr->protocol_len = 4;
    arp_hdr->operation = htons(ARP_OP_REQUEST);
    memcpy(arp_hdr->sender_mac, iface->mac_addr, ETH_ALEN);
    arp_hdr->sender_ip = htonl(iface->ipv4_addr);
    memset(arp_hdr->target_mac, 0, ETH_ALEN);
    arp_hdr->target_ip = htonl(target_ip);
    
    // Send packet
    int result = iface->send_packet(iface->driver_data, packet, packet_len);
    
    flux_free(packet);
    return result;
}

int arp_send_reply(network_interface_t* iface, arp_header_t* request) {
    size_t packet_len = sizeof(eth_header_t) + sizeof(arp_header_t);
    uint8_t* packet = flux_allocate(NULL, packet_len, FLUX_ALLOC_KERNEL);
    if (!packet) {
        return -1;
    }
    
    eth_header_t* eth_hdr = (eth_header_t*)packet;
    arp_header_t* arp_hdr = (arp_header_t*)(packet + sizeof(eth_header_t));
    
    // Ethernet header
    memcpy(eth_hdr->dest, request->sender_mac, ETH_ALEN);
    memcpy(eth_hdr->src, iface->mac_addr, ETH_ALEN);
    eth_hdr->type = htons(ETH_P_ARP);
    
    // ARP header
    arp_hdr->hardware_type = htons(ARP_HW_ETHERNET);
    arp_hdr->protocol_type = htons(ETH_P_IP);
    arp_hdr->hardware_len = ETH_ALEN;
    arp_hdr->protocol_len = 4;
    arp_hdr->operation = htons(ARP_OP_REPLY);
    memcpy(arp_hdr->sender_mac, iface->mac_addr, ETH_ALEN);
    arp_hdr->sender_ip = request->target_ip;  // Our IP
    memcpy(arp_hdr->target_mac, request->sender_mac, ETH_ALEN);
    arp_hdr->target_ip = request->sender_ip;
    
    // Send packet
    int result = iface->send_packet(iface->driver_data, packet, packet_len);
    
    flux_free(packet);
    return result;
}

// =============================================================================
// Pending Packet Queue
// =============================================================================

int arp_queue_packet(network_interface_t* iface, uint32_t dest_ip,
                    uint16_t ethertype, void* data, size_t len) {
    spinlock_acquire(&g_arp_lock);
    
    // Find pending request
    arp_pending_t* pending = g_pending_requests;
    while (pending) {
        if (pending->ip_addr == dest_ip && pending->interface == iface) {
            break;
        }
        pending = pending->next;
    }
    
    if (!pending) {
        // Create new pending request
        pending = flux_allocate(NULL, sizeof(arp_pending_t), FLUX_ALLOC_KERNEL);
        if (!pending) {
            spinlock_release(&g_arp_lock);
            return -1;
        }
        pending->ip_addr = dest_ip;
        pending->interface = iface;
        pending->timestamp = harmony_get_time();
        pending->retries = 0;
        pending->packet_queue = NULL;
        pending->next = g_pending_requests;
        g_pending_requests = pending;
    }
    
    // Queue packet
    arp_queued_packet_t* queued = flux_allocate(NULL, 
                                               sizeof(arp_queued_packet_t) + len,
                                               FLUX_ALLOC_KERNEL);
    if (!queued) {
        spinlock_release(&g_arp_lock);
        return -1;
    }
    
    queued->ethertype = ethertype;
    queued->data_len = len;
    memcpy(queued->data, data, len);
    queued->next = pending->packet_queue;
    pending->packet_queue = queued;
    
    spinlock_release(&g_arp_lock);
    
    // Send ARP request
    arp_send_request(iface, dest_ip);
    
    return 0;
}

void arp_process_pending(uint32_t ip_addr, uint8_t* mac_addr) {
    spinlock_acquire(&g_arp_lock);
    
    // Find pending request
    arp_pending_t** prev = &g_pending_requests;
    arp_pending_t* pending = g_pending_requests;
    
    while (pending) {
        if (pending->ip_addr == ip_addr) {
            // Send queued packets
            arp_queued_packet_t* queued = pending->packet_queue;
            while (queued) {
                // Build Ethernet frame
                size_t frame_len = sizeof(eth_header_t) + queued->data_len;
                uint8_t* frame = flux_allocate(NULL, frame_len, FLUX_ALLOC_KERNEL);
                
                if (frame) {
                    eth_header_t* eth_hdr = (eth_header_t*)frame;
                    memcpy(eth_hdr->dest, mac_addr, ETH_ALEN);
                    memcpy(eth_hdr->src, pending->interface->mac_addr, ETH_ALEN);
                    eth_hdr->type = htons(queued->ethertype);
                    memcpy(frame + sizeof(eth_header_t), queued->data, queued->data_len);
                    
                    pending->interface->send_packet(pending->interface->driver_data,
                                                   frame, frame_len);
                    flux_free(frame);
                }
                
                arp_queued_packet_t* next = queued->next;
                flux_free(queued);
                queued = next;
            }
            
            // Remove pending request
            *prev = pending->next;
            flux_free(pending);
            break;
        }
        
        prev = &pending->next;
        pending = pending->next;
    }
    
    spinlock_release(&g_arp_lock);
}

// =============================================================================
// Timer Management
// =============================================================================

void arp_timer_tick(void) {
    uint64_t now = harmony_get_time();
    
    spinlock_acquire(&g_arp_lock);
    
    // Clean up expired cache entries
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        arp_entry_t** prev = &g_arp_cache[i];
        arp_entry_t* entry = g_arp_cache[i];
        
        while (entry) {
            if (now - entry->timestamp > ARP_CACHE_TIMEOUT) {
                // Remove expired entry
                *prev = entry->next;
                flux_free(entry);
                entry = *prev;
            } else {
                prev = &entry->next;
                entry = entry->next;
            }
        }
    }
    
    // Clean up timed out pending requests
    arp_pending_t** prev = &g_pending_requests;
    arp_pending_t* pending = g_pending_requests;
    
    while (pending) {
        if (now - pending->timestamp > ARP_REQUEST_TIMEOUT * ARP_MAX_RETRIES) {
            // Free queued packets
            arp_queued_packet_t* queued = pending->packet_queue;
            while (queued) {
                arp_queued_packet_t* next = queued->next;
                flux_free(queued);
                queued = next;
            }
            
            // Remove pending request
            *prev = pending->next;
            flux_free(pending);
            pending = *prev;
        } else {
            prev = &pending->next;
            pending = pending->next;
        }
    }
    
    spinlock_release(&g_arp_lock);
}

// =============================================================================
// Initialization
// =============================================================================

void arp_init(void) {
    memset(g_arp_cache, 0, sizeof(g_arp_cache));
    g_pending_requests = NULL;
}

void arp_cleanup(void) {
    // Free all cache entries
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        arp_entry_t* entry = g_arp_cache[i];
        while (entry) {
            arp_entry_t* next = entry->next;
            flux_free(entry);
            entry = next;
        }
        g_arp_cache[i] = NULL;
    }
    
    // Free pending requests
    arp_pending_t* pending = g_pending_requests;
    while (pending) {
        arp_queued_packet_t* queued = pending->packet_queue;
        while (queued) {
            arp_queued_packet_t* next = queued->next;
            flux_free(queued);
            queued = next;
        }
        
        arp_pending_t* next = pending->next;
        flux_free(pending);
        pending = next;
    }
    g_pending_requests = NULL;
}
