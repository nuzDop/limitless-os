/*
 * AHCI/SATA Driver for Continuum Kernel
 * Advanced Host Controller Interface for SATA drives
 */

#include "ahci.h"
#include "../resonance.h"
#include "../../flux_memory.h"

// =============================================================================
// Global AHCI State
// =============================================================================

static ahci_controller_t* g_ahci_controllers[MAX_AHCI_CONTROLLERS];
static uint32_t g_ahci_count = 0;
static spinlock_t g_ahci_lock = SPINLOCK_INIT;

// =============================================================================
// AHCI Port Operations
// =============================================================================

static void ahci_start_port(ahci_port_t* port) {
    // Wait for port to be idle
    while (port->regs->cmd & (AHCI_PORT_CMD_CR | AHCI_PORT_CMD_FR)) {
        io_wait();
    }
    
    // Enable FIS receive
    port->regs->cmd |= AHCI_PORT_CMD_FRE;
    
    // Enable command processing
    port->regs->cmd |= AHCI_PORT_CMD_ST;
}

static void ahci_stop_port(ahci_port_t* port) {
    // Clear ST bit
    port->regs->cmd &= ~AHCI_PORT_CMD_ST;
    
    // Wait for CR to clear
    while (port->regs->cmd & AHCI_PORT_CMD_CR) {
        io_wait();
    }
    
    // Clear FRE bit
    port->regs->cmd &= ~AHCI_PORT_CMD_FRE;
    
    // Wait for FR to clear
    while (port->regs->cmd & AHCI_PORT_CMD_FR) {
        io_wait();
    }
}

static int ahci_port_rebase(ahci_port_t* port, uint32_t port_num) {
    ahci_stop_port(port);
    
    // Allocate command list (1KB, 32 commands * 32 bytes)
    port->clb_dma = resonance_alloc_dma(1024, DMA_FLAG_COHERENT);
    if (!port->clb_dma) {
        return -1;
    }
    port->clb = (ahci_hba_cmd_header_t*)port->clb_dma->virtual_addr;
    memset(port->clb, 0, 1024);
    
    // Allocate FIS receive area (256 bytes)
    port->fb_dma = resonance_alloc_dma(256, DMA_FLAG_COHERENT);
    if (!port->fb_dma) {
        resonance_free_dma(port->clb_dma);
        return -1;
    }
    port->fb = (ahci_hba_fis_t*)port->fb_dma->virtual_addr;
    memset(port->fb, 0, 256);
    
    // Allocate command tables (8KB per command)
    for (int i = 0; i < 32; i++) {
        port->ctba_dma[i] = resonance_alloc_dma(8192, DMA_FLAG_COHERENT);
        if (!port->ctba_dma[i]) {
            // Clean up allocated tables
            for (int j = 0; j < i; j++) {
                resonance_free_dma(port->ctba_dma[j]);
            }
            resonance_free_dma(port->clb_dma);
            resonance_free_dma(port->fb_dma);
            return -1;
        }
        port->ctba[i] = (ahci_hba_cmd_tbl_t*)port->ctba_dma[i]->virtual_addr;
        memset(port->ctba[i], 0, 8192);
        
        // Setup command header
        port->clb[i].prdtl = 8;  // 8 PRD entries
        port->clb[i].ctba = port->ctba_dma[i]->physical_addr & 0xFFFFFFFF;
        port->clb[i].ctbau = (port->ctba_dma[i]->physical_addr >> 32) & 0xFFFFFFFF;
    }
    
    // Set base addresses
    port->regs->clb = port->clb_dma->physical_addr & 0xFFFFFFFF;
    port->regs->clbu = (port->clb_dma->physical_addr >> 32) & 0xFFFFFFFF;
    port->regs->fb = port->fb_dma->physical_addr & 0xFFFFFFFF;
    port->regs->fbu = (port->fb_dma->physical_addr >> 32) & 0xFFFFFFFF;
    
    // Clear interrupts and errors
    port->regs->serr = 0xFFFFFFFF;
    port->regs->is = 0xFFFFFFFF;
    
    // Enable interrupts
    port->regs->ie = AHCI_PORT_IE_DEFAULT;
    
    ahci_start_port(port);
    
    return 0;
}

