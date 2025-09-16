/* Genesis Boot Core Linker Script */

OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(genesis_direct_entry)

SECTIONS
{
    . = 0x80000;  /* Load at 512KB */
    
    .text : ALIGN(4K)
    {
        *(.text.boot)
        *(.text)
        *(.text.*)
    }
    
    .rodata : ALIGN(4K)
    {
        *(.rodata)
        *(.rodata.*)
    }
    
    .data : ALIGN(4K)
    {
        *(.data)
        *(.data.*)
    }
    
    .bss : ALIGN(4K)
    {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    }
    
    __genesis_end = .;
    
    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame)
        *(.note.*)
    }
}
