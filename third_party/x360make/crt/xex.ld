SECTIONS {
    . = 0x82000000;
    .text : { *(.text) }
    .data : { *(.data) }
    .bss  : { *(.bss)  }
    PROVIDE(entry = _start);
}