// =============================================================================
// AHCI Command Execution
// =============================================================================

static int ahci_find_cmdslot(ahci_port_t* port) {
    uint32_t slots = port->regs->sact | port->regs->ci;
    for (int i = 0; i < 32; i++) {
        if ((slots & (1 << i)) == 0) {
            return i;
        }
    }
    return -1;
}

static int ahci_send_command(ahci_port_t* port, ahci_command_t* cmd) {
    spinlock_acquire(&port->lock);
    
    // Find free command slot
    int slot = ahci_find_cmdslot(port);
    if (slot == -1) {
        spinlock_release(&port->lock);
        return -1;
    }
    
    ahci_hba_cmd_header_t* cmdheader = &port->clb[slot];
    cmdheader->cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t);  // Command FIS size
    cmdheader->w = cmd->write ? 1 : 0;  // Read/Write
    cmdheader->prdtl = (uint16_t)((cmd->count - 1) >> 4) + 1;  // PRDT entries
    
    ahci_hba_cmd_tbl_t* cmdtbl = port->ctba[slot];
    memset(cmdtbl, 0, sizeof(ahci_hba_cmd_tbl_t) + 
           (cmdheader->prdtl - 1) * sizeof(ahci_hba_prdt_entry_t));
    
    // Setup PRDT entries
    int i;
    for (i = 0; i < cmdheader->prdtl - 1; i++) {
        cmdtbl->prdt_entry[i].dba = cmd->buf_phys + (i * 8192);
        cmdtbl->prdt_entry[i].dbau = 0;
        cmdtbl->prdt_entry[i].dbc = 8191;  // 8KB - 1
        cmdtbl->prdt_entry[i].i = 0;
    }
    
    // Last entry
    cmdtbl->prdt_entry[i].dba = cmd->buf_phys + (i * 8192);
    cmdtbl->prdt_entry[i].dbau = 0;
    cmdtbl->prdt_entry[i].dbc = ((cmd->count << 9) - 1) % 8192;  // Remaining bytes
    cmdtbl->prdt_entry[i].i = 0;
    
    // Setup command FIS
    fis_reg_h2d_t* cmdfis = (fis_reg_h2d_t*)&cmdtbl->cfis;
    memset(cmdfis, 0, sizeof(fis_reg_h2d_t));
    
    cmdfis->fis_type = FIS_TYPE_REG_H2D;
    cmdfis->c = 1;  // Command
    cmdfis->command = cmd->ata_cmd;
    
    cmdfis->lba0 = (uint8_t)cmd->lba;
    cmdfis->lba1 = (uint8_t)(cmd->lba >> 8);
    cmdfis->lba2 = (uint8_t)(cmd->lba >> 16);
    cmdfis->device = 1 << 6;  // LBA mode
    
    cmdfis->lba3 = (uint8_t)(cmd->lba >> 24);
    cmdfis->lba4 = (uint8_t)(cmd->lba >> 32);
    cmdfis->lba5 = (uint8_t)(cmd->lba >> 40);
    
    cmdfis->countl = cmd->count & 0xFF;
    cmdfis->counth = (cmd->count >> 8) & 0xFF;
    
    // Issue command
    port->regs->ci = 1 << slot;
    
    // Wait for completion
    uint64_t timeout = continuum_get_time() + 5000000;  // 5 seconds
    while ((port->regs->ci & (1 << slot)) && continuum_get_time() < timeout) {
        if (port->regs->is & AHCI_PORT_IS_TFES) {
            // Task file error
            spinlock_release(&port->lock);
            return -1;
        }
    }
    
    if (port->regs->ci & (1 << slot)) {
        // Timeout
        spinlock_release(&port->lock);
        return -1;
    }
    
    // Check for errors
    if (port->regs->is & AHCI_PORT_IS_TFES) {
        spinlock_release(&port->lock);
        return -1;
    }
    
    spinlock_release(&port->lock);
    return 0;
}

// =============================================================================
// AHCI Read/Write Operations
// =============================================================================

