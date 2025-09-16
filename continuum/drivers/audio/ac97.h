/*
 * AC'97 Audio Codec Driver Header
 * Intel Audio Codec '97 specification definitions
 */

#ifndef AC97_H
#define AC97_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../resonance.h"

// =============================================================================
// AC'97 Constants
// =============================================================================

#define MAX_AC97_CONTROLLERS    4
#define AC97_BDL_ENTRIES       32
#define AC97_BDL_BUFFER_SIZE   65536  // 64KB per buffer

// AC'97 Codec Registers (NAM - Native Audio Mixer)
#define AC97_RESET              0x00
#define AC97_MASTER_VOLUME      0x02
#define AC97_AUX_OUT_VOLUME     0x04
#define AC97_MONO_VOLUME        0x06
#define AC97_MASTER_TONE        0x08
#define AC97_PC_BEEP_VOLUME     0x0A
#define AC97_PHONE_VOLUME       0x0C
#define AC97_MIC_VOLUME         0x0E
#define AC97_LINE_IN_VOLUME     0x10
#define AC97_CD_VOLUME          0x12
#define AC97_VIDEO_VOLUME       0x14
#define AC97_AUX_IN_VOLUME      0x16
#define AC97_PCM_OUT_VOLUME     0x18
#define AC97_RECORD_SELECT      0x1A
#define AC97_RECORD_GAIN        0x1C
#define AC97_RECORD_GAIN_MIC    0x1E
#define AC97_GENERAL_PURPOSE    0x20
#define AC97_3D_CONTROL         0x22
#define AC97_POWERDOWN          0x26
#define AC97_EXTENDED_AUDIO     0x28
#define AC97_EXTENDED_STATUS    0x2A
#define AC97_PCM_FRONT_RATE     0x2C
#define AC97_PCM_SURR_RATE      0x2E
#define AC97_PCM_LFE_RATE       0x30
#define AC97_PCM_LR_RATE        0x32
#define AC97_MIC_RATE           0x34
#define AC97_VENDOR_ID1         0x7C
#define AC97_VENDOR_ID2         0x7E

// Volume control
#define AC97_MUTE               0x8000
#define AC97_VOLUME_MASK        0x003F

// Reset register capabilities
#define AC97_CAP_VARIABLE_RATE  0x0001
#define AC97_CAP_DOUBLE_RATE    0x0002
#define AC97_CAP_SURROUND       0x0004
#define AC97_CAP_HEADPHONE      0x0010
#define AC97_CAP_LOUDNESS       0x0020
#define AC97_CAP_18BIT_DAC      0x0040
#define AC97_CAP_20BIT_DAC      0x0080
#define AC97_CAP_18BIT_ADC      0x0100
#define AC97_CAP_20BIT_ADC      0x0200

// Extended Audio register
#define AC97_EXT_AUDIO_VRA      0x0001  // Variable Rate Audio
#define AC97_EXT_AUDIO_DRA      0x0002  // Double Rate Audio
#define AC97_EXT_AUDIO_SPDIF    0x0004  // S/PDIF
#define AC97_EXT_AUDIO_VRM      0x0008  // Variable Rate Mic
#define AC97_EXT_AUDIO_CDAC     0x0040  // Center DAC
#define AC97_EXT_AUDIO_SDAC     0x0080  // Surround DAC
#define AC97_EXT_AUDIO_LDAC     0x0100  // LFE DAC

// Bus Master Registers (NABM - Native Audio Bus Master)
#define AC97_PI_BASE            0x00    // PCM In
#define AC97_PO_BASE            0x10    // PCM Out
#define AC97_MC_BASE            0x20    // Mic In

// Channel registers (relative to base)
#define AC97_BDBAR              0x00    // Buffer Descriptor Base Address
#define AC97_CIV                0x04    // Current Index Value
#define AC97_LVI                0x05    // Last Valid Index
#define AC97_SR                 0x06    // Status Register
#define AC97_PICB               0x08    // Position In Current Buffer
#define AC97_PIV                0x0A    // Prefetch Index Value
#define AC97_CR                 0x0B    // Control Register

// Global registers
#define AC97_GLOB_CNT           0x2C    // Global Control
#define AC97_GLOB_STA           0x30    // Global Status
#define AC97_CAS                0x34    // Codec Access Semaphore

// Status Register bits
#define AC97_SR_DCH             0x01    // DMA Controller Halted
#define AC97_SR_CELV            0x02    // Current Equals Last Valid
#define AC97_SR_LVBCI           0x04    // Last Valid Buffer Completion Interrupt
#define AC97_SR_BCIS            0x08    // Buffer Completion Interrupt Status
#define AC97_SR_FIFOE           0x10    // FIFO Error

// Control Register bits
#define AC97_CR_RPBM            0x01    // Run/Pause Bus Master
#define AC97_CR_RR              0x02    // Reset Registers
#define AC97_CR_LVBIE           0x04    // Last Valid Buffer Interrupt Enable
#define AC97_CR_IOCE            0x08    // Interrupt On Completion Enable
#define AC97_CR_FEIE            0x10    // FIFO Error Interrupt Enable

