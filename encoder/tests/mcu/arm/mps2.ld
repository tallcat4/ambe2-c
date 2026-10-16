/* MPS2 (AN385 / AN386 / AN500): 4 MB code SRAM at 0, 4 MB data SRAM at 0x20000000 */
MEMORY { CODE (rx) : ORIGIN = 0x00000000, LENGTH = 4M  RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 4M }
ENTRY(reset_entry)
reset_entry = 0;
SECTIONS {
    .text : { KEEP(*(.vectors)) *(.text*) *(.init) *(.fini) *(.rodata*) KEEP(*(.eh_frame*)) } > CODE
    .ARM.exidx : { *(.ARM.exidx*) } > CODE
    .preinit_array : { PROVIDE_HIDDEN(__preinit_array_start = .); KEEP(*(.preinit_array)) PROVIDE_HIDDEN(__preinit_array_end = .); } > CODE
    .init_array : { PROVIDE_HIDDEN(__init_array_start = .); KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array)) PROVIDE_HIDDEN(__init_array_end = .); } > CODE
    .fini_array : { PROVIDE_HIDDEN(__fini_array_start = .); KEEP(*(.fini_array)) PROVIDE_HIDDEN(__fini_array_end = .); } > CODE
    __etext = .;
    .data : AT(__etext) { __data_start__ = .; *(.data*) __data_end__ = .; } > RAM
    .bss (NOLOAD) : { __bss_start__ = .; *(.bss*) *(COMMON) __bss_end__ = .; } > RAM
    end = .;
    __end__ = .;
    __stack_top = ORIGIN(RAM) + LENGTH(RAM);
    __StackTop = __stack_top;
}
