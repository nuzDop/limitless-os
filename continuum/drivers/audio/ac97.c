/*
 * AC'97 Audio Codec Driver for Continuum Kernel
 * Intel Audio Codec '97 specification implementation
 */

#include "ac97.h"
#include "../resonance.h"
#include "../../flux_memory.h"

// =============================================================================
// Global AC'97 State
// =============================================================================

static ac97_controller_t* g_ac97_controllers[MAX_AC97_CONTROLLERS];
static uint32_t g_ac97_count = 0;
static spinlock_t g_ac97_lock = SPINLOCK_INIT;

// =============================================================================
// Codec Access
// =============================================================================

static uint16_t ac97_codec_read(ac97_controller_t* ac97, uint8_t reg) {
    // Wait for codec ready
    uint64_t timeout = continuum_get_time() + 1000000;
    while (continuum_get_time() < timeout) {
        if (!(inb(ac97->nabmbar + AC97_CAS) & AC97_CAS_ACTIVE)) {
            break;
        }
        io_wait();
    }
    
    return inw(ac97->nambar + reg);
}

static void ac97_codec_write(ac97_controller_t* ac97, uint8_t reg, uint16_t value) {
    // Wait for codec ready
    uint64_t timeout = continuum_get_time() + 1000000;
    while (continuum_get_time() < timeout) {
        if (!(inb(ac97->nabmbar + AC97_CAS) & AC97_CAS_ACTIVE)) {
            break;
        }
        io_wait();
    }
    
    outw(ac97->nambar + reg, value);
}

// =============================================================================
// Buffer Descriptor List Management
// =============================================================================

static int ac97_setup_bdl(ac97_controller_t* ac97, ac97_channel_t* channel,
                         void* buffer, size_t size) {
    // Allocate BDL if not already allocated
    if (!channel->bdl_dma) {
        channel->bdl_dma = resonance_alloc_dma(sizeof(ac97_bdl_entry_t) * AC97_BDL_ENTRIES,
                                              DMA_FLAG_COHERENT);
        if (!channel->bdl_dma) {
            return -1;
        }
        channel->bdl = (ac97_bdl_entry_t*)channel->bdl_dma->virtual_addr;
    }
    
    // Allocate audio buffer
    if (!channel->buffer_dma || channel->buffer_size < size) {
        if (channel->buffer_dma) {
            resonance_free_dma(channel->buffer_dma);
        }
        
        channel->buffer_dma = resonance_alloc_dma(size, DMA_FLAG_COHERENT);
        if (!channel->buffer_dma) {
            return -1;
        }
        channel->buffer = channel->buffer_dma->virtual_addr;
        channel->buffer_size = size;
    }
    
    // Copy audio data
    memcpy(channel->buffer, buffer, size);
    
    // Setup BDL entries
    uint32_t bytes_per_entry = AC97_BDL_BUFFER_SIZE;
    uint32_t entries = (size + bytes_per_entry - 1) / bytes_per_entry;
    
    if (entries > AC97_BDL_ENTRIES) {
        entries = AC97_BDL_ENTRIES;
    }
    
    uint32_t offset = 0;
    for (uint32_t i = 0; i < entries; i++) {
        uint32_t entry_size = (size - offset > bytes_per_entry) ? 
                             bytes_per_entry : (size - offset);
        
        channel->bdl[i].address = channel->buffer_dma->physical_addr + offset;
        channel->bdl[i].samples = entry_size / (channel->bits_per_sample / 8);
        channel->bdl[i].flags = AC97_BDL_FLAG_IOC;  // Interrupt on completion
        
        if (i == entries - 1) {
            channel->bdl[i].flags |= AC97_BDL_FLAG_BUP;  // Buffer underrun policy
        }
        
        offset += entry_size;
    }
    
    channel->bdl_entries = entries;
    
    // Set BDL address register
    outl(channel->base + AC97_BDBAR, channel->bdl_dma->physical_addr);
    
    // Set last valid index
    outb(channel->base + AC97_LVI, entries - 1);
    
    return 0;
}

