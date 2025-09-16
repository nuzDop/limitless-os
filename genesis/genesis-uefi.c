/*
 * Genesis UEFI Bootloader for LimitlessOS
 * This is the source file that compiles to genesis-uefi.efi
 */

#include <efi.h>
#include <efilib.h>
#include "genesis_boot.h"

// For compilation without GNU-EFI, include minimal definitions
#ifndef _EFI_H
typedef void* EFI_HANDLE;
typedef struct {
    char dummy[1];
} EFI_SYSTEM_TABLE;
#define EFI_SUCCESS 0
typedef unsigned long long EFI_STATUS;
#endif

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    // Initialize UEFI
    InitializeLib(ImageHandle, SystemTable);
    
    // Clear screen
    uefi_call_wrapper(ST->ConOut->ClearScreen, 1, ST->ConOut);
    
    Print(L"Genesis UEFI Bootloader v1.0.0\n");
    Print(L"Loading LimitlessOS...\n");
    
    // This would integrate with genesis_boot.c
    // genesis_uefi_entry(uefi_context);
    
    return EFI_SUCCESS;
}
