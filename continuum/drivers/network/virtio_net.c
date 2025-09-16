/*
 * VirtIO Network Driver for Continuum Kernel
 * Virtual network device driver for virtualized environments
 */

#include "virtio_net.h"
#include "../resonance.h"
#include "../../flux_memory.h"

// =============================================================================
// Global VirtIO Net State
// =============================================================================

static virtio_net_device_t* g_virtio_net_devices[MAX_VIRTIO_NET_DEVICES];
static uint32_t g_virtio_net_count = 0;
static spinlock_t g_virtio_net_lock = SPINLOCK_INIT;

// =============================================================================
// VirtIO Configuration Access (shared with virtio_block)
// =============================================================================

static uint8_t virtio_read8(virtio_net_device_t* dev, uint32_t offset) {
    if (dev->common_cfg) {
        return mmio_read8(dev->common_cfg + offset);
    } else {
        return inb(dev->io_base + offset);
    }
}

static uint16_t virtio_read16(virtio_net_device_t* dev, uint32_t offset) {
    if (dev->common_cfg) {
        return mmio_read16(dev->common_cfg + offset);
    } else {
        return inw(dev->io_base + offset);
    }
}

static uint32_t virtio_read32(virtio_net_device_t* dev, uint32_t offset) {
    if (dev->common_cfg) {
        return mmio_read32(dev->common_cfg + offset);
    } else {
        return inl(dev->io_base + offset);
    }
}

static void virtio_write8(virtio_net_device_t* dev, uint32_t offset, uint8_t value) {
    if (dev->common_cfg) {
        mmio_write8(dev->common_cfg + offset, value);
    } else {
        outb(dev->io_base + offset, value);
    }
}

static void virtio_write16(virtio_net_device_t* dev, uint32_t offset, uint16_t value) {
    if (dev->common_cfg) {
        mmio_write16(dev->common_cfg + offset, value);
    } else {
        outw(dev->io_base + offset, value);
    }
}

static void virtio_write32(virtio_net_device_t* dev, uint32_t offset, uint32_t value) {
    if (dev->common_cfg) {
        mmio_write32(dev->common_cfg + offset, value);
    } else {
        outl(dev->io_base + offset, value);
    }
}

// =============================================================================
// VirtQueue Management
// =============================================================================

static virtio_net_queue_t* virtqueue_create_net(virtio_net_device_t* dev,
                                                uint16_t queue_idx, uint16_t queue_size) {
    virtio_net_queue_t* vq = flux_allocate(NULL, sizeof(virtio_net_queue_t),
                                          FLUX_ALLOC_KERNEL | FLUX_ALLOC_ZERO);
    if (!vq) {
        return NULL;
    }
    
    vq->queue_idx = queue_idx;
    vq->queue_size = queue_size;
    vq->last_used_idx = 0;
    vq->device = dev;
    
    // Calculate memory requirements
    size_t desc_size = queue_size * sizeof(virtq_desc_t);
    size_t avail_size = sizeof(virtq_avail_t) + queue_size * sizeof(uint16_t);
    size_t used_size = sizeof(virtq_used_t) + queue_size * sizeof(virtq_used_elem_t);
    size_t total_size = desc_size + avail_size + used_size;
    
    // Allocate queue memory
    vq->queue_dma = resonance_alloc_dma(total_size, DMA_FLAG_COHERENT);
    if (!vq->queue_dma) {
        flux_free(vq);
        return NULL;
    }
    
    // Setup queue pointers
    vq->desc = (virtq_desc_t*)vq->queue_dma->virtual_addr;
    vq->avail = (virtq_avail_t*)((uint8_t*)vq->desc + desc_size);
    vq->used = (virtq_used_t*)((uint8_t*)vq->avail + avail_size);
    
    // Clear queue memory
    memset(vq->desc, 0, desc_size);
    memset(vq->avail, 0, avail_size);
    memset(vq->used, 0, used_size);
    
    // Initialize free descriptor list
    vq->free_head = 0;
    for (uint16_t i = 0; i < queue_size - 1; i++) {
        vq->desc[i].next = i + 1;
    }
    vq->desc[queue_size - 1].next = 0xFFFF;
    
    // Allocate buffers for this queue
    for (int i = 0; i < queue_size; i++) {
        vq->buffers[i] = resonance_alloc_dma(VIRTIO_NET_BUFFER_SIZE,
                                            DMA_FLAG_COHERENT);
        if (!vq->buffers[i]) {
            // Clean up
            for (int j = 0; j < i; j++) {
                resonance_free_dma(vq->buffers[j]);
            }
            resonance_free_dma(vq->queue_dma);
            flux_free(vq);
            return NULL;
        }
    }
    
    spinlock_init(&vq->lock);
    
    return vq;
}

