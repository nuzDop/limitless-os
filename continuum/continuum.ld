/* Continuum Kernel Linker Script */

OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(continuum_start)

KERNEL_VMA = 0xFFFFFFFF80000000;
KERNEL_PMA = 0x100000;

SECTIONS
{
    . = KERNEL_VMA + KERNEL_PMA;
    
    .text ALIGN(4K) : AT(ADDR(.text) - KERNEL_VMA)
    {
        *(.text.boot)
        *(.text*)
    }
    
    .rodata ALIGN(4K) : AT(ADDR(.rodata) - KERNEL_VMA)
    {
        *(.rodata*)
    }
    
    .data ALIGN(4K) : AT(ADDR(.data) - KERNEL_VMA)
    {
        *(.data*)
    }
    
    .bss ALIGN(4K) : AT(ADDR(.bss) - KERNEL_VMA)
    {
        *(COMMON)
        *(.bss*)
        . = ALIGN(4K);
    }
    
    /DISCARD/ :
    {
        *(.eh_frame)
        *(.comment)
    }
}
