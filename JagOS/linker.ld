/*
 * =============================================================================
 * JagOs - linker.ld
 * Linker script for the JagOs kernel binary
 *
 * Memory layout:
 *   0x0000 - 0x0FFF  BIOS/IVT/BDA (reserved)
 *   0x1000 - 0x7BFF  Kernel (loaded here by bootloader)
 *   0x7C00 - 0x7DFF  Bootloader
 *   0x7E00 - 0x8FFFF Free/stack
 *   0xB8000           VGA text buffer
 *
 * The bootloader does: call 0x1000
 * So kernel_main must be the very first symbol at 0x1000.
 * =============================================================================
 */

ENTRY(kernel_main)

SECTIONS
{
    /* Kernel loads at 0x1000 in physical memory */
    . = 0x1000;

    /* Text (code) section - kernel_main MUST be first */
    .text :
    {
        /* Force kernel_main to be at the very start */
        KEEP(*(.text.kernel_main))
        *(.text)
        *(.text.*)
    }

    /* Read-only data */
    .rodata :
    {
        *(.rodata)
        *(.rodata.*)
    }

    /* C++ static initializers - must run before shell_main */
    .init_array :
    {
        __init_array_start = .;
        KEEP(*(.init_array))
        KEEP(*(.ctors))
        __init_array_end = .;
    }

    /* Initialized data */
    .data :
    {
        *(.data)
        *(.data.*)
    }

    /* BSS - uninitialized data (zeroed at startup) */
    .bss :
    {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    }

    /* Discard sections we don't need in a freestanding kernel */
    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame)
        *(.eh_frame_hdr)
        *(.note*)
        *(.gnu*)
        *(.debug*)
    }
}
