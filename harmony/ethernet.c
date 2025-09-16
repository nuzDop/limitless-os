/*
 * Ethernet Layer Implementation
 * Data link layer for Harmony
 */

#include "harmony_net.h"
#include "ethernet.h"
#include "arp.h"
#include "ip.h"
#include "../continuum/flux_memory.h"

// =============================================================================
// Ethernet Input
// =============================================================================

void ethernet_input(network_interface_t* iface, void* frame, size_t len) {
    if (len < sizeof(eth_header_t)) {
        return;  // Frame too small
    }
    
    eth_header_t* eth_hdr = (eth_header_t*)frame;
    uint16_t ethertype = ntohs(eth_hdr->type);
    
    // Check if frame is for us
    bool for_us = false;
    
    // Check broadcast
    if (memcmp(eth_hdr->dest, "\xFF\xFF\xFF\xFF\xFF\xFF", ETH_ALEN) == 0) {
        for_us = true;
    }
    // Check our MAC
    else if (memcmp(eth_hdr->dest, iface->mac_addr, ETH_ALEN) == 0) {
        for_us = true;
    }
    // Check multicast
    else if (eth_hdr->dest[0] & 0x01) {
        for_us = ethernet_is_multicast_member(iface, eth_hdr->dest);
    }
    
    if (!for_us && !iface->promiscuous) {
        return;
    }
    
    // Process based on ethertype
    void* payload = (uint8_t*)frame + sizeof(eth_header_t);
    size_t payload_len = len - sizeof(eth_header_t);
    
    switch (ethertype) {
        case ETH_P_IP:
            ip_input(iface, payload, payload_len);
            break;
            
        case ETH_P_ARP:
            arp_input(iface, (arp_header_t*)payload, payload_len);
            break;
            
        case ETH_P_IPV6:
            ip6_input(iface, payload, payload_len);
            break;
            
        default:
            // Unknown ethertype
            iface->rx_errors++;
            break;
    }
}

// =============================================================================
// Ethernet Output
// =============================================================================

int ethernet_send(network_interface_t* iface, uint32_t dest_ip,
                 uint16_t ethertype, void* data, size_t len) {
    if (len > ETH_MTU) {
        return -1;  // Packet too large
    }
    
    // Allocate frame buffer
    size_t frame_len = sizeof(eth_header_t) + len;
    if (frame_len < ETH_MIN_FRAME) {
        frame_len = ETH_MIN_FRAME;
    }
    
    uint8_t* frame = flux_allocate(NULL, frame_len, FLUX_ALLOC_KERNEL);
    if (!frame) {
        return -1;
    }
    
    eth_header_t* eth_hdr = (eth_header_t*)frame;
    
    // Set source MAC
    memcpy(eth_hdr->src, iface->mac_addr, ETH_ALEN);
    
    // Determine destination MAC
    if (dest_ip == 0xFFFFFFFF) {
        // Broadcast
        memset(eth_hdr->dest, 0xFF, ETH_ALEN);
    } else if ((dest_ip & 0xF0000000) == 0xE0000000) {
        // Multicast - compute MAC from IP
        eth_hdr->dest[0] = 0x01;
        eth_hdr->dest[1] = 0x00;
        eth_hdr->dest[2] = 0x5E;
        eth_hdr->dest[3] = (dest_ip >> 16) & 0x7F;
        eth_hdr->dest[4] = (dest_ip >> 8) & 0xFF;
        eth_hdr->dest[5] = dest_ip & 0xFF;
    } else {
        // Unicast - need ARP resolution
        uint8_t dest_mac[ETH_ALEN];
        if (arp_resolve(iface, dest_ip, dest_mac) != 0) {
            // ARP resolution failed - queue packet
            flux_free(frame);
            return arp_queue_packet(iface, dest_ip, ethertype, data, len);
        }
        memcpy(eth_hdr->dest, dest_mac, ETH_ALEN);
    }
    
    // Set ethertype
    eth_hdr->type = htons(ethertype);
    
    // Copy payload
    memcpy(frame + sizeof(eth_header_t), data, len);
    
    // Pad if necessary
    if (frame_len > sizeof(eth_header_t) + len) {
        memset(frame + sizeof(eth_header_t) + len, 0,
              frame_len - sizeof(eth_header_t) - len);
    }
    
    // Send via driver
    int result = iface->send_packet(iface->driver_data, frame, frame_len);
    
    flux_free(frame);
    
    return result;
}

// =============================================================================
// Multicast Management
// =============================================================================

typedef struct multicast_entry {
    uint8_t mac_addr[ETH_ALEN];
    uint32_t ref_count;
    struct multicast_entry* next;
} multicast_entry_t;

static multicast_entry_t* g_multicast_list;
static spinlock_t g_multicast_lock = SPINLOCK_INIT;

int ethernet_join_multicast(network_interface_t* iface, uint8_t* mac_addr) {
    spinlock_acquire(&g_multicast_lock);
    
    // Check if already joined
    multicast_entry_t* entry = g_multicast_list;
    while (entry) {
        if (memcmp(entry->mac_addr, mac_addr, ETH_ALEN) == 0) {
            entry->ref_count++;
            spinlock_release(&g_multicast_lock);
            return 0;
        }
        entry = entry->next;
    }
    
    // Add new entry
    entry = flux_allocate(NULL, sizeof(multicast_entry_t), FLUX_ALLOC_KERNEL);
    if (!entry) {
        spinlock_release(&g_multicast_lock);
        return -1;
    }
    
    memcpy(entry->mac_addr, mac_addr, ETH_ALEN);
    entry->ref_count = 1;
    entry->next = g_multicast_list;
    g_multicast_list = entry;
    
    spinlock_release(&g_multicast_lock);
    
    // Update hardware filter if supported
    // This would typically call driver-specific function
    
    return 0;
}

bool ethernet_is_multicast_member(network_interface_t* iface, uint8_t* mac_addr) {
    spinlock_acquire(&g_multicast_lock);
    
    multicast_entry_t* entry = g_multicast_list;
    while (entry) {
        if (memcmp(entry->mac_addr, mac_addr, ETH_ALEN) == 0) {
            spinlock_release(&g_multicast_lock);
            return true;
        }
        entry = entry->next;
    }
    
    spinlock_release(&g_multicast_lock);
    return false;
}
