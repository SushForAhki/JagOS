ENTRY(kernel_main)

SECTIONS {
    . = 0x1000;

    .text : {
        *(.text.boot)
        *(.text)
        *(.text.*)
    }

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    .data : {
        *(.data)
        *(.data.*)
    }

    .bss : {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    }

    /DISCARD/ : {
        *(.comment) *(.eh_frame) *(.eh_frame_hdr)
        *(.note*) *(.gnu*) *(.init_array) *(.ctors)
    }
}
