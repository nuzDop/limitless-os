/*
 * ICMP Protocol Implementation
 * Internet Control Message Protocol for Harmony
 */

#include "harmony_net.h"
#include "icmp.h"
#include "ip.h"
#include "../continuum/flux_memory.h"

// =============================================================================
// ICMP Statistics
// =============================================================================

static struct {
    uint64_t echo_requests_sent;
    uint64_t echo_requests_received;
    uint64_t echo_replies_sent;
    uint64_t echo_replies_received;
    uint64_t dest_unreachable_sent;
    uint64_t dest_unreachable_received;
    uint64_t time_exceeded_sent;
    uint64_t time_exceeded_received;
} g_icmp_stats;

// =============================================================================
// ICMP Checksum
// =============================================================================

static uint16_t icmp_checksum(icmp_header_t* hdr, size_t len) {
    uint32_t sum = 0;
    uint16_t* ptr = (uint16_t*)hdr;
    
    // Save and clear checksum field
    uint16_t saved_checksum = hdr->checksum;
    hdr->checksum = 0;
    
    // Calculate checksum
    while (len > 1) {
        sum += ntohs(*ptr++);
        len -= 2;
    }
    
    if (len > 0) {
        sum += ((uint8_t*)ptr)[0] << 8;
    }
    
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    // Restore checksum field
    hdr->checksum = saved_checksum;
    
    return ~sum;
}

// =============================================================================
// ICMP Input Processing
// =============================================================================

void icmp_input(network_interface_t* iface, ipv4_header_t* ip_hdr,
               icmp_header_t* icmp_hdr, size_t len) {
    if (len < sizeof(icmp_header_t)) {
        return;
    }
    
    // Verify checksum
    if (icmp_checksum(icmp_hdr, len) != icmp_hdr->checksum) {
        return;
    }
    
    switch (icmp_hdr->type) {
        case ICMP_TYPE_ECHO_REQUEST:
            g_icmp_stats.echo_requests_received++;
            icmp_handle_echo_request(iface, ip_hdr, icmp_hdr, len);
            break;
            
        case ICMP_TYPE_ECHO_REPLY:
            g_icmp_stats.echo_replies_received++;
            icmp_handle_echo_reply(ip_hdr, icmp_hdr, len);
            break;
            
        case ICMP_TYPE_DEST_UNREACHABLE:
            g_icmp_stats.dest_unreachable_received++;
            icmp_handle_dest_unreachable(ip_hdr, icmp_hdr, len);
            break;
            
        case ICMP_TYPE_TIME_EXCEEDED:
            g_icmp_stats.time_exceeded_received++;
            icmp_handle_time_exceeded(ip_hdr, icmp_hdr, len);
            break;
            
        case ICMP_TYPE_REDIRECT:
            icmp_handle_redirect(ip_hdr, icmp_hdr, len);
            break;
    }
}

// =============================================================================
// Echo Request/Reply (Ping)
// =============================================================================

static void icmp_handle_echo_request(network_interface_t* iface,
                                     ipv4_header_t* ip_hdr,
                                     icmp_header_t* icmp_hdr, size_t len) {
    // Build echo reply
    size_t reply_len = len;
    uint8_t* reply = flux_allocate(NULL, reply_len, FLUX_ALLOC_KERNEL);
    if (!reply) {
        return;
    }
    
    icmp_header_t* reply_hdr = (icmp_header_t*)reply;
    
    // Copy original packet
    memcpy(reply, icmp_hdr, len);
    
    // Change type to echo reply
    reply_hdr->type = ICMP_TYPE_ECHO_REPLY;
    reply_hdr->code = 0;
    
    // Recalculate checksum
    reply_hdr->checksum = 0;
    reply_hdr->checksum = icmp_checksum(reply_hdr, len);
    
    // Send reply
    ip_send(ntohl(ip_hdr->dest_addr), ntohl(ip_hdr->src_addr),
           IPPROTO_ICMP, reply, reply_len);
    
    g_icmp_stats.echo_replies_sent++;
    
    flux_free(reply);
}

static void icmp_handle_echo_reply(ipv4_header_t* ip_hdr,
                                   icmp_header_t* icmp_hdr, size_t len) {
    // Find waiting ping request
    uint16_t id = ntohs(icmp_hdr->echo.id);
    uint16_t seq = ntohs(icmp_hdr->echo.sequence);
    
    // Notify waiting process
    icmp_notify_ping_reply(ntohl(ip_hdr->src_addr), id, seq,
                          len - sizeof(icmp_header_t));
}

// =============================================================================
// Error Messages
// =============================================================================

