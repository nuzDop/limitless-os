/*
 * DHCP Client Implementation
 * Dynamic Host Configuration Protocol for Harmony
 */

#include "harmony_net.h"
#include "dhcp.h"
#include "udp.h"
#include "../continuum/flux_memory.h"
#include "../continuum/temporal_scheduler.h"

// =============================================================================
// Global DHCP State
// =============================================================================

typedef struct dhcp_client {
    network_interface_t* interface;
    uint8_t state;
    uint32_t xid;  // Transaction ID
    
    // Offered/Assigned addresses
    uint32_t offered_addr;
    uint32_t assigned_addr;
    uint32_t server_addr;
    uint32_t gateway_addr;
    uint32_t dns_addr[4];
    uint32_t subnet_mask;
    
    // Lease info
    uint32_t lease_time;
    uint32_t renewal_time;
    uint32_t rebinding_time;
    uint64_t lease_obtained;
    
    // Retry info
    uint32_t retry_count;
    uint64_t last_request;
    
    struct dhcp_client* next;
} dhcp_client_t;

static dhcp_client_t* g_dhcp_clients;
static spinlock_t g_dhcp_lock = SPINLOCK_INIT;

// =============================================================================
// DHCP Message Creation
// =============================================================================

static dhcp_message_t* dhcp_create_message(uint8_t type, dhcp_client_t* client) {
    dhcp_message_t* msg = flux_allocate(NULL, sizeof(dhcp_message_t),
                                       FLUX_ALLOC_KERNEL | FLUX_ALLOC_ZERO);
    if (!msg) {
        return NULL;
    }
    
    msg->op = DHCP_OP_REQUEST;
    msg->htype = 1;  // Ethernet
    msg->hlen = 6;   // MAC address length
    msg->hops = 0;
    msg->xid = htonl(client->xid);
    msg->secs = 0;
    msg->flags = htons(DHCP_FLAG_BROADCAST);
    
    // Set client IP based on state
    if (client->state == DHCP_STATE_RENEWING ||
        client->state == DHCP_STATE_REBINDING) {
        msg->ciaddr = htonl(client->assigned_addr);
    }
    
    // Copy MAC address
    memcpy(msg->chaddr, client->interface->mac_addr, 6);
    
    // Set magic cookie
    msg->magic = htonl(DHCP_MAGIC_COOKIE);
    
    // Add options
    uint8_t* opt = msg->options;
    
    // Message type
    *opt++ = DHCP_OPT_MSG_TYPE;
    *opt++ = 1;
    *opt++ = type;
    
    // Client identifier
    *opt++ = DHCP_OPT_CLIENT_ID;
    *opt++ = 7;
    *opt++ = 1;  // Hardware type
    memcpy(opt, client->interface->mac_addr, 6);
    opt += 6;
    
    // Requested IP (for REQUEST)
    if (type == DHCP_MSG_REQUEST && client->offered_addr) {
        *opt++ = DHCP_OPT_REQUESTED_IP;
        *opt++ = 4;
        *(uint32_t*)opt = htonl(client->offered_addr);
        opt += 4;
    }
    
    // Server identifier (for REQUEST)
    if (type == DHCP_MSG_REQUEST && client->server_addr) {
        *opt++ = DHCP_OPT_SERVER_ID;
        *opt++ = 4;
        *(uint32_t*)opt = htonl(client->server_addr);
        opt += 4;
    }
    
    // Parameter request list
    *opt++ = DHCP_OPT_PARAM_LIST;
    *opt++ = 4;
    *opt++ = DHCP_OPT_SUBNET_MASK;
    *opt++ = DHCP_OPT_ROUTER;
    *opt++ = DHCP_OPT_DNS;
    *opt++ = DHCP_OPT_DOMAIN_NAME;
    
    // End option
    *opt++ = DHCP_OPT_END;
    
    return msg;
}

// =============================================================================
// DHCP State Machine
// =============================================================================

int dhcp_start(network_interface_t* iface) {
    dhcp_client_t* client = flux_allocate(NULL, sizeof(dhcp_client_t),
                                         FLUX_ALLOC_KERNEL | FLUX_ALLOC_ZERO);
    if (!client) {
        return -1;
    }
    
    client->interface = iface;
    client->state = DHCP_STATE_INIT;
    client->xid = harmony_random();
    
    spinlock_acquire(&g_dhcp_lock);
    client->next = g_dhcp_clients;
    g_dhcp_clients = client;
    spinlock_release(&g_dhcp_lock);
    
    // Send DISCOVER
    dhcp_send_discover(client);
    
    return 0;
}

static int dhcp_send_discover(dhcp_client_t* client) {
    dhcp_message_t* msg = dhcp_create_message(DHCP_MSG_DISCOVER, client);
    if (!msg) {
        return -1;
    }
    
    client->state = DHCP_STATE_SELECTING;
    client->last_request = temporal_get_time();
    
    // Send to broadcast address
    int result = udp_sendto_broadcast(client->interface, DHCP_CLIENT_PORT,
                                     DHCP_SERVER_PORT, msg, sizeof(dhcp_message_t));
    
    flux_free(msg);
    return result;
}

