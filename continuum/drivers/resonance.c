/*
 * Resonance Driver Framework for Continuum Kernel
 * Universal driver abstraction supporting diverse hardware
 */

#include "resonance.h"
#include "../continuum_core.h"
#include "../flux_memory.h"
#include "../conduit_ipc.h"

// =============================================================================
// Global Driver Registry
// =============================================================================

static resonance_registry_t g_driver_registry = {
    .initialized = false,
    .driver_count = 0,
    .device_count = 0
};

static resonance_driver_t* g_drivers[MAX_DRIVERS];
static device_node_t* g_devices[MAX_DEVICES];
static spinlock_t g_driver_lock = SPINLOCK_INIT;

// Bus managers
static bus_manager_t g_bus_managers[BUS_TYPE_MAX];

// =============================================================================
// Device Tree
// =============================================================================

static device_tree_t g_device_tree = {
    .root = NULL,
    .node_count = 0
};

// =============================================================================
// Bus Management
// =============================================================================

static void bus_manager_init(bus_type_t bus_type) {
    bus_manager_t* manager = &g_bus_managers[bus_type];
    
    manager->bus_type = bus_type;
    manager->device_count = 0;
    manager->initialized = false;
    spinlock_init(&manager->lock);
    
    switch (bus_type) {
        case BUS_TYPE_PCI:
            manager->scan = pci_bus_scan;
            manager->configure = pci_bus_configure;
            break;
            
        case BUS_TYPE_USB:
            manager->scan = usb_bus_scan;
            manager->configure = usb_bus_configure;
            break;
            
        case BUS_TYPE_VIRTIO:
            manager->scan = virtio_bus_scan;
            manager->configure = virtio_bus_configure;
            break;
            
        case BUS_TYPE_THUNDERBOLT:
            manager->scan = thunderbolt_bus_scan;
            manager->configure = thunderbolt_bus_configure;
            break;
            
        default:
            manager->scan = NULL;
            manager->configure = NULL;
            break;
    }
}

// =============================================================================
// PCI Bus Operations
// =============================================================================

static int pci_bus_scan(bus_manager_t* manager) {
    uint32_t device_count = 0;
    
    // Scan all PCI buses
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            for (uint8_t function = 0; function < 8; function++) {
                uint32_t vendor_device = pci_config_read(bus, device, function, 0);
                uint16_t vendor_id = vendor_device & 0xFFFF;
                uint16_t device_id = vendor_device >> 16;
                
                if (vendor_id == 0xFFFF) {
                    continue;  // No device
                }
                
                // Found a device, create node
                device_node_t* node = resonance_create_device_node();
                if (!node) {
                    continue;
                }
                
                node->bus_type = BUS_TYPE_PCI;
                node->vendor_id = vendor_id;
                node->device_id = device_id;
                
                // Read class and subclass
                uint32_t class_info = pci_config_read(bus, device, function, 0x08);
                node->class_code = (class_info >> 24) & 0xFF;
                node->subclass_code = (class_info >> 16) & 0xFF;
                
                // Store PCI location
                pci_device_info_t* pci_info = flux_allocate(NULL, 
                    sizeof(pci_device_info_t), FLUX_ALLOC_KERNEL);
                pci_info->bus = bus;
                pci_info->device = device;
                pci_info->function = function;
                pci_info->bar_count = 0;
                
                // Read BARs
                for (int bar = 0; bar < 6; bar++) {
                    uint32_t bar_value = pci_config_read(bus, device, function, 
                                                         0x10 + bar * 4);
                    if (bar_value) {
                        pci_info->bars[pci_info->bar_count++] = bar_value;
                    }
                }
                
                // Read IRQ
                uint32_t irq_info = pci_config_read(bus, device, function, 0x3C);
                pci_info->irq_line = irq_info & 0xFF;
                
                node->bus_specific_data = pci_info;
                
                // Add to device tree
                resonance_add_device(node);
                device_count++;
                
                // Check if multi-function device
                if (function == 0) {
                    uint32_t header = pci_config_read(bus, device, function, 0x0C);
                    if (!((header >> 16) & 0x80)) {
                        break;  // Single function device
                    }
                }
            }
        }
    }
    
    manager->device_count = device_count;
    return device_count;
}

