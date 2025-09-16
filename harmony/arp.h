/*
 * ARP Protocol Header
 * Address Resolution Protocol definitions
 */

#ifndef ARP_H
#define ARP_H

#include "harmony_net.h"

// ARP Hardware Types
#define ARP_HW_ETHERNET     1

// ARP Operations
#define ARP_OP_REQUEST      1
#define ARP_OP_REPLY        2
#define ARP_OP_RARP_REQUEST 3
#define ARP_OP_RARP_REPLY   4

// ARP Cache Entry States
#define ARP_STATE_FREE      0
#define ARP_STATE_PENDING   1
#define ARP_STATE_VALID     2
#define ARP_STATE_EXPIRED   3

// ARP Pending Packet
typedef struct arp_queued_packet {
    uint16_t ethertype;
    size_t data_len;
    uint8_t data[];
    struct arp_queued_packet* next;
} arp_queued_packet_t;

// ARP Pending Request
typedef struct arp_pending {
    uint32_t ip_addr;
    network_interface_t* interface;
    uint64_t timestamp;
    uint32_t retries;
    arp_queued_packet_t* packet_queue;
    struct arp_pending* next;
} arp_pending_t;

// Function prototypes
void arp_init(void);
void arp_cleanup(void);
void arp_input(network_interface_t* iface, arp_header_t* arp_hdr, size_t len);
int arp_resolve(network_interface_t* iface, uint32_t ip_addr, uint8_t* mac_addr);
int arp_add_entry(uint32_t ip_addr, uint8_t* mac_addr);
int arp_send_request(network_interface_t* iface, uint32_t target_ip);
int arp_send_reply(network_interface_t* iface, arp_header_t* request);
int arp_send_announcement(network_interface_t* iface);
int arp_queue_packet(network_interface_t* iface, uint32_t dest_ip,
                    uint16_t ethertype, void* data, size_t len);
void arp_process_pending(uint32_t ip_addr, uint8_t* mac_addr);
void arp_timer_tick(void);

#endif /* ARP_H */