// Global Control bits
#define AC97_GLOB_CNT_GIE       0x01    // GPI Interrupt Enable
#define AC97_GLOB_CNT_COLD_RESET 0x02   // Cold Reset
#define AC97_GLOB_CNT_WARM_RESET 0x04   // Warm Reset
#define AC97_GLOB_CNT_SHUT      0x08    // Shut Down
#define AC97_GLOB_CNT_IE        0x20    // Interrupt Enable

// Global Status bits
#define AC97_GLOB_STA_GSCI      0x00000001  // GPI Status Change Interrupt
#define AC97_GLOB_STA_MIINT     0x00000002  // Modem In Interrupt
#define AC97_GLOB_STA_MOINT     0x00000004  // Modem Out Interrupt
#define AC97_GLOB_STA_PIINT     0x00000020  // PCM In Interrupt
#define AC97_GLOB_STA_POINT     0x00000040  // PCM Out Interrupt
#define AC97_GLOB_STA_MINT      0x00000080  // Mic In Interrupt
#define AC97_GLOB_STA_PCR       0x00000100  // Primary Codec Ready
#define AC97_GLOB_STA_SCR       0x00000200  // Secondary Codec Ready

// Codec Access Semaphore
#define AC97_CAS_ACTIVE         0x01    // Codec Access In Progress

// Buffer Descriptor List flags
#define AC97_BDL_FLAG_IOC       0x8000  // Interrupt On Completion
#define AC97_BDL_FLAG_BUP       0x4000  // Buffer Underrun Policy

// =============================================================================
// AC'97 Data Structures
// =============================================================================

// Buffer Descriptor List Entry
typedef struct __attribute__((packed)) {
    uint32_t address;      // Physical address of buffer
    uint16_t samples;      // Number of samples
    uint16_t flags;        // Control flags
} ac97_bdl_entry_t;

// Audio Channel
typedef struct {
    uint32_t base;         // Base I/O address
    
    // Buffer descriptor list
    ac97_bdl_entry_t* bdl;
    dma_region_t* bdl_dma;
    uint32_t bdl_entries;
    
    // Audio buffer
    void* buffer;
    dma_region_t* buffer_dma;
    size_t buffer_size;
    
    // Format
    uint32_t sample_rate;
    uint8_t channels;
    uint8_t bits_per_sample;
    
    // State
    bool playing;
    bool recording;
    bool loop;
    
    // Statistics
    uint64_t samples_played;
    uint64_t interrupts;
    uint64_t errors;
} ac97_channel_t;

// Controller State
typedef enum {
    AC97_STATE_DISABLED = 0,
    AC97_STATE_INITIALIZING,
    AC97_STATE_READY,
    AC97_STATE_ERROR
} ac97_state_t;

// AC'97 Controller
typedef struct {
    uint16_t nambar;       // Native Audio Mixer BAR
    uint16_t nabmbar;      // Native Audio Bus Master BAR
    uint8_t irq;
    
    ac97_state_t state;
    
    // Codec info
    uint32_t vendor_id;
    uint16_t capabilities;
    
    // Audio channels
    ac97_channel_t pcm_out;
    ac97_channel_t pcm_in;
    ac97_channel_t mic_in;
    
    // Volume settings
    uint8_t master_volume;
    uint8_t pcm_volume;
    uint8_t mic_volume;
    
    spinlock_t lock;
} ac97_controller_t;

// Interrupt frame
typedef struct {
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} interrupt_frame_t;

// =============================================================================
// Function Prototypes
// =============================================================================

void ac97_init(void);

// Playback control
int ac97_play(ac97_controller_t* ac97, void* buffer, size_t size,
             uint32_t sample_rate, uint8_t channels, uint8_t bits);
void ac97_stop(ac97_controller_t* ac97);
void ac97_pause(ac97_controller_t* ac97);
void ac97_resume(ac97_controller_t* ac97);

// Recording control
int ac97_record(ac97_controller_t* ac97, void* buffer, size_t size,
               uint32_t sample_rate, uint8_t channels, uint8_t bits);
void ac97_stop_recording(ac97_controller_t* ac97);

// Volume control
void ac97_set_master_volume(ac97_controller_t* ac97, uint8_t left, uint8_t right);
void ac97_set_pcm_volume(ac97_controller_t* ac97, uint8_t left, uint8_t right);
void ac97_set_mic_volume(ac97_controller_t* ac97, uint8_t volume);
uint8_t ac97_get_master_volume(ac97_controller_t* ac97);
uint8_t ac97_get_pcm_volume(ac97_controller_t* ac97);

// Device enumeration
uint32_t ac97_get_controller_count(void);
ac97_controller_t* ac97_get_controller(uint32_t index);

// Helper functions
void pic_send_eoi(uint8_t irq);
void interrupt_register(uint8_t vector, void (*handler)(interrupt_frame_t*));

#endif /* AC97_H */