int ahci_read(ahci_port_t* port, uint64_t lba, uint32_t count, void* buffer) {
    if (!port || !buffer || count == 0) {
        return -1;
    }
    
    // Allocate DMA buffer
    size_t size = count * 512;  // Assuming 512 byte sectors
    dma_region_t* dma = resonance_alloc_dma(size, DMA_FLAG_COHERENT);
    if (!dma) {
        return -1;
    }
    
    ahci_command_t cmd = {
        .ata_cmd = ATA_CMD_READ_DMA_EX,
        .lba = lba,
        .count = count,
        .buf_phys = dma->physical_addr,
        .write = false
    };
    
    int result = ahci_send_command(port, &cmd);
    
    if (result == 0) {
        memcpy(buffer, dma->virtual_addr, size);
    }
    
    resonance_free_dma(dma);
    return result;
}

int ahci_write(ahci_port_t* port, uint64_t lba, uint32_t count, void* buffer) {
    if (!port || !buffer || count == 0) {
        return -1;
    }
    
    // Allocate DMA buffer
    size_t size = count * 512;
    dma_region_t* dma = resonance_alloc_dma(size, DMA_FLAG_COHERENT);
    if (!dma) {
        return -1;
    }
    
    memcpy(dma->virtual_addr, buffer, size);
    
    ahci_command_t cmd = {
        .ata_cmd = ATA_CMD_WRITE_DMA_EX,
        .lba = lba,
        .count = count,
        .buf_phys = dma->physical_addr,
        .write = true
    };
    
    int result = ahci_send_command(port, &cmd);
    
    resonance_free_dma(dma);
    return result;
}

// =============================================================================
// AHCI Device Identification
// =============================================================================

static int ahci_identify_device(ahci_port_t* port) {
    dma_region_t* dma = resonance_alloc_dma(512, DMA_FLAG_COHERENT);
    if (!dma) {
        return -1;
    }
    
    ahci_command_t cmd = {
        .ata_cmd = ATA_CMD_IDENTIFY,
        .lba = 0,
        .count = 1,
        .buf_phys = dma->physical_addr,
        .write = false
    };
    
    if (ahci_send_command(port, &cmd) != 0) {
        resonance_free_dma(dma);
        return -1;
    }
    
    uint16_t* identify = (uint16_t*)dma->virtual_addr;
    
    // Extract device info
    for (int i = 0; i < 20; i++) {
        port->serial[i] = ((char*)&identify[10])[i];
    }
    port->serial[20] = '\0';
    
    for (int i = 0; i < 40; i++) {
        port->model[i] = ((char*)&identify[27])[i];
    }
    port->model[40] = '\0';
    
    // Get capacity
    if (identify[83] & (1 << 10)) {
        // 48-bit LBA
        port->sectors = ((uint64_t)identify[103] << 48) |
                       ((uint64_t)identify[102] << 32) |
                       ((uint64_t)identify[101] << 16) |
                       ((uint64_t)identify[100]);
    } else {
        // 28-bit LBA
        port->sectors = ((uint32_t)identify[61] << 16) | identify[60];
    }
    
    resonance_free_dma(dma);
    return 0;
}

// =============================================================================
// AHCI Controller Initialization
// =============================================================================