static void virtqueue_destroy_net(virtio_net_queue_t* vq) {
    if (!vq) {
        return;
    }
    
    for (int i = 0; i < vq->queue_size; i++) {
        if (vq->buffers[i]) {
            resonance_free_dma(vq->buffers[i]);
        }
    }
    
    if (vq->queue_dma) {
        resonance_free_dma(vq->queue_dma);
    }
    
    flux_free(vq);
}

// =============================================================================
// Packet Transmission
// =============================================================================

int virtio_net_send_packet(virtio_net_device_t* dev, void* data, size_t length) {
    if (!dev || !data || length > VIRTIO_NET_MAX_PACKET_SIZE) {
        return -1;
    }
    
    virtio_net_queue_t* vq = dev->tx_queue;
    spinlock_acquire(&vq->lock);
    
    // Allocate descriptor
    if (vq->free_head == 0xFFFF) {
        spinlock_release(&vq->lock);
        return -1;
    }
    
    uint16_t desc_idx = vq->free_head;
    vq->free_head = vq->desc[desc_idx].next;
    
    // Setup packet with net header
    virtio_net_hdr_t* hdr = (virtio_net_hdr_t*)vq->buffers[desc_idx]->virtual_addr;
    hdr->flags = 0;
    hdr->gso_type = VIRTIO_NET_HDR_GSO_NONE;
    hdr->hdr_len = sizeof(virtio_net_hdr_t);
    hdr->gso_size = 0;
    hdr->csum_start = 0;
    hdr->csum_offset = 0;
    
    // Copy packet data after header
    memcpy((uint8_t*)hdr + sizeof(virtio_net_hdr_t), data, length);
    
    // Setup descriptor
    vq->desc[desc_idx].addr = vq->buffers[desc_idx]->physical_addr;
    vq->desc[desc_idx].len = sizeof(virtio_net_hdr_t) + length;
    vq->desc[desc_idx].flags = 0;
    vq->desc[desc_idx].next = 0;
    
    // Add to available ring
    uint16_t avail_idx = vq->avail->idx % vq->queue_size;
    vq->avail->ring[avail_idx] = desc_idx;
    __sync_synchronize();
    vq->avail->idx++;
    
    spinlock_release(&vq->lock);
    
    // Notify device
    if (dev->common_cfg) {
        mmio_write16(dev->common_cfg + VIRTIO_PCI_QUEUE_NOTIFY, vq->queue_idx);
    } else {
        outw(dev->io_base + VIRTIO_PCI_QUEUE_NOTIFY, vq->queue_idx);
    }
    
    dev->stats.tx_packets++;
    dev->stats.tx_bytes += length;
    
    return 0;
}

// =============================================================================
// Packet Reception
// =============================================================================

int virtio_net_receive_packet(virtio_net_device_t* dev, void* buffer, size_t max_len) {
    if (!dev || !buffer) {
        return -1;
    }
    
    virtio_net_queue_t* vq = dev->rx_queue;
    spinlock_acquire(&vq->lock);
    
    // Check for completed buffers
    if (vq->last_used_idx == vq->used->idx) {
        spinlock_release(&vq->lock);
        return 0;  // No packets
    }
    
    // Get completed descriptor
    uint16_t used_idx = vq->last_used_idx % vq->queue_size;
    uint32_t desc_idx = vq->used->ring[used_idx].id;
    uint32_t len = vq->used->ring[used_idx].len;
    
    // Skip virtio net header
    if (len > sizeof(virtio_net_hdr_t)) {
        len -= sizeof(virtio_net_hdr_t);
        if (len > max_len) {
            len = max_len;
        }
        
        uint8_t* packet_data = (uint8_t*)vq->buffers[desc_idx]->virtual_addr +
                              sizeof(virtio_net_hdr_t);
        memcpy(buffer, packet_data, len);
    } else {
        len = 0;
    }
    
    // Re-add descriptor to receive queue
    vq->desc[desc_idx].addr = vq->buffers[desc_idx]->physical_addr;
    vq->desc[desc_idx].len = VIRTIO_NET_BUFFER_SIZE;
    vq->desc[desc_idx].flags = VIRTQ_DESC_F_WRITE;  // Device writes to buffer
    vq->desc[desc_idx].next = 0;
    
    uint16_t avail_idx = vq->avail->idx % vq->queue_size;
    vq->avail->ring[avail_idx] = desc_idx;
    __sync_synchronize();
    vq->avail->idx++;
    
    vq->last_used_idx++;
    
    dev->stats.rx_packets++;
    dev->stats.rx_bytes += len;
    
    spinlock_release(&vq->lock);
    
    return len;
}