// =============================================================================
// Playback Control
// =============================================================================

int ac97_play(ac97_controller_t* ac97, void* buffer, size_t size,
             uint32_t sample_rate, uint8_t channels, uint8_t bits) {
    if (!ac97 || !buffer || size == 0) {
        return -1;
    }
    
    spinlock_acquire(&ac97->lock);
    
    ac97_channel_t* channel = &ac97->pcm_out;
    
    // Stop current playback
    ac97_stop(ac97);
    
    // Configure channel
    channel->sample_rate = sample_rate;
    channel->channels = channels;
    channel->bits_per_sample = bits;
    
    // Set sample rate
    if (ac97->capabilities & AC97_CAP_VARIABLE_RATE) {
        ac97_codec_write(ac97, AC97_PCM_FRONT_RATE, sample_rate);
    }
    
    // Setup buffer descriptor list
    if (ac97_setup_bdl(ac97, channel, buffer, size) != 0) {
        spinlock_release(&ac97->lock);
        return -1;
    }
    
    // Clear status
    outw(channel->base + AC97_SR, AC97_SR_FIFOE | AC97_SR_BCIS | AC97_SR_LVBCI);
    
    // Start playback
    outb(channel->base + AC97_CR, AC97_CR_RPBM | AC97_CR_IOCE);
    
    channel->playing = true;
    
    spinlock_release(&ac97->lock);
    return 0;
}

void ac97_stop(ac97_controller_t* ac97) {
    if (!ac97) {
        return;
    }
    
    spinlock_acquire(&ac97->lock);
    
    ac97_channel_t* channel = &ac97->pcm_out;
    
    // Stop DMA
    outb(channel->base + AC97_CR, 0);
    
    // Reset channel
    outb(channel->base + AC97_CR, AC97_CR_RR);
    
    // Wait for reset to complete
    uint64_t timeout = continuum_get_time() + 100000;
    while (continuum_get_time() < timeout) {
        if (!(inb(channel->base + AC97_CR) & AC97_CR_RR)) {
            break;
        }
        io_wait();
    }
    
    channel->playing = false;
    
    spinlock_release(&ac97->lock);
}

void ac97_pause(ac97_controller_t* ac97) {
    if (!ac97) {
        return;
    }
    
    spinlock_acquire(&ac97->lock);
    
    ac97_channel_t* channel = &ac97->pcm_out;
    
    if (channel->playing) {
        uint8_t cr = inb(channel->base + AC97_CR);
        cr &= ~AC97_CR_RPBM;
        outb(channel->base + AC97_CR, cr);
        channel->playing = false;
    }
    
    spinlock_release(&ac97->lock);
}

void ac97_resume(ac97_controller_t* ac97) {
    if (!ac97) {
        return;
    }
    
    spinlock_acquire(&ac97->lock);
    
    ac97_channel_t* channel = &ac97->pcm_out;
    
    if (!channel->playing && channel->buffer) {
        uint8_t cr = inb(channel->base + AC97_CR);
        cr |= AC97_CR_RPBM;
        outb(channel->base + AC97_CR, cr);
        channel->playing = true;
    }
    
    spinlock_release(&ac97->lock);
}

// =============================================================================
// Volume Control
// =============================================================================

void ac97_set_master_volume(ac97_controller_t* ac97, uint8_t left, uint8_t right) {
    if (!ac97) {
        return;
    }
    
    // Convert 0-100 to 0-63 (AC'97 uses 6-bit volume with 1.5dB steps)
    uint8_t left_val = (63 - (left * 63 / 100)) & 0x3F;
    uint8_t right_val = (63 - (right * 63 / 100)) & 0x3F;
    
    uint16_t volume = (left_val << 8) | right_val;
    
    // Set mute bit if volume is 0
    if (left == 0 && right == 0) {
        volume |= AC97_MUTE;
    }
    
    ac97_codec_write(ac97, AC97_MASTER_VOLUME, volume);
}