static int ahci_init_controller(ahci_controller_t* ctrl) {
    // Enable AHCI mode
    ctrl->abar->ghc |= AHCI_GHC_AE;
    
    // Perform HBA reset
    ctrl->abar->ghc |= AHCI_GHC_HR;
    while (ctrl->abar->ghc & AHCI_GHC_HR) {
        io_wait();
    }
    
    // Re-enable AHCI mode
    ctrl->abar->ghc |= AHCI_GHC_AE;
    
    // Get capabilities
    ctrl->cap = ctrl->abar->cap;
    ctrl->num_ports = ((ctrl->cap >> 0) & 0x1F) + 1;
    ctrl->num_cmd_slots = ((ctrl->cap >> 8) & 0x1F) + 1;
    ctrl->supports_64bit = (ctrl->cap >> 31) & 0x01;
    
    // Enable interrupts
    ctrl->abar->ghc |= AHCI_GHC_IE;
    
    // Initialize ports
    uint32_t ports_impl = ctrl->abar->pi;
    
    for (int i = 0; i < 32; i++) {
        if (!(ports_impl & (1 << i))) {
            continue;
        }
        
        ahci_port_t* port = flux_allocate(NULL, sizeof(ahci_port_t),
                                          FLUX_ALLOC_KERNEL | FLUX_ALLOC_ZERO);
        if (!port) {
            continue;
        }
        
        port->controller = ctrl;
        port->port_num = i;
        port->regs = &ctrl->abar->ports[i];
        spinlock_init(&port->lock);
        
        // Check port status
        uint32_t ssts = port->regs->ssts;
        uint8_t det = ssts & 0x0F;
        uint8_t ipm = (ssts >> 8) & 0x0F;
        
        if (det != HBA_PORT_DET_PRESENT || ipm != HBA_PORT_IPM_ACTIVE) {
            flux_free(port);
            continue;
        }
        
        // Check device type
        uint32_t sig = port->regs->sig;
        if (sig == SATA_SIG_ATA) {
            port->device_type = AHCI_DEV_SATA;
        } else if (sig == SATA_SIG_ATAPI) {
            port->device_type = AHCI_DEV_SATAPI;
        } else if (sig == SATA_SIG_SEMB) {
            port->device_type = AHCI_DEV_SEMB;
        } else if (sig == SATA_SIG_PM) {
            port->device_type = AHCI_DEV_PM;
        } else {
            port->device_type = AHCI_DEV_NULL;
            flux_free(port);
            continue;
        }
        
        // Initialize port
        if (ahci_port_rebase(port, i) != 0) {
            flux_free(port);
            continue;
        }
        
        // Identify device
        if (port->device_type == AHCI_DEV_SATA) {
            ahci_identify_device(port);
        }
        
        ctrl->ports[i] = port;
        ctrl->active_ports++;
    }
    
    return 0;
}

// =============================================================================
// Driver Interface
// =============================================================================

static void* ahci_probe(device_node_t* node) {
    if (node->class_code != 0x01 || node->subclass_code != 0x06) {
        return NULL;  // Not AHCI
    }
    
    ahci_controller_t* ctrl = flux_allocate(NULL, sizeof(ahci_controller_t),
                                           FLUX_ALLOC_KERNEL | FLUX_ALLOC_ZERO);
    if (!ctrl) {
        return NULL;
    }
    
    // Get PCI resources
    pci_device_info_t* pci_info = (pci_device_info_t*)node->bus_specific_data;
    
    // Map ABAR (BAR5)
    ctrl->abar = (ahci_hba_mem_t*)(uintptr_t)(pci_info->bars[5] & ~0x1F);
    
    // Initialize controller
    if (ahci_init_controller(ctrl) != 0) {
        flux_free(ctrl);
        return NULL;
    }
    
    // Add to global list
    spinlock_acquire(&g_ahci_lock);
    g_ahci_controllers[g_ahci_count++] = ctrl;
    spinlock_release(&g_ahci_lock);
    
    return ctrl;
}

static int ahci_attach(device_handle_t* handle) {
    ahci_controller_t* ctrl = (ahci_controller_t*)handle->driver_data;
    ctrl->state = AHCI_STATE_READY;
    return 0;
}

static void ahci_detach(device_handle_t* handle) {
    ahci_controller_t* ctrl = (ahci_controller_t*)handle->driver_data;
    
    // Stop all ports
    for (int i = 0; i < 32; i++) {
        if (ctrl->ports[i]) {
            ahci_stop_port(ctrl->ports[i]);
        }
    }
    
    // Disable interrupts
    ctrl->abar->ghc &= ~AHCI_GHC_IE;
    
    ctrl->state = AHCI_STATE_DISABLED;
}

// Driver registration
static resonance_driver_t ahci_driver = {
    .name = "ahci",
    .vendor_ids = {0},  // Match all vendors
    .device_ids = {0},  // Match all devices
    .class_code = 0x01,     // Mass storage
    .subclass_code = 0x06,  // AHCI
    .probe = ahci_probe,
    .attach = ahci_attach,
    .detach = ahci_detach
};

void ahci_init(void) {
    resonance_register_driver(&ahci_driver);
}
