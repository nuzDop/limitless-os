/*
 * AHCI/SATA Driver Header
 * Advanced Host Controller Interface definitions
 */

#ifndef AHCI_H
#define AHCI_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../resonance.h"

// =============================================================================
// AHCI Constants
// =============================================================================

#define MAX_AHCI_CONTROLLERS    8
#define MAX_AHCI_PORTS         32

// FIS Types
#define FIS_TYPE_REG_H2D       0x27
#define FIS_TYPE_REG_D2H       0x34
#define FIS_TYPE_DMA_ACT       0x39
#define FIS_TYPE_DMA_SETUP     0x41
#define FIS_TYPE_DATA          0x46
#define FIS_TYPE_BIST          0x58
#define FIS_TYPE_PIO_SETUP     0x5F
#define FIS_TYPE_DEV_BITS      0xA1

// ATA Commands
#define ATA_CMD_READ_DMA       0xC8
#define ATA_CMD_READ_DMA_EX    0x25
#define ATA_CMD_WRITE_DMA      0xCA
#define ATA_CMD_WRITE_DMA_EX   0x35
#define ATA_CMD_IDENTIFY       0xEC
#define ATA_CMD_PACKET         0xA0
#define ATA_CMD_FLUSH          0xE7
#define ATA_CMD_FLUSH_EX       0xEA

// AHCI GHC bits
#define AHCI_GHC_HR            (1 << 0)   // HBA Reset
#define AHCI_GHC_IE            (1 << 1)   // Interrupt Enable
#define AHCI_GHC_AE            (1 << 31)  // AHCI Enable

// Port Command bits
#define AHCI_PORT_CMD_ST       (1 << 0)   // Start
#define AHCI_PORT_CMD_FRE      (1 << 4)   // FIS Receive Enable
#define AHCI_PORT_CMD_FR       (1 << 14)  // FIS Receive Running
#define AHCI_PORT_CMD_CR       (1 << 15)  // Command List Running

// Port Interrupt bits
#define AHCI_PORT_IS_TFES      (1 << 30)  // Task File Error Status
#define AHCI_PORT_IS_HBFS      (1 << 29)  // Host Bus Fatal Error
#define AHCI_PORT_IS_HBDS      (1 << 28)  // Host Bus Data Error
#define AHCI_PORT_IS_IFS       (1 << 27)  // Interface Fatal Error

// Default port interrupts to enable
#define AHCI_PORT_IE_DEFAULT   0x7DC0007F

// Port detection
#define HBA_PORT_DET_PRESENT   3
#define HBA_PORT_IPM_ACTIVE    1

// SATA device signatures
#define SATA_SIG_ATA           0x00000101
#define SATA_SIG_ATAPI         0xEB140101
#define SATA_SIG_SEMB          0xC33C0101
#define SATA_SIG_PM            0x96690101

// =============================================================================
// FIS Structures
// =============================================================================

// FIS REG H2D - Register FIS - Host to Device
typedef struct __attribute__((packed)) {
    uint8_t fis_type;
    uint8_t pmport:4;
    uint8_t rsv0:3;
    uint8_t c:1;
    uint8_t command;
    uint8_t featurel;
    
    uint8_t lba0;
    uint8_t lba1;
    uint8_t lba2;
    uint8_t device;
    
    uint8_t lba3;
    uint8_t lba4;
    uint8_t lba5;
    uint8_t featureh;
    
    uint8_t countl;
    uint8_t counth;
    uint8_t icc;
    uint8_t control;
    
    uint32_t auxiliary;
} fis_reg_h2d_t;

// FIS REG D2H - Register FIS - Device to Host
typedef struct __attribute__((packed)) {
    uint8_t fis_type;
    uint8_t pmport:4;
    uint8_t rsv0:2;
    uint8_t i:1;
    uint8_t rsv1:1;
    uint8_t status;
    uint8_t error;
    
    uint8_t lba0;
    uint8_t lba1;
    uint8_t lba2;
    uint8_t device;
    
    uint8_t lba3;
    uint8_t lba4;
    uint8_t lba5;
    uint8_t rsv2;
    
    uint8_t countl;
    uint8_t counth;
    uint16_t rsv3;
    
    uint32_t rsv4;
} fis_reg_d2h_t;

// =============================================================================
// AHCI Memory Structures
// =============================================================================

// HBA Port registers
typedef struct __attribute__((packed)) {
    uint32_t clb;          // Command list base address
    uint32_t clbu;         // Command list base address upper
    uint32_t fb;           // FIS base address
    uint32_t fbu;          // FIS base address upper
    uint32_t is;           // Interrupt status
    uint32_t ie;           // Interrupt enable
    uint32_t cmd;          // Command and status
    uint32_t rsv0;
    uint32_t tfd;          // Task file data
    uint32_t sig;          // Signature
    uint32_t ssts;         // SATA status
    uint32_t sctl;         // SATA control
    uint32_t serr;         // SATA error
    uint32_t sact;         // SATA active
    uint32_t ci;           // Command issue
    uint32_t sntf;         // SATA notification
    uint32_t fbs;          // FIS-based switching control
    uint32_t rsv1[11];
    uint32_t vendor[4];
} ahci_hba_port_t;