// =============================================================================
// Feature Negotiation
// =============================================================================

static int virtio_net_negotiate_features(virtio_net_device_t* dev) {
    // Read device features
    dev->device_features = virtio_read32(dev, VIRTIO_PCI_DEVICE_FEATURES);
    
    // Select features we support
    dev->driver_features = 0;
    
    if (dev->device_features & VIRTIO_NET_F_MAC) {
        dev->driver_features |= VIRTIO_NET_F_MAC;
    }
    if (dev->device_features & VIRTIO_NET_F_STATUS) {
        dev->driver_features |= VIRTIO_NET_F_STATUS;
    }
    if (dev->device_features & VIRTIO_NET_F_MRG_RXBUF) {
        dev->driver_features |= VIRTIO_NET_F_MRG_RXBUF;
    }
    if (dev->device_features & VIRTIO_NET_F_CSUM) {
        dev->driver_features |= VIRTIO_NET_F_CSUM;
        dev->has_csum = true;
    }
    
    // Write accepted features
    virtio_write32(dev, VIRTIO_PCI_DRIVER_FEATURES, dev->driver_features);
    
    return 0;
}

// =============================================================================
// Device Configuration
// =============================================================================

static int virtio_net_read_config(virtio_net_device_t* dev) {
    // Read MAC address if provided
    if (dev->driver_features & VIRTIO_NET_F_MAC) {
        for (int i = 0; i < 6; i++) {
            dev->mac_addr[i] = virtio_read8(dev, VIRTIO_NET_CFG_MAC + i);
        }
    } else {
        // Generate random MAC with locally administered bit set
        dev->mac_addr[0] = 0x52;
        dev->mac_addr[1] = 0x54;
        dev->mac_addr[2] = 0x00;
        dev->mac_addr[3] = 0x12;
        dev->mac_addr[4] = 0x34;
        dev->mac_addr[5] = 0x56;
    }
    
    // Read status if supported
    if (dev->driver_features & VIRTIO_NET_F_STATUS) {
        dev->status = virtio_read16(dev, VIRTIO_NET_CFG_STATUS);
        dev->link_up = (dev->status & VIRTIO_NET_S_LINK_UP) ? true : false;
    } else {
        dev->link_up = true;  // Assume link is up
    }
    
    // Read max virtqueue pairs if supported
    if (dev->driver_features & VIRTIO_NET_F_MQ) {
        dev->max_queue_pairs = virtio_read16(dev, VIRTIO_NET_CFG_MAX_VQ_PAIRS);
    } else {
        dev->max_queue_pairs = 1;
    }
    
    return 0;
}

// =============================================================================
// Device Initialization
// =============================================================================