void ac97_set_pcm_volume(ac97_controller_t* ac97, uint8_t left, uint8_t right) {
    if (!ac97) {
        return;
    }
    
    uint8_t left_val = (31 - (left * 31 / 100)) & 0x1F;
    uint8_t right_val = (31 - (right * 31 / 100)) & 0x1F;
    
    uint16_t volume = (left_val << 8) | right_val;
    
    if (left == 0 && right == 0) {
        volume |= AC97_MUTE;
    }
    
    ac97_codec_write(ac97, AC97_PCM_OUT_VOLUME, volume);
}

uint8_t ac97_get_master_volume(ac97_controller_t* ac97) {
    if (!ac97) {
        return 0;
    }
    
    uint16_t volume = ac97_codec_read(ac97, AC97_MASTER_VOLUME);
    
    if (volume & AC97_MUTE) {
        return 0;
    }
    
    uint8_t left = ((volume >> 8) & 0x3F);
    return (63 - left) * 100 / 63;
}

// =============================================================================
// Interrupt Handler
// =============================================================================

static void ac97_interrupt(interrupt_frame_t* frame) {
    // Find which controller triggered the interrupt
    for (uint32_t i = 0; i < g_ac97_count; i++) {
        ac97_controller_t* ac97 = g_ac97_controllers[i];
        
        // Check global status
        uint32_t status = inl(ac97->nabmbar + AC97_GLOB_STA);
        
        if (status & AC97_GLOB_STA_POINT) {
            // PCM out interrupt
            ac97_channel_t* channel = &ac97->pcm_out;
            uint16_t sr = inw(channel->base + AC97_SR);
            
            if (sr & AC97_SR_BCIS) {
                // Buffer completion
                channel->interrupts++;
                
                // Clear interrupt
                outw(channel->base + AC97_SR, AC97_SR_BCIS);
                
                // Update position
                uint8_t civ = inb(channel->base + AC97_CIV);
                uint8_t lvi = inb(channel->base + AC97_LVI);
                
                if (civ == lvi) {
                    // Reached end of buffer
                    if (channel->loop) {
                        // Reset to beginning
                        outb(channel->base + AC97_LVI, channel->bdl_entries - 1);
                    } else {
                        // Stop playback
                        ac97_stop(ac97);
                    }
                }
            }
            
            if (sr & AC97_SR_LVBCI) {
                // Last valid buffer completion
                outw(channel->base + AC97_SR, AC97_SR_LVBCI);
            }
            
            if (sr & AC97_SR_FIFOE) {
                // FIFO error
                outw(channel->base + AC97_SR, AC97_SR_FIFOE);
                channel->errors++;
            }
        }
        
        // Clear global interrupt status
        outl(ac97->nabmbar + AC97_GLOB_STA, status);
    }
    
    pic_send_eoi(5);  // IRQ 5 is common for AC'97
}

// =============================================================================
// Codec Initialization
// =============================================================================

static int ac97_init_codec(ac97_controller_t* ac97) {
    // Perform cold reset
    outl(ac97->nabmbar + AC97_GLOB_CNT, AC97_GLOB_CNT_COLD_RESET);
    
    // Wait for codec ready
    uint64_t timeout = continuum_get_time() + 1000000;
    while (continuum_get_time() < timeout) {
        uint32_t status = inl(ac97->nabmbar + AC97_GLOB_STA);
        if (status & AC97_GLOB_STA_PCR) {
            break;  // Primary codec ready
        }
        io_wait();
    }
    
    // Read codec ID
    uint16_t vendor_id1 = ac97_codec_read(ac97, AC97_VENDOR_ID1);
    uint16_t vendor_id2 = ac97_codec_read(ac97, AC97_VENDOR_ID2);
    ac97->vendor_id = ((uint32_t)vendor_id1 << 16) | vendor_id2;
    
    // Read capabilities
    uint16_t reset = ac97_codec_read(ac97, AC97_RESET);
    ac97->capabilities = reset;
    
    // Initialize volumes
    ac97_set_master_volume(ac97, 75, 75);
    ac97_set_pcm_volume(ac97, 75, 75);
    
    // Unmute outputs
    ac97_codec_write(ac97, AC97_MASTER_VOLUME, 0x0000);  // Max volume, unmuted
    ac97_codec_write(ac97, AC97_PCM_OUT_VOLUME, 0x0808); // Good default
    
    // Enable variable rate if supported
    if (ac97->capabilities & AC97_CAP_VARIABLE_RATE) {
        uint16_t ext_audio = ac97_codec_read(ac97, AC97_EXTENDED_AUDIO);
        ext_audio |= AC97_EXT_AUDIO_VRA;  // Variable Rate Audio
        ac97_codec_write(ac97, AC97_EXTENDED_AUDIO, ext_audio);
    }
    
    return 0;
}

