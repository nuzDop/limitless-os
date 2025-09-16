/**
 * @file genesis-uefi.c
 * @brief Genesis UEFI Bootloader for LimitlessOS.
 *
 * This is the main entry point for booting LimitlessOS on UEFI-based systems.
 * It initializes the UEFI library, prints a boot message, and will eventually
 * hand off control to the Genesis boot core.
 *
 * @version 1.1.0
 * @date 2025-09-15
 * @author nuzDop
 * @copyright MIT License
 */

#include <efi.h>
#include <efilib.h>

// All UEFI types and protocols are now included from the headers above.
// The local, conflicting definitions have been removed.

/**
 * @brief The main entry point for the UEFI application.
 *
 * @param ImageHandle The handle for the loaded image.
 * @param SystemTable A pointer to the UEFI System Table.
 *
 * @return EFI_STATUS status of the bootloader execution.
 */
EFI_STATUS
EFIAPI
efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    // Initialize the GNU-EFI library
    InitializeLib(ImageHandle, SystemTable);

    // Clear the screen and print a banner
    uefi_call_wrapper(ST->ConOut->ClearScreen, 1, ST->ConOut);
    Print(L"Genesis UEFI Bootloader v1.1.0 (LimitlessOS)\n");
    Print(L"Loading Continuum Kernel...\n");

    // TODO:
    // 1. Load genesis_boot.bin from the disk.
    // 2. Prepare the genesis_boot_context_t structure.
    // 3. Jump to the 64-bit boot core entry point.

    // Halt for now
    for (;;) {
        __asm__ __volatile__("hlt");
    }

    return EFI_SUCCESS;
}