static int virtio_net_init_device(virtio_net_device_t* dev) {
    // Reset device
    virtio_write8(dev, VIRTIO_PCI_STATUS, 0);
    
    // Set ACKNOWLEDGE status
    virtio_write8(dev, VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);
    
    // Set DRIVER status
    virtio_write8(dev, VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACKNOWLEDGE |
                                          VIRTIO_STATUS_DRIVER);
    
    // Negotiate features
    if (virtio_net_negotiate_features(dev) != 0) {
        return -1;
    }
    
    // Set FEATURES_OK
    virtio_write8(dev, VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACKNOWLEDGE |
                                          VIRTIO_STATUS_DRIVER |
                                          VIRTIO_STATUS_FEATURES_OK);
    
    // Verify FEATURES_OK
    uint8_t status = virtio_read8(dev, VIRTIO_PCI_STATUS);
    if (!(status & VIRTIO_STATUS_FEATURES_OK)) {
        return -1;
    }
    
    // Setup virtqueues (RX queue = 0, TX queue = 1)
    
    // RX queue
    virtio_write16(dev, VIRTIO_PCI_QUEUE_SEL, 0);
    uint16_t rx_queue_size = virtio_read16(dev, VIRTIO_PCI_QUEUE_SIZE);
    if (rx_queue_size == 0) {
        return -1;
    }
    
    dev->rx_queue = virtqueue_create_net(dev, 0, rx_queue_size);
    if (!dev->rx_queue) {
        return -1;
    }
    
    virtio_write32(dev, VIRTIO_PCI_QUEUE_PFN,
                   dev->rx_queue->queue_dma->physical_addr >> 12);
    
    // Add receive buffers
    for (int i = 0; i < rx_queue_size; i++) {
        dev->rx_queue->desc[i].addr = dev->rx_queue->buffers[i]->physical_addr;
        dev->rx_queue->desc[i].len = VIRTIO_NET_BUFFER_SIZE;
        dev->rx_queue->desc[i].flags = VIRTQ_DESC_F_WRITE;
        dev->rx_queue->desc[i].next = 0;
        
        dev->rx_queue->avail->ring[i] = i;
    }
    dev->rx_queue->avail->idx = rx_queue_size;
    
    // TX queue
    virtio_write16(dev, VIRTIO_PCI_QUEUE_SEL, 1);
    uint16_t tx_queue_size = virtio_read16(dev, VIRTIO_PCI_QUEUE_SIZE);
    if (tx_queue_size == 0) {
        virtqueue_destroy_net(dev->rx_queue);
        return -1;
    }
    
    dev->tx_queue = virtqueue_create_net(dev, 1, tx_queue_size);
    if (!dev->tx_queue) {
        virtqueue_destroy_net(dev->rx_queue);
        return -1;
    }
    
    virtio_write32(dev, VIRTIO_PCI_QUEUE_PFN,
                   dev->tx_queue->queue_dma->physical_addr >> 12);
    
    // Read device configuration
    if (virtio_net_read_config(dev) != 0) {
        virtqueue_destroy_net(dev->rx_queue);
        virtqueue_destroy_net(dev->tx_queue);
        return -1;
    }
    
    // Set DRIVER_OK
    virtio_write8(dev, VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACKNOWLEDGE |
                                          VIRTIO_STATUS_DRIVER |
                                          VIRTIO_STATUS_FEATURES_OK |
                                          VIRTIO_STATUS_DRIVER_OK);
    
    return 0;
}

// =============================================================================
// Driver Interface
// =============================================================================

static void* virtio_net_probe(device_node_t* node) {
    // Check for VirtIO network device (device ID 0x1000)
    if (node->vendor_id != 0x1AF4 || node->device_id != 0x1000) {
        return NULL;
    }
    
    virtio_net_device_t* dev = flux_allocate(NULL, sizeof(virtio_net_device_t),
                                            FLUX_ALLOC_KERNEL | FLUX_ALLOC_ZERO);
    if (!dev) {
        return NULL;
    }
    
    // Get PCI resources
    pci_device_info_t* pci_info = (pci_device_info_t*)node->bus_specific_data;
    
    // Check for I/O or MMIO
    if (pci_info->bars[0] & 0x01) {
        // I/O space
        dev->io_base = pci_info->bars[0] & ~0x03;
        dev->common_cfg = NULL;
    } else {
        // Memory space
        dev->common_cfg = (void*)(uintptr_t)(pci_info->bars[0] & ~0x0F);
        dev->io_base = 0;
    }
    
    // Initialize device
    if (virtio_net_init_device(dev) != 0) {
        flux_free(dev);
        return NULL;
    }
    
    // Add to global list
    spinlock_acquire(&g_virtio_net_lock);
    g_virtio_net_devices[g_virtio_net_count++] = dev;
    spinlock_release(&g_virtio_net_lock);
    
    return dev;
}

static int virtio_net_attach(device_handle_t* handle) {
    virtio_net_device_t* dev = (virtio_net_device_t*)handle->driver_data;
    dev->state = VIRTIO_NET_STATE_READY;
    return 0;
}

static void virtio_net_detach(device_handle_t* handle) {
    virtio_net_device_t* dev = (virtio_net_device_t*)handle->driver_data;
    
    // Reset device
    virtio_write8(dev, VIRTIO_PCI_STATUS, 0);
    
    // Free queues
    if (dev->rx_queue) {
        virtqueue_destroy_net(dev->rx_queue);
    }
    if (dev->tx_queue) {
        virtqueue_destroy_net(dev->tx_queue);
    }
    
    dev->state = VIRTIO_NET_STATE_DISABLED;
}

// Driver registration
static resonance_driver_t virtio_net_driver = {
    .name = "virtio-net",
    .vendor_ids = {0x1AF4, 0},
    .device_ids = {0x1000, 0},  // VirtIO network device
    .probe = virtio_net_probe,
    .attach = virtio_net_attach,
    .detach = virtio_net_detach
};

void virtio_net_init(void) {
    resonance_register_driver(&virtio_net_driver);
}