// =============================================================================
// Controller Initialization
// =============================================================================

static int ac97_init_controller(ac97_controller_t* ac97) {
    // Setup PCM out channel
    ac97->pcm_out.base = ac97->nabmbar + AC97_PO_BASE;
    ac97->pcm_out.sample_rate = 48000;  // Default
    ac97->pcm_out.channels = 2;
    ac97->pcm_out.bits_per_sample = 16;
    
    // Setup PCM in channel
    ac97->pcm_in.base = ac97->nabmbar + AC97_PI_BASE;
    
    // Setup Mic in channel
    ac97->mic_in.base = ac97->nabmbar + AC97_MC_BASE;
    
    // Initialize codec
    if (ac97_init_codec(ac97) != 0) {
        return -1;
    }
    
    // Enable interrupts
    outl(ac97->nabmbar + AC97_GLOB_CNT, AC97_GLOB_CNT_COLD_RESET | AC97_GLOB_CNT_IE);
    
    // Register interrupt handler
    interrupt_register(ac97->irq, ac97_interrupt);
    
    return 0;
}

// =============================================================================
// Driver Interface
// =============================================================================

static void* ac97_probe(device_node_t* node) {
    // Check for AC'97 audio controller
    if (node->class_code != 0x04 || node->subclass_code != 0x01) {
        return NULL;
    }
    
    ac97_controller_t* ac97 = flux_allocate(NULL, sizeof(ac97_controller_t),
                                           FLUX_ALLOC_KERNEL | FLUX_ALLOC_ZERO);
    if (!ac97) {
        return NULL;
    }
    
    // Get PCI resources
    pci_device_info_t* pci_info = (pci_device_info_t*)node->bus_specific_data;
    
    // NAM BAR (Native Audio Mixer) - BAR0
    ac97->nambar = pci_info->bars[0] & ~0x03;
    
    // NABM BAR (Native Audio Bus Master) - BAR1
    ac97->nabmbar = pci_info->bars[1] & ~0x03;
    
    ac97->irq = pci_info->irq;
    spinlock_init(&ac97->lock);
    
    // Initialize controller
    if (ac97_init_controller(ac97) != 0) {
        flux_free(ac97);
        return NULL;
    }
    
    // Add to global list
    spinlock_acquire(&g_ac97_lock);
    g_ac97_controllers[g_ac97_count++] = ac97;
    spinlock_release(&g_ac97_lock);
    
    return ac97;
}

static int ac97_attach(device_handle_t* handle) {
    ac97_controller_t* ac97 = (ac97_controller_t*)handle->driver_data;
    ac97->state = AC97_STATE_READY;
    return 0;
}

static void ac97_detach(device_handle_t* handle) {
    ac97_controller_t* ac97 = (ac97_controller_t*)handle->driver_data;
    
    // Stop all channels
    ac97_stop(ac97);
    
    // Disable interrupts
    outl(ac97->nabmbar + AC97_GLOB_CNT, 0);
    
    ac97->state = AC97_STATE_DISABLED;
}

// Driver registration
static resonance_driver_t ac97_driver = {
    .name = "ac97",
    .class_code = 0x04,     // Multimedia
    .subclass_code = 0x01,   // Audio
    .probe = ac97_probe,
    .attach = ac97_attach,
    .detach = ac97_detach
};

void ac97_init(void) {
    resonance_register_driver(&ac97_driver);
}