static int pci_bus_configure(bus_manager_t* manager) {
    // Enable bus mastering, memory space, I/O space for all devices
    for (uint32_t i = 0; i < g_device_tree.node_count; i++) {
        device_node_t* node = g_devices[i];
        if (node && node->bus_type == BUS_TYPE_PCI) {
            pci_device_info_t* pci_info = (pci_device_info_t*)node->bus_specific_data;
            
            // Read command register
            uint32_t command = pci_config_read(pci_info->bus, pci_info->device,
                                              pci_info->function, 0x04);
            
            // Enable bus mastering, memory space, I/O space
            command |= 0x07;
            pci_config_write(pci_info->bus, pci_info->device,
                            pci_info->function, 0x04, command);
        }
    }
    
    return 0;
}

static uint32_t pci_config_read(uint8_t bus, uint8_t device, 
                                uint8_t function, uint8_t offset) {
    uint32_t address = (1 << 31) | (bus << 16) | (device << 11) | 
                      (function << 8) | (offset & 0xFC);
    
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

static void pci_config_write(uint8_t bus, uint8_t device,
                            uint8_t function, uint8_t offset, uint32_t value) {
    uint32_t address = (1 << 31) | (bus << 16) | (device << 11) |
                      (function << 8) | (offset & 0xFC);
    
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

// =============================================================================
// USB Bus Operations
// =============================================================================

static int usb_bus_scan(bus_manager_t* manager) {
    // USB scanning would involve EHCI/XHCI controller enumeration
    // Simplified for now
    return 0;
}

static int usb_bus_configure(bus_manager_t* manager) {
    // USB configuration
    return 0;
}

// =============================================================================
// VirtIO Bus Operations
// =============================================================================

static int virtio_bus_scan(bus_manager_t* manager) {
    // VirtIO device discovery via PCI or MMIO
    uint32_t device_count = 0;
    
    // Scan for VirtIO PCI devices (vendor ID 0x1AF4)
    for (uint32_t i = 0; i < g_device_tree.node_count; i++) {
        device_node_t* node = g_devices[i];
        if (node && node->bus_type == BUS_TYPE_PCI && node->vendor_id == 0x1AF4) {
            // This is a VirtIO device
            node->bus_type = BUS_TYPE_VIRTIO;  // Reclassify
            device_count++;
        }
    }
    
    manager->device_count = device_count;
    return device_count;
}

static int virtio_bus_configure(bus_manager_t* manager) {
    // VirtIO configuration
    return 0;
}

// =============================================================================
// Thunderbolt Bus Operations
// =============================================================================

static int thunderbolt_bus_scan(bus_manager_t* manager) {
    // Thunderbolt/USB4 scanning
    return 0;
}

static int thunderbolt_bus_configure(bus_manager_t* manager) {
    // Thunderbolt configuration
    return 0;
}

// =============================================================================
// Core Driver Framework
// =============================================================================

void resonance_init(void) {
    spinlock_acquire(&g_driver_lock);
    
    // Clear driver and device arrays
    for (int i = 0; i < MAX_DRIVERS; i++) {
        g_drivers[i] = NULL;
    }
    
    for (int i = 0; i < MAX_DEVICES; i++) {
        g_devices[i] = NULL;
    }
    
    // Initialize bus managers
    for (int i = 0; i < BUS_TYPE_MAX; i++) {
        bus_manager_init(i);
    }
    
    // Initialize device tree
    g_device_tree.root = NULL;
    g_device_tree.node_count = 0;
    
    g_driver_registry.initialized = true;
    
    spinlock_release(&g_driver_lock);
    
    // Perform initial bus scans
    resonance_scan_all_buses();
}

int resonance_register_driver(resonance_driver_t* driver) {
    if (!driver || !driver->name) {
        return -1;
    }
    
    spinlock_acquire(&g_driver_lock);
    
    // Find free slot
    int slot = -1;
    for (int i = 0; i < MAX_DRIVERS; i++) {
        if (!g_drivers[i]) {
            slot = i;
            break;
        }
    }
    
    if (slot == -1) {
        spinlock_release(&g_driver_lock);
        return -1;
    }
    
    // Register driver
    g_drivers[slot] = driver;
    driver->id = slot;
    driver->state = DRIVER_STATE_REGISTERED;
    g_driver_registry.driver_count++;
    
    spinlock_release(&g_driver_lock);
    
    // Try to match with existing devices
    resonance_probe_devices(driver);
    
    return 0;
}

void resonance_unregister_driver(resonance_driver_t* driver) {
    if (!driver) {
        return;
    }
    
    spinlock_acquire(&g_driver_lock);
    
    // Detach from all devices
    for (uint32_t i = 0; i < g_device_tree.node_count; i++) {
        device_node_t* node = g_devices[i];
        if (node && node->driver == driver) {
            if (driver->detach) {
                driver->detach(node->handle);
            }
            node->driver = NULL;
            node->handle = NULL;
        }
    }
    
    // Remove from registry
    g_drivers[driver->id] = NULL;
    driver->state = DRIVER_STATE_UNREGISTERED;
    g_driver_registry.driver_count--;
    
    spinlock_release(&g_driver_lock);
}

// =============================================================================
// Device Management
// =============================================================================

device_node_t* resonance_create_device_node(void) {
    device_node_t* node = flux_allocate(NULL, sizeof(device_node_t),
                                        FLUX_ALLOC_KERNEL | FLUX_ALLOC_ZERO);
    if (!node) {
        return NULL;
    }
    
    node->state = DEVICE_STATE_DISCOVERED;
    spinlock_init(&node->lock);
    
    return node;
}

int resonance_add_device(device_node_t* node) {
    if (!node) {
        return -1;
    }
    
    spinlock_acquire(&g_driver_lock);
    
    // Find free slot
    int slot = -1;
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (!g_devices[i]) {
            slot = i;
            break;
        }
    }
    
    if (slot == -1) {
        spinlock_release(&g_driver_lock);
        return -1;
    }
    
    // Add to device array
    g_devices[slot] = node;
    node->id = slot;
    g_device_tree.node_count++;
    g_driver_registry.device_count++;
    
    spinlock_release(&g_driver_lock);
    
    // Try to find matching driver
    resonance_match_driver(node);
    
    return 0;
}

void resonance_remove_device(device_node_t* node) {
    if (!node) {
        return;
    }
    
    spinlock_acquire(&g_driver_lock);
    
    // Detach driver if attached
    if (node->driver && node->driver->detach) {
        node->driver->detach(node->handle);
    }
    
    // Remove from device array
    g_devices[node->id] = NULL;
    g_device_tree.node_count--;
    g_driver_registry.device_count--;
    
    spinlock_release(&g_driver_lock);
    
    // Free resources
    if (node->bus_specific_data) {
        flux_free(node->bus_specific_data);
    }
    
    flux_free(node);
}

// =============================================================================
// Driver Matching
// =============================================================================

static bool driver_matches_device(resonance_driver_t* driver, device_node_t* node) {
    // Check vendor/device ID match
    for (int i = 0; driver->vendor_ids[i] != 0; i++) {
        if (driver->vendor_ids[i] == node->vendor_id) {
            for (int j = 0; driver->device_ids[j] != 0; j++) {
                if (driver->device_ids[j] == node->device_id) {
                    return true;
                }
            }
        }
    }
    
    // Check class match
    if (driver->class_code != 0xFF && driver->class_code == node->class_code) {
        if (driver->subclass_code == 0xFF || 
            driver->subclass_code == node->subclass_code) {
            return true;
        }
    }
    
    return false;
}

void resonance_match_driver(device_node_t* node) {
    if (!node) {
        return;
    }
    
    spinlock_acquire(&g_driver_lock);
    
    // Find matching driver
    for (int i = 0; i < MAX_DRIVERS; i++) {
        resonance_driver_t* driver = g_drivers[i];
        if (driver && driver->state == DRIVER_STATE_REGISTERED) {
            if (driver_matches_device(driver, node)) {
                // Found a match, try to probe
                spinlock_release(&g_driver_lock);
                
                if (driver->probe) {
                    void* handle = driver->probe(node);
                    if (handle) {
                        // Successful probe
                        node->driver = driver;
                        node->handle = handle;
                        node->state = DEVICE_STATE_CONFIGURED;
                        
                        // Attach driver
                        if (driver->attach) {
                            driver->attach(handle);
                        }
                    }
                }
                
                return;
            }
        }
    }
    
    spinlock_release(&g_driver_lock);
}

void resonance_probe_devices(resonance_driver_t* driver) {
    if (!driver) {
        return;
    }
    
    // Try to match with all unbound devices
    for (uint32_t i = 0; i < g_device_tree.node_count; i++) {
        device_node_t* node = g_devices[i];
        if (node && !node->driver) {
            if (driver_matches_device(driver, node)) {
                if (driver->probe) {
                    void* handle = driver->probe(node);
                    if (handle) {
                        node->driver = driver;
                        node->handle = handle;
                        node->state = DEVICE_STATE_CONFIGURED;
                        
                        if (driver->attach) {
                            driver->attach(handle);
                        }
                    }
                }
            }
        }
    }
}

// =============================================================================
// Bus Scanning
// =============================================================================

void resonance_scan_all_buses(void) {
    // Scan each bus type
    for (int i = 0; i < BUS_TYPE_MAX; i++) {
        bus_manager_t* manager = &g_bus_managers[i];
        if (manager->scan) {
            int count = manager->scan(manager);
            manager->initialized = true;
            
            // Configure discovered devices
            if (count > 0 && manager->configure) {
                manager->configure(manager);
            }
        }
    }
}

// =============================================================================
// Interrupt Handling
// =============================================================================

int resonance_register_irq(device_handle_t* handle, uint32_t irq,
                          irq_handler_t handler, void* context) {
    if (!handle || !handler) {
        return -1;
    }
    
    // Register IRQ handler with interrupt controller
    // This would integrate with the kernel's interrupt management
    
    handle->irq_vectors[handle->irq_count].irq = irq;
    handle->irq_vectors[handle->irq_count].handler = handler;
    handle->irq_vectors[handle->irq_count].context = context;
    handle->irq_count++;
    
    return 0;
}

void resonance_unregister_irq(device_handle_t* handle, uint32_t irq) {
    if (!handle) {
        return;
    }
    
    // Remove IRQ handler
    for (uint32_t i = 0; i < handle->irq_count; i++) {
        if (handle->irq_vectors[i].irq == irq) {
            // Shift remaining entries
            for (uint32_t j = i; j < handle->irq_count - 1; j++) {
                handle->irq_vectors[j] = handle->irq_vectors[j + 1];
            }
            handle->irq_count--;
            break;
        }
    }
}

// =============================================================================
// DMA Operations
// =============================================================================

dma_region_t* resonance_alloc_dma(size_t size, uint32_t flags) {
    dma_region_t* region = flux_allocate(NULL, sizeof(dma_region_t),
                                        FLUX_ALLOC_KERNEL);
    if (!region) {
        return NULL;
    }
    
    // Allocate DMA-capable memory
    region->virtual_addr = flux_allocate(NULL, size,
                                         FLUX_ALLOC_KERNEL | FLUX_ALLOC_DMA);
    if (!region->virtual_addr) {
        flux_free(region);
        return NULL;
    }
    
    // Get physical address for DMA
    region->physical_addr = flux_translate_address(NULL, 
                                                   (uint64_t)region->virtual_addr);
    region->size = size;
    region->flags = flags;
    
    return region;
}

void resonance_free_dma(dma_region_t* region) {
    if (!region) {
        return;
    }
    
    if (region->virtual_addr) {
        flux_free(region->virtual_addr);
    }
    
    flux_free(region);
}

// =============================================================================
// I/O Operations
// =============================================================================

io_result_t resonance_io_request(device_handle_t* handle, io_packet_t* packet) {
    if (!handle || !packet) {
        return IO_ERROR;
    }
    
    // Dispatch to driver's I/O handler
    device_node_t* node = handle->device_node;
    if (node && node->driver && node->driver->io_request) {
        return node->driver->io_request(handle, packet);
    }
    
    return IO_ERROR;
}

// =============================================================================
// Helper Functions
// =============================================================================

void outb(uint16_t port, uint8_t value) {
    __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void outw(uint16_t port, uint16_t value) {
    __asm__ __volatile__("outw %0, %1" : : "a"(value), "Nd"(port));
}

uint16_t inw(uint16_t port) {
    uint16_t value;
    __asm__ __volatile__("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void outl(uint16_t port, uint32_t value) {
    __asm__ __volatile__("outl %0, %1" : : "a"(value), "Nd"(port));
}

uint32_t inl(uint16_t port) {
    uint32_t value;
    __asm__ __volatile__("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void io_wait(void) {
    // Port 0x80 is used for POST codes and creates a small delay
    outb(0x80, 0);
}
