/* QEMU riscv "virt": RAM at 0x80000000 (image and data in the same RAM) */
MEMORY { RAM (rwx) : ORIGIN = 0x80000000, LENGTH = 16M }
ENTRY(_start)
SECTIONS {
    .text : { KEEP(*(.text.start)) *(.text*) *(.rodata*) *(.srodata*) } > RAM
    __etext = .;
    .data : { __data_start__ = .; *(.data*) *(.sdata*) __data_end__ = .; } > RAM
    __global_pointer$ = __data_start__ + 0x800;
    .bss (NOLOAD) : { __bss_start__ = .; *(.bss*) *(.sbss*) *(COMMON) __bss_end__ = .; } > RAM
    . = ALIGN(16);
    end = .;
    __stack_top = ORIGIN(RAM) + LENGTH(RAM);
}