int icmp_send_dest_unreachable(network_interface_t* iface, ipv4_header_t* orig_ip,
                               uint8_t code) {
    size_t packet_len = sizeof(icmp_header_t) + sizeof(ipv4_header_t) + 8;
    uint8_t* packet = flux_allocate(NULL, packet_len, FLUX_ALLOC_KERNEL);
    if (!packet) {
        return -1;
    }
    
    icmp_header_t* icmp_hdr = (icmp_header_t*)packet;
    icmp_hdr->type = ICMP_TYPE_DEST_UNREACHABLE;
    icmp_hdr->code = code;
    icmp_hdr->checksum = 0;
    icmp_hdr->unused = 0;
    
    // Copy original IP header + first 8 bytes of data
    memcpy(packet + sizeof(icmp_header_t), orig_ip,
          sizeof(ipv4_header_t) + 8);
    
    // Calculate checksum
    icmp_hdr->checksum = icmp_checksum(icmp_hdr, packet_len);
    
    // Send packet
    ip_send(iface->ipv4_addr, ntohl(orig_ip->src_addr),
           IPPROTO_ICMP, packet, packet_len);
    
    g_icmp_stats.dest_unreachable_sent++;
    
    flux_free(packet);
    return 0;
}

int icmp_send_time_exceeded(network_interface_t* iface, ipv4_header_t* orig_ip) {
    size_t packet_len = sizeof(icmp_header_t) + sizeof(ipv4_header_t) + 8;
    uint8_t* packet = flux_allocate(NULL, packet_len, FLUX_ALLOC_KERNEL);
    if (!packet) {
        return -1;
    }
    
    icmp_header_t* icmp_hdr = (icmp_header_t*)packet;
    icmp_hdr->type = ICMP_TYPE_TIME_EXCEEDED;
    icmp_hdr->code = ICMP_CODE_TTL_EXCEEDED;
    icmp_hdr->checksum = 0;
    icmp_hdr->unused = 0;
    
    // Copy original IP header + first 8 bytes
    memcpy(packet + sizeof(icmp_header_t), orig_ip,
          sizeof(ipv4_header_t) + 8);
    
    // Calculate checksum
    icmp_hdr->checksum = icmp_checksum(icmp_hdr, packet_len);
    
    // Send packet
    ip_send(iface->ipv4_addr, ntohl(orig_ip->src_addr),
           IPPROTO_ICMP, packet, packet_len);
    
    g_icmp_stats.time_exceeded_sent++;
    
    flux_free(packet);
    return 0;
}

// =============================================================================
// Ping Implementation
// =============================================================================

static uint16_t g_ping_id = 1;

int icmp_ping(uint32_t dest_addr, uint16_t sequence, void* data, size_t data_len) {
    size_t packet_len = sizeof(icmp_header_t) + data_len;
    uint8_t* packet = flux_allocate(NULL, packet_len, FLUX_ALLOC_KERNEL);
    if (!packet) {
        return -1;
    }
    
    icmp_header_t* icmp_hdr = (icmp_header_t*)packet;
    icmp_hdr->type = ICMP_TYPE_ECHO_REQUEST;
    icmp_hdr->code = 0;
    icmp_hdr->checksum = 0;
    icmp_hdr->echo.id = htons(g_ping_id++);
    icmp_hdr->echo.sequence = htons(sequence);
    
    // Copy data
    if (data && data_len > 0) {
        memcpy(packet + sizeof(icmp_header_t), data, data_len);
    }
    
    // Calculate checksum
    icmp_hdr->checksum = icmp_checksum(icmp_hdr, packet_len);
    
    // Send packet
    int result = ip_send(0, dest_addr, IPPROTO_ICMP, packet, packet_len);
    
    if (result == 0) {
        g_icmp_stats.echo_requests_sent++;
    }
    
    flux_free(packet);
    return result;
}

// =============================================================================
// Initialization
// =============================================================================

void icmp_init(void) {
    memset(&g_icmp_stats, 0, sizeof(g_icmp_stats));
}

void icmp_get_statistics(icmp_stats_t* stats) {
    if (stats) {
        stats->echo_requests_sent = g_icmp_stats.echo_requests_sent;
        stats->echo_requests_received = g_icmp_stats.echo_requests_received;
        stats->echo_replies_sent = g_icmp_stats.echo_replies_sent;
        stats->echo_replies_received = g_icmp_stats.echo_replies_received;
        stats->dest_unreachable_sent = g_icmp_stats.dest_unreachable_sent;
        stats->dest_unreachable_received = g_icmp_stats.dest_unreachable_received;
    }
}