// HBA Memory registers
typedef struct __attribute__((packed)) {
    uint32_t cap;          // Host capability
    uint32_t ghc;          // Global host control
    uint32_t is;           // Interrupt status
    uint32_t pi;           // Port implemented
    uint32_t vs;           // Version
    uint32_t ccc_ctl;      // Command completion coalescing control
    uint32_t ccc_pts;      // Command completion coalescing ports
    uint32_t em_loc;       // Enclosure management location
    uint32_t em_ctl;       // Enclosure management control
    uint32_t cap2;         // Host capabilities extended
    uint32_t bohc;         // BIOS/OS handoff control
    uint8_t rsv[0xA0-0x2C];
    uint8_t vendor[0x100-0xA0];
    ahci_hba_port_t ports[32];
} ahci_hba_mem_t;

// Command header
typedef struct __attribute__((packed)) {
    uint8_t cfl:5;         // Command FIS length
    uint8_t a:1;           // ATAPI
    uint8_t w:1;           // Write
    uint8_t p:1;           // Prefetchable
    
    uint8_t r:1;           // Reset
    uint8_t b:1;           // BIST
    uint8_t c:1;           // Clear busy upon R_OK
    uint8_t rsv0:1;
    uint8_t pmp:4;         // Port multiplier port
    
    uint16_t prdtl;        // Physical region descriptor table length
    volatile uint32_t prdbc;  // Physical region descriptor byte count
    uint32_t ctba;         // Command table descriptor base address
    uint32_t ctbau;        // Command table descriptor base address upper
    uint32_t rsv1[4];
} ahci_hba_cmd_header_t;

// Physical region descriptor table entry
typedef struct __attribute__((packed)) {
    uint32_t dba;          // Data base address
    uint32_t dbau;         // Data base address upper
    uint32_t rsv0;
    uint32_t dbc:22;       // Byte count
    uint32_t rsv1:9;
    uint32_t i:1;          // Interrupt on completion
} ahci_hba_prdt_entry_t;

// Command table
typedef struct __attribute__((packed)) {
    uint8_t cfis[64];      // Command FIS
    uint8_t acmd[16];      // ATAPI command
    uint8_t rsv[48];
    ahci_hba_prdt_entry_t prdt_entry[1];  // Variable length
} ahci_hba_cmd_tbl_t;

// Received FIS
typedef struct __attribute__((packed)) {
    fis_reg_d2h_t rfis;
    uint8_t pad0[4];
    uint8_t psfis[20];
    uint8_t pad1[8];
    uint8_t sdbfis[8];
    uint8_t ufis[64];
    uint8_t rsv[96];
} ahci_hba_fis_t;

// =============================================================================
// Driver Structures
// =============================================================================

// Device types
typedef enum {
    AHCI_DEV_NULL = 0,
    AHCI_DEV_SATA,
    AHCI_DEV_SATAPI,
    AHCI_DEV_SEMB,
    AHCI_DEV_PM
} ahci_device_type_t;

// Port structure
typedef struct {
    struct ahci_controller* controller;
    uint32_t port_num;
    ahci_hba_port_t* regs;
    ahci_device_type_t device_type;
    
    // Command structures
    ahci_hba_cmd_header_t* clb;
    ahci_hba_fis_t* fb;
    ahci_hba_cmd_tbl_t* ctba[32];
    
    // DMA regions
    dma_region_t* clb_dma;
    dma_region_t* fb_dma;
    dma_region_t* ctba_dma[32];
    
    // Device info
    char serial[21];
    char model[41];
    uint64_t sectors;
    
    // Statistics
    uint64_t commands_issued;
    uint64_t bytes_read;
    uint64_t bytes_written;
    
    spinlock_t lock;
} ahci_port_t;

// Controller state
typedef enum {
    AHCI_STATE_DISABLED = 0,
    AHCI_STATE_INITIALIZING,
    AHCI_STATE_READY,
    AHCI_STATE_ERROR
} ahci_state_t;

// Controller structure
typedef struct ahci_controller {
    ahci_hba_mem_t* abar;
    ahci_state_t state;
    
    // Capabilities
    uint32_t cap;
    uint32_t num_ports;
    uint32_t num_cmd_slots;
    bool supports_64bit;
    
    // Ports
    ahci_port_t* ports[32];
    uint32_t active_ports;
    
    // Statistics
    uint64_t total_commands;
    uint64_t total_errors;
} ahci_controller_t;

// Command structure
typedef struct {
    uint8_t ata_cmd;
    uint64_t lba;
    uint16_t count;
    uint64_t buf_phys;
    bool write;
} ahci_command_t;

// =============================================================================
// Function Prototypes
// =============================================================================

// Initialization
void ahci_init(void);

// Port operations
int ahci_read(ahci_port_t* port, uint64_t lba, uint32_t count, void* buffer);
int ahci_write(ahci_port_t* port, uint64_t lba, uint32_t count, void* buffer);
int ahci_flush(ahci_port_t* port);

// Controller management
ahci_controller_t* ahci_get_controller(uint32_t index);
uint32_t ahci_get_controller_count(void);
ahci_port_t* ahci_get_port(ahci_controller_t* ctrl, uint32_t port_num);

// Device info
uint64_t ahci_get_device_sectors(ahci_port_t* port);
const char* ahci_get_device_model(ahci_port_t* port);
const char* ahci_get_device_serial(ahci_port_t* port);

#endif /* AHCI_H */