static int dhcp_send_request(dhcp_client_t* client) {
    dhcp_message_t* msg = dhcp_create_message(DHCP_MSG_REQUEST, client);
    if (!msg) {
        return -1;
    }
    
    client->state = DHCP_STATE_REQUESTING;
    client->last_request = temporal_get_time();
    
    // Send to broadcast or unicast based on state
    int result;
    if (client->assigned_addr) {
        // Renewing - unicast to server
        result = udp_sendto(NULL, msg, sizeof(dhcp_message_t),
                          client->server_addr, DHCP_SERVER_PORT);
    } else {
        // Initial request - broadcast
        result = udp_sendto_broadcast(client->interface, DHCP_CLIENT_PORT,
                                     DHCP_SERVER_PORT, msg, sizeof(dhcp_message_t));
    }
    
    flux_free(msg);
    return result;
}

// =============================================================================
// DHCP Message Processing
// =============================================================================

void dhcp_handle_offer(dhcp_client_t* client, dhcp_message_t* msg) {
    if (client->state != DHCP_STATE_SELECTING) {
        return;
    }
    
    // Parse offered address
    client->offered_addr = ntohl(msg->yiaddr);
    
    // Parse options
    dhcp_parse_options(msg->options, client);
    
    // Send REQUEST
    dhcp_send_request(client);
}

void dhcp_handle_ack(dhcp_client_t* client, dhcp_message_t* msg) {
    if (client->state != DHCP_STATE_REQUESTING &&
        client->state != DHCP_STATE_RENEWING &&
        client->state != DHCP_STATE_REBINDING) {
        return;
    }
    
    // Parse assigned address
    client->assigned_addr = ntohl(msg->yiaddr);
    
    // Parse options
    dhcp_parse_options(msg->options, client);
    
    // Configure interface
    ip_configure_interface(client->interface, client->assigned_addr,
                          client->subnet_mask);
    
    // Add default route
    if (client->gateway_addr) {
        ip_add_route(0, 0, client->gateway_addr, client->interface);
    }
    
    // Configure DNS
    if (client->dns_addr[0]) {
        dns_set_server(client->dns_addr[0]);
    }
    
    // Update state
    client->state = DHCP_STATE_BOUND;
    client->lease_obtained = temporal_get_time();
    
    // Schedule renewal
    if (client->renewal_time == 0) {
        client->renewal_time = client->lease_time / 2;
    }
}

// =============================================================================
// Timer Management
// =============================================================================

void dhcp_timer_tick(void) {
    uint64_t now = temporal_get_time();
    
    spinlock_acquire(&g_dhcp_lock);
    
    dhcp_client_t* client = g_dhcp_clients;
    while (client) {
        switch (client->state) {
            case DHCP_STATE_SELECTING:
            case DHCP_STATE_REQUESTING:
                // Check for timeout
                if (now - client->last_request > DHCP_REQUEST_TIMEOUT) {
                    client->retry_count++;
                    if (client->retry_count > DHCP_MAX_RETRIES) {
                        // Give up
                        client->state = DHCP_STATE_INIT;
                    } else {
                        // Retry
                        if (client->state == DHCP_STATE_SELECTING) {
                            dhcp_send_discover(client);
                        } else {
                            dhcp_send_request(client);
                        }
                    }
                }
                break;
                
            case DHCP_STATE_BOUND:
                // Check for renewal time
                if (now - client->lease_obtained > client->renewal_time * 1000000) {
                    client->state = DHCP_STATE_RENEWING;
                    dhcp_send_request(client);
                }
                break;
                
            case DHCP_STATE_RENEWING:
                // Check for rebinding time
                if (now - client->lease_obtained > client->rebinding_time * 1000000) {
                    client->state = DHCP_STATE_REBINDING;
                    dhcp_send_request(client);
                }
                break;
                
            case DHCP_STATE_REBINDING:
                // Check for lease expiration
                if (now - client->lease_obtained > client->lease_time * 1000000) {
                    // Lease expired - restart
                    client->state = DHCP_STATE_INIT;
                    client->assigned_addr = 0;
                    dhcp_send_discover(client);
                }
                break;
        }
        
        client = client->next;
    }
    
    spinlock_release(&g_dhcp_lock);
}

// =============================================================================
// Initialization
// =============================================================================

void dhcp_init(void) {
    g_dhcp_clients = NULL;
    
    // Register UDP handler for DHCP client port
    udp_register_handler(DHCP_CLIENT_PORT, dhcp_handle_packet);
}

void dhcp_cleanup(void) {
    dhcp_client_t* client = g_dhcp_clients;
    while (client) {
        dhcp_client_t* next = client->next;
        
        // Send RELEASE if bound
        if (client->state == DHCP_STATE_BOUND) {
            dhcp_send_release(client);
        }
        
        flux_free(client);
        client = next;
    }
    
    g_dhcp_clients = NULL;
}
