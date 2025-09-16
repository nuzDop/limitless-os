/*
 * Resonance Driver Framework Header
 * Universal driver abstraction for Continuum
 */

#ifndef RESONANCE_H
#define RESONANCE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../continuum_core.h"

// =============================================================================
// Constants
// =============================================================================

#define MAX_DRIVERS             256
#define MAX_DEVICES             1024
#define MAX_IRQ_VECTORS         32
#define MAX_DMA_REGIONS         16
#define MAX_VENDOR_IDS          16
#define MAX_DEVICE_IDS          16

// I/O Ports
#define PCI_CONFIG_ADDRESS      0xCF8
#define PCI_CONFIG_DATA         0xCFC

// =============================================================================
// Type Definitions
// =============================================================================

// Forward declarations
typedef struct device_node device_node_t;
typedef struct device_handle device_handle_t;
typedef struct resonance_driver resonance_driver_t;

// Bus types
typedef enum {
    BUS_TYPE_PCI = 0,
    BUS_TYPE_USB,
    BUS_TYPE_VIRTIO,
    BUS_TYPE_THUNDERBOLT,
    BUS_TYPE_I2C,
    BUS_TYPE_SPI,
    BUS_TYPE_PLATFORM,
    BUS_TYPE_CUSTOM,
    BUS_TYPE_MAX
} bus_type_t;

// Device states
typedef enum {
    DEVICE_STATE_UNKNOWN = 0,
    DEVICE_STATE_DISCOVERED,
    DEVICE_STATE_PROBED,
    DEVICE_STATE_CONFIGURED,
    DEVICE_STATE_ACTIVE,
    DEVICE_STATE_SUSPENDED,
    DEVICE_STATE_ERROR
} device_state_t;

// Driver states
typedef enum {
    DRIVER_STATE_UNREGISTERED = 0,
    DRIVER_STATE_REGISTERED,
    DRIVER_STATE_ACTIVE,
    DRIVER_STATE_SUSPENDED
} driver_state_t;

// I/O result codes
typedef enum {
    IO_SUCCESS = 0,
    IO_ERROR,
    IO_PENDING,
    IO_TIMEOUT,
    IO_BUSY,
    IO_NO_DEVICE
} io_result_t;

// =============================================================================
// Data Structures
// =============================================================================

// Interrupt vector
typedef struct {
    uint32_t irq;
    void (*handler)(void* context);
    void* context;
} interrupt_vector_t;

// DMA region
typedef struct {
    void* virtual_addr;
    uint64_t physical_addr;
    size_t size;
    uint32_t flags;
} dma_region_t;

// I/O packet
typedef struct {
    uint32_t operation;     // Read, Write, Control
    uint64_t offset;
    void* buffer;
    size_t size;
    uint32_t flags;
    void* completion;       // Completion callback
} io_packet_t;

// PCI device info
typedef struct {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint32_t bars[6];
    uint8_t bar_count;
    uint8_t irq_line;
    uint8_t irq_pin;
} pci_device_info_t;

// USB device info
typedef struct {
    uint8_t address;
    uint8_t configuration;
    uint8_t interface;
    uint16_t vendor_id;
    uint16_t product_id;
    uint8_t device_class;
    uint8_t device_subclass;
    uint8_t device_protocol;
} usb_device_info_t;

// Device node
struct device_node {
    uint32_t id;
    char name[64];
    bus_type_t bus_type;
    device_state_t state;
    
    // Identification
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass_code;
    uint32_t revision;
    
    // Bus-specific data
    void* bus_specific_data;
    
    // Resources
    interrupt_vector_t* irq_vectors;
    dma_region_t* dma_regions;
    
    // Driver binding
    resonance_driver_t* driver;
    device_handle_t* handle;
    
    // Tree structure
    device_node_t* parent;
    device_node_t* children;
    device_node_t* sibling;
    
    // Synchronization
    spinlock_t lock;
};

// Device handle
struct device_handle {
    device_node_t* device_node;
    void* driver_data;
    
    // Resources
    interrupt_vector_t irq_vectors[MAX_IRQ_VECTORS];
    uint32_t irq_count;
    dma_region_t* dma_regions[MAX_DMA_REGIONS];
    uint32_t dma_count;
    
    // Statistics
    uint64_t io_requests;
    uint64_t io_errors;
    uint64_t bytes_transferred;
};

// Driver structure
struct resonance_driver {
    uint32_t id;
    char name[64];
    driver_state_t state;
    
    // Device matching
    uint16_t vendor_ids[MAX_VENDOR_IDS];
    uint16_t device_ids[MAX_DEVICE_IDS];
    uint8_t class_code;
    uint8_t subclass_code;
    
    // Driver operations
    void* (*probe)(device_node_t* node);
    int (*attach)(device_handle_t* handle);
    void (*detach)(device_handle_t* handle);
    io_result_t (*io_request)(device_handle_t* handle, io_packet_t* packet);
    int (*suspend)(device_handle_t* handle);
    int (*resume)(device_handle_t* handle);
    
    // Power management
    int (*power_on)(device_handle_t* handle);
    int (*power_off)(device_handle_t* handle);
    
    // Configuration
    int (*configure)(device_handle_t* handle, void* config);
    int (*reset)(device_handle_t* handle);
};

// Bus manager
typedef struct {
    bus_type_t bus_type;
    bool initialized;
    uint32_t device_count;
    
    // Bus operations
    int (*scan)(struct bus_manager* manager);
    int (*configure)(struct bus_manager* manager);
    int (*reset)(struct bus_manager* manager);
    
    // Synchronization
    spinlock_t lock;
} bus_manager_t;

// Device tree
typedef struct {
    device_node_t* root;
    uint32_t node_count;
} device_tree_t;

// Driver registry
typedef struct {
    bool initialized;
    uint32_t driver_count;
    uint32_t device_count;
} resonance_registry_t;

// IRQ handler type
typedef void (*irq_handler_t)(void* context);

// =============================================================================
// Function Prototypes
// =============================================================================

// Initialization
void resonance_init(void);

// Driver management
int resonance_register_driver(resonance_driver_t* driver);
void resonance_unregister_driver(resonance_driver_t* driver);

// Device management
device_node_t* resonance_create_device_node(void);
int resonance_add_device(device_node_t* node);
void resonance_remove_device(device_node_t* node);
device_node_t* resonance_find_device(uint16_t vendor_id, uint16_t device_id);

// Driver matching
void resonance_match_driver(device_node_t* node);
void resonance_probe_devices(resonance_driver_t* driver);

// Bus operations
void resonance_scan_all_buses(void);
int resonance_scan_bus(bus_type_t bus_type);

// Interrupt handling
int resonance_register_irq(device_handle_t* handle, uint32_t irq,
                          irq_handler_t handler, void* context);
void resonance_unregister_irq(device_handle_t* handle, uint32_t irq);

// DMA operations
dma_region_t* resonance_alloc_dma(size_t size, uint32_t flags);
void resonance_free_dma(dma_region_t* region);
uint64_t resonance_dma_map(void* virtual_addr, size_t size);
void resonance_dma_unmap(uint64_t physical_addr, size_t size);

// I/O operations
io_result_t resonance_io_request(device_handle_t* handle, io_packet_t* packet);

// Bus-specific operations
int pci_bus_scan(bus_manager_t* manager);
int pci_bus_configure(bus_manager_t* manager);
uint32_t pci_config_read(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
void pci_config_write(uint8_t bus, uint8_t device, uint8_t function, 
                     uint8_t offset, uint32_t value);

int usb_bus_scan(bus_manager_t* manager);
int usb_bus_configure(bus_manager_t* manager);

int virtio_bus_scan(bus_manager_t* manager);
int virtio_bus_configure(bus_manager_t* manager);

int thunderbolt_bus_scan(bus_manager_t* manager);
int thunderbolt_bus_configure(bus_manager_t* manager);

// I/O port operations
void outb(uint16_t port, uint8_t value);
uint8_t inb(uint16_t port);
void outw(uint16_t port, uint16_t value);
uint16_t inw(uint16_t port);
void outl(uint16_t port, uint32_t value);
uint32_t inl(uint16_t port);
void io_wait(void);

// Memory-mapped I/O
static inline void mmio_write32(void* addr, uint32_t value) {
    *(volatile uint32_t*)addr = value;
}

static inline uint32_t mmio_read32(void* addr) {
    return *(volatile uint32_t*)addr;
}

static inline void mmio_write64(void* addr, uint64_t value) {
    *(volatile uint64_t*)addr = value;
}

static inline uint64_t mmio_read64(void* addr) {
    return *(volatile uint64_t*)addr;
}

#endif /* RESONANCE_H */
